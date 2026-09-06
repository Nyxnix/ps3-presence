#include "discord_client.h"
#include "websocket_stream.h"
#include <string.h>
int discord_resume_host(const char *url,char out[254]) {
    size_t n,i; out[0]=0;
    if(strncmp(url,"wss://",6)) return 0;
    n=strlen(url+6); if(n && url[6+n-1]=='/') n--;
    if(n<12 || n>253) return 0;
    for(i=0;i<n;i++) {
        unsigned char c=(unsigned char)url[6+i]; if(c>='A' && c<='Z') c+=32;
        if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='-' || c=='.')) { out[0]=0; return 0; }
        out[i]=(char)c;
    }
    out[n]=0;
    if(strcmp(out+n-11,".discord.gg") || out[0]=='.' || strstr(out,"..")) { out[0]=0; return 0; }
    return 1;
}
void discord_client_init(struct discord_client *c) { memset(c,0,sizeof(*c)); gateway_init(&c->gateway); }
void discord_client_connected(struct discord_client *c,uint64_t now) {
    gateway_connected(&c->gateway,now); c->close_code=c->reconnect=c->invalid_session=c->pending=0;
    c->presence_sent=0; c->presence_at=now; c->now=now;
}
static int event(struct discord_client *c) {
    struct gateway_event *e=&c->json.event; struct gateway *g=&c->gateway;
    if(e->opcode==10) return e->data_kind==4 && gateway_on_hello(g,e->heartbeat_interval,c->now,c->random)?0:-1;
    if(g->phase!=GW_AUTHENTICATING && g->phase!=GW_READY) return -1;
    if(e->opcode==11) { gateway_ack(g); return 0; }
    if(e->opcode==1) { gateway_request_heartbeat(g); return 0; }
    if(e->opcode==7) { c->reconnect=1; return 0; }
    if(e->opcode==9) {
        if(e->data_kind!=2 && e->data_kind!=3) return -1;
        c->invalid_session=e->can_resume?2:1; c->reconnect=1; return 0;
    }
    if(e->opcode==0) {
        if(!e->has_sequence) return -1;
        gateway_dispatch(g,e->sequence);
        if(!strcmp(e->type,"READY")) {
            if(g->phase!=GW_AUTHENTICATING || g->auth_pending) return -1;
            unsigned resumable=e->session_id[0] && discord_resume_host(e->resume_url,c->resume_host);
            if(resumable) memcpy(c->session_id,e->session_id,sizeof(c->session_id));
            else c->session_id[0]=c->resume_host[0]=0;
            gateway_ready(g,resumable);
        } else if(!strcmp(e->type,"RESUMED")) {
            if(g->phase!=GW_AUTHENTICATING || g->auth_pending || !c->resume_attempt) return -1;
            gateway_ready(g,1);
        }
    }
    return 0;
}
int discord_client_receive(void *ctx,unsigned opcode,const unsigned char *bytes,size_t n,unsigned flags) {
    struct discord_client *c=ctx;
    if(opcode==8) { c->close_code=n>=2?bytes[0]*256+bytes[1]:1000; c->reconnect=1; return 0; }
    if(opcode==10) return 0;
    if(opcode!=1) return -1;
    if(flags&WS_CHUNK_BEGIN) gateway_json_init(&c->json);
    if(gateway_json_feed(&c->json,bytes,n)) return -1;
    if(flags&WS_CHUNK_END) { if(gateway_json_finish(&c->json)) return -1; return event(c); }
    return 0;
}
int discord_client_prepare(struct discord_client *c,const struct discord_config *config,const struct presence_session *presence,uint64_t now,char *out,size_t cap) {
    enum gateway_action action; size_t n=0;
    if(c->reconnect || c->pending || !config->enabled) return -1;
    c->now=now; action=gateway_poll(&c->gateway,now); c->ws_opcode=1;
    if(action==GW_RECONNECT) return -1;
    if(action==GW_IDENTIFY) { c->pending=1; c->resume_attempt=0; n=discord_identify(config,out,cap); }
    else if(action==GW_RESUME) { c->pending=1; c->resume_attempt=1; n=discord_resume(config,c->session_id,c->gateway.sequence,out,cap); }
    else if(action==GW_HEARTBEAT) { c->pending=2; n=discord_heartbeat(c->gateway.has_sequence,c->gateway.sequence,out,cap); }
    else if(c->gateway.phase==GW_READY && now>=c->presence_at && (!c->presence_sent || c->generation_sent!=presence->generation)) {
        c->pending=3; c->generation_pending=presence->generation; n=discord_activity(config,presence,out,cap);
    }
    if(c->pending && !n) return -1;
    return (int)n;
}
void discord_client_sent(struct discord_client *c,uint64_t now) {
    if(c->pending==1) gateway_auth_sent(&c->gateway,now);
    if(c->pending==2) gateway_heartbeat_sent(&c->gateway,now);
    if(c->pending==3) { c->generation_sent=c->generation_pending; c->presence_sent=1; c->presence_at=now+5000; }
    c->pending=0;
}
void discord_client_disconnected(struct discord_client *c,uint64_t now,uint32_t random) {
    if(c->invalid_session) gateway_invalid_session(&c->gateway,c->invalid_session==2,now,random);
    else gateway_disconnected(&c->gateway,c->close_code,now,random);
    c->pending=0;
    if(!c->gateway.resumable) c->session_id[0]=c->resume_host[0]=0;
}
