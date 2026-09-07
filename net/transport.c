#include "transport.h"
#include "tls_port.h"
#include "websocket.h"
#include "websocket_stream.h"
#include "artwork.h"
#include "presence_clock.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha1.h"
#include <string.h>
#include "trust_store.h"
#define HOST "gateway.discord.gg"
static struct presence_clock discord_clock;
int64_t net_clock_offset(void) { return discord_clock.valid?discord_clock.offset:0; }
int64_t mbedtls_ms_time(void) { return (int64_t)net_milliseconds(); }
struct connection {
    mbedtls_ssl_context ssl;
    mbedtls_ctr_drbg_context rng;
    uint64_t deadline;
    unsigned interval;
    int hello;
    struct discord_client *client;
};
/* The upgrade headers and client output are used in distinct
 * phases. Keep their shared storage in the existing TLS arena, not on the
 * worker stack. The input must remain separate while a phase changes mid-read. */
struct transport_buffers {
    unsigned char input[256];
    union {
        char headers[4096];
        char output[2048];
    } phase;
};
static int retry(struct connection *p,int result) {
    if(result!=MBEDTLS_ERR_SSL_WANT_READ && result!=MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
    if(net_cancelled() || net_milliseconds()>=p->deadline) return 0;
    net_sleep(); return 1;
}
static int write_all(struct connection *p,const unsigned char *bytes,size_t size) {
    size_t done=0; int r;
    while(done<size) {
        if(net_cancelled() || net_milliseconds()>=p->deadline) return -1;
        r=mbedtls_ssl_write(&p->ssl,bytes+done,size-done);
        if(r>0) done+=(size_t)r;
        else if(!retry(p,r)) return r?r:-1;
    } return 0;
}
static int frame_write(void *context,const unsigned char *data,size_t n) {
    return write_all(context,data,n);
}
static int send_frame(struct connection *p,unsigned opcode,const unsigned char *data,size_t n) {
    unsigned char mask[4];
    if(mbedtls_ctr_drbg_random(&p->rng,mask,4)) return -1;
    return ws_write_client_frame(opcode,data,n,mask,frame_write,p);
}
static int stream_event(void *ctx,unsigned opcode,const unsigned char *data,size_t n,unsigned flags) {
    struct connection *p=ctx;
    if(opcode==9) { p->deadline=net_milliseconds()+5000; return send_frame(p,10,data,n); }
    p->client->now=net_milliseconds();
    int r=discord_client_receive(p->client,opcode,data,n,flags);
    if(p->client->gateway.phase==GW_AUTHENTICATING || p->client->gateway.phase==GW_READY) {
        p->hello=1; p->interval=p->client->gateway.interval;
    }
    return r;
}
static int run(const char *hostname,struct discord_client *client,const struct discord_config *config) {
    struct connection p; mbedtls_ssl_config conf; mbedtls_x509_crt ca;
    struct transport_buffers *buffers=0;
    struct ws_stream stream;
    unsigned char nonce[16],key[25],digest[20],accept[29],*input=0;
    char *headers=0,request[512],challenge[61]; size_t n,header_n=0,request_n; int r=-1; uint32_t verify=0;
    uint64_t started=net_milliseconds(); const char *stage="initializing";
    memset(&p,0,sizeof(p)); p.client=client; mbedtls_ssl_init(&p.ssl); mbedtls_ssl_config_init(&conf);
    discord_client_connected(client,started);
    mbedtls_x509_crt_init(&ca); mbedtls_ctr_drbg_init(&p.rng); ws_stream_init(&stream);
    net_report(stage,0,0,0,0,0);
    stage="buffers"; buffers=presence_calloc(1,sizeof(*buffers)); if(!buffers) { r=-1001; goto done; }
    input=buffers->input; headers=buffers->phase.headers;
    stage="random";
    r=mbedtls_ctr_drbg_seed(&p.rng,net_entropy,0,(const unsigned char *)"ps3-presence-transport-v1",25); if(r) goto done;
    stage="trust_store"; r=presence_load_roots(&ca); if(r) goto done;
    r=mbedtls_ssl_config_defaults(&conf,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT); if(r) goto done;
    mbedtls_ssl_conf_authmode(&conf,MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf,&ca,0); mbedtls_ssl_conf_rng(&conf,mbedtls_ctr_drbg_random,&p.rng);
    r=mbedtls_ssl_setup(&p.ssl,&conf); if(r) goto done;
    r=mbedtls_ssl_set_hostname(&p.ssl,hostname); if(r) goto done;
    stage="tcp_connect"; net_report(stage,0,0,presence_arena_peak(),net_milliseconds()-started,0);
    r=net_open(hostname); if(r) goto done;
    mbedtls_ssl_set_bio(&p.ssl,0,net_send,net_recv,0);
    stage="tls_handshake"; p.deadline=net_milliseconds()+30000;
    net_report(stage,0,0,presence_arena_peak(),net_milliseconds()-started,0);
    do { r=mbedtls_ssl_handshake(&p.ssl); } while(retry(&p,r));
    verify=mbedtls_ssl_get_verify_result(&p.ssl); if(r || verify) { if(!r) r=-1; goto done; }
    stage="websocket_upgrade"; net_report(stage,0,verify,presence_arena_peak(),net_milliseconds()-started,0);
    r=mbedtls_ctr_drbg_random(&p.rng,nonce,sizeof(nonce)); if(r) goto done;
    r=mbedtls_base64_encode(key,sizeof(key),&n,nonce,sizeof(nonce)); if(r) goto done;
    memcpy(challenge,key,24); memcpy(challenge+24,"258EAFA5-E914-47DA-95CA-C5AB0DC85B11",36);
    r=mbedtls_sha1((unsigned char *)challenge,60,digest); if(r) goto done;
    r=mbedtls_base64_encode(accept,sizeof(accept),&n,digest,20); if(r) goto done;
    const char *prefix="GET /?v=10&encoding=json HTTP/1.1\r\nHost: ";
    const char *suffix="\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: ";
    if(strlen(prefix)+strlen(hostname)+strlen(suffix)+28>sizeof(request)) { r=-8; goto done; }
    request_n=strlen(prefix); memcpy(request,prefix,request_n);
    memcpy(request+request_n,hostname,strlen(hostname)); request_n+=strlen(hostname);
    memcpy(request+request_n,suffix,strlen(suffix)); request_n+=strlen(suffix);
    memcpy(request+request_n,key,24); request_n+=24;
    memcpy(request+request_n,"\r\n\r\n",4); request_n+=4;
    p.deadline=net_milliseconds()+15000; r=write_all(&p,(unsigned char *)request,request_n); if(r) goto done;
    while(!p.hello && !net_cancelled() && net_milliseconds()<p.deadline) {
        r=mbedtls_ssl_read(&p.ssl,input,sizeof(buffers->input));
        if(r<=0) { if(retry(&p,r)) continue; r=r?r:-1; goto done; }
        size_t at=0;
        if(stage[0]=='w') {
            while(at<(size_t)r) {
                if(header_n==sizeof(buffers->phase.headers)) { r=-2; goto done; }
                headers[header_n++]=(char)input[at++];
                if(header_n>=4 && !memcmp(headers+header_n-4,"\r\n\r\n",4)) {
                    if(!ws_validate_upgrade(headers,header_n,(char *)accept)) { r=-3; goto done; }
                    presence_clock_calibrate(&discord_clock,headers,header_n,presence_time(0));
                    stage="gateway_hello";
                    {
                        discord_client_connected(client,net_milliseconds());
                        int random_error=mbedtls_ctr_drbg_random(&p.rng,(unsigned char *)&client->random,sizeof(client->random));
                        if(random_error) { r=random_error; goto done; }
                    }
                    break;
                }
            }
        }
        if(stage[0]=='g' && at<(size_t)r) {
            int parsed=ws_stream_feed(&stream,input+at,(size_t)r-at,stream_event,&p);
            if(parsed) { r=-4; goto done; }
        }
    }
    if(!p.hello) { r=-5; goto done; }
    {
        char *output=buffers->phase.output; struct presence_session latest;
        uint64_t report_at=0,config_at=0; unsigned reported_ready=0;
        stage="authenticating"; net_report(stage,0,verify,presence_arena_peak(),net_milliseconds()-started,p.interval);
        while(!net_cancelled()) {
            uint64_t now=net_milliseconds(); int length;
            struct discord_config effective;
            if(now>=config_at) { config_at=now+2000; if(net_config_changed(config)) { r=-701; goto done; } }
            net_snapshot(&latest);
            if(client->gateway.phase==GW_READY && net_artwork_pending(&latest)) { r=-704; goto done; }
            effective=*config; net_artwork_apply(&effective,&latest);
            /* Artwork identifies the raw console session, not its corrected wire timestamp. */
            latest.started_at=presence_clock_start(&discord_clock,latest.started_at);
            length=discord_client_prepare(client,&effective,&latest,now,output,sizeof(buffers->phase.output));
            discord_wipe(&effective,sizeof(effective));
            if(length<0) { discord_wipe(output,sizeof(buffers->phase.output)); r=-700; goto done; }
            if(length) {
                p.deadline=now+5000; r=send_frame(&p,1,(unsigned char *)output,(size_t)length);
                discord_wipe(output,sizeof(buffers->phase.output)); if(r) goto done;
                discord_client_sent(client,net_milliseconds());
            }
            if(client->gateway.phase==GW_READY && (!reported_ready || now>=report_at)) {
                stage="connected"; reported_ready=1; report_at=now+30000;
                net_report(stage,0,verify,presence_arena_peak(),now-started,p.interval);
            }
            r=mbedtls_ssl_read(&p.ssl,input,sizeof(buffers->input));
            if(r==MBEDTLS_ERR_SSL_WANT_READ || r==MBEDTLS_ERR_SSL_WANT_WRITE) { net_sleep(); continue; }
            if(r<=0) { r=r?r:-1; goto done; }
            if(ws_stream_feed(&stream,input,(size_t)r,stream_event,&p)) { r=-702; goto done; }
        }
        r=-703; goto done;
    }
done:
    {
        if(p.hello && (net_cancelled() || r==-701)) {
            char clear[512]; struct presence_session empty;
            int stopping=net_cancelled(); memset(&empty,0,sizeof(empty));
            net_teardown_io(1); p.deadline=net_milliseconds()+500;
            if(client->gateway.phase==GW_READY) {
                size_t length=discord_activity(config,&empty,clear,sizeof(clear));
                if(length) send_frame(&p,1,(unsigned char *)clear,length);
                discord_wipe(clear,sizeof(clear));
            }
            { const unsigned char code[]={3,232}; send_frame(&p,8,code,sizeof(code)); }
            int closing;
            do { closing=mbedtls_ssl_close_notify(&p.ssl); } while(retry(&p,closing));
            net_teardown_io(0); client->close_code=1000;
            stage=stopping?"stopped":"configuration_changed";
        }
        if(client->close_code && r!=-701 && r!=-703) { stage="gateway_closed"; r=-(int)client->close_code; }
        if(net_cancelled()) stage="stopped";
        discord_client_disconnected(client,net_milliseconds(),client->random);
    }
    net_close(); mbedtls_ssl_free(&p.ssl); mbedtls_ssl_config_free(&conf); mbedtls_x509_crt_free(&ca); mbedtls_ctr_drbg_free(&p.rng);
    presence_free(buffers);
    net_report(stage,r,verify,presence_arena_peak(),net_milliseconds()-started,p.interval);
    return r;
}
int transport_client(struct discord_client *client,const struct discord_config *config) {
    const char *host=client->gateway.resumable && client->resume_host[0]?client->resume_host:HOST;
    return run(host,client,config);
}
