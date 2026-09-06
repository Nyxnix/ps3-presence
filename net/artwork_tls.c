#include "artwork.h"
#include "transport.h"
#include "tls_port.h"
#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "trust_store.h"
#include <string.h>
static int again(int r,uint64_t deadline) {
    if((r!=MBEDTLS_ERR_SSL_WANT_READ && r!=MBEDTLS_ERR_SSL_WANT_WRITE) || net_cancelled() || net_milliseconds()>=deadline) return 0;
    net_sleep(); return 1;
}
static int put(char *out,size_t *n,size_t cap,const char *text) {
    size_t length=strlen(text); if(length>=cap-*n) return -1;
    memcpy(out+*n,text,length); *n+=length; out[*n]=0; return 0;
}
int artwork_fetch(const struct discord_config *config,const char title_id[10],char asset[257]) {
    mbedtls_ssl_context ssl; mbedtls_ssl_config conf; mbedtls_ctr_drbg_context rng; mbedtls_x509_crt roots;
    unsigned char *response=0,*body; char request[1024],payload[160],url[128],digits[8];
    size_t n=0,payload_n=0,received=0,sent=0,body_n; int r=-1,result=0; uint64_t deadline;
    asset[0]=0; if(!artwork_url(title_id,url)) return 0;
    mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf); mbedtls_ctr_drbg_init(&rng); mbedtls_x509_crt_init(&roots);
    response=presence_calloc(1,8192); if(!response) goto done;
    r=mbedtls_ctr_drbg_seed(&rng,net_entropy,0,(const unsigned char *)"ps3-artwork-v1",14); if(r) goto done;
    r=presence_load_roots(&roots); if(r) goto done;
    r=mbedtls_ssl_config_defaults(&conf,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT); if(r) goto done;
    mbedtls_ssl_conf_authmode(&conf,MBEDTLS_SSL_VERIFY_REQUIRED); mbedtls_ssl_conf_ca_chain(&conf,&roots,0); mbedtls_ssl_conf_rng(&conf,mbedtls_ctr_drbg_random,&rng);
    r=mbedtls_ssl_setup(&ssl,&conf); if(r) goto done;
    r=mbedtls_ssl_set_hostname(&ssl,"discord.com"); if(r) goto done;
    r=net_open("discord.com"); if(r) goto done;
    mbedtls_ssl_set_bio(&ssl,0,net_send,net_recv,0); deadline=net_milliseconds()+15000;
    do { r=mbedtls_ssl_handshake(&ssl); } while(again(r,deadline));
    if(r || mbedtls_ssl_get_verify_result(&ssl)) goto done;
    if(put(payload,&payload_n,sizeof(payload),"{\"urls\":[\"") || put(payload,&payload_n,sizeof(payload),url) || put(payload,&payload_n,sizeof(payload),"\"]}")) goto done;
    size_t value=payload_n,at=sizeof(digits)-1; digits[at]=0;
    do { digits[--at]=(char)('0'+value%10); value/=10; } while(value);
    if(put(request,&n,sizeof(request),"POST /api/v10/applications/") || put(request,&n,sizeof(request),config->application_id) || put(request,&n,sizeof(request),"/external-assets HTTP/1.1\r\nHost: discord.com\r\nUser-Agent: PS3Presence/0.3\r\nAuthorization: ") || put(request,&n,sizeof(request),config->token) || put(request,&n,sizeof(request),"\r\nContent-Type: application/json\r\nAccept-Encoding: identity\r\nConnection: close\r\nContent-Length: ") || put(request,&n,sizeof(request),digits+at) || put(request,&n,sizeof(request),"\r\n\r\n") || put(request,&n,sizeof(request),payload)) goto done;
    deadline=net_milliseconds()+10000;
    while(sent<n) {
        r=mbedtls_ssl_write(&ssl,(unsigned char *)request+sent,n-sent);
        if(r>0) sent+=(size_t)r; else if(!again(r,deadline)) goto done;
    }
    discord_wipe(request,sizeof(request));
    while(received<8192 && !net_cancelled() && net_milliseconds()<deadline) {
        r=mbedtls_ssl_read(&ssl,response+received,8192-received);
        if(r>0) received+=(size_t)r;
        else if(r==0 || r==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) break;
        else if(!again(r,deadline)) goto done;
    }
    if(received==8192) goto done;
    r=artwork_http(response,received,&body,&body_n);
    if(r==200) result=artwork_parse(body,body_n,asset);
    else if(r==429) result=-429;
done:
    discord_wipe(request,sizeof(request)); net_close(); mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); mbedtls_x509_crt_free(&roots); mbedtls_ctr_drbg_free(&rng); presence_free(response);
    return result;
}
