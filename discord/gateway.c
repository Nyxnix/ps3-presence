#include "gateway.h"
#include <string.h>
void gateway_init(struct gateway *g) { memset(g,0,sizeof(*g)); }
void gateway_connected(struct gateway *g,uint64_t now) {
    g->phase=GW_HELLO; g->hello_deadline=now+15000;
    g->awaiting_ack=0; g->requested_heartbeat=0; g->auth_pending=0;
}
int gateway_on_hello(struct gateway *g,uint32_t interval,uint64_t now,uint32_t random) {
    if(g->phase!=GW_HELLO || interval<1000 || interval>300000) return 0;
    g->interval=interval; g->next_heartbeat=now+random%interval;
    g->phase=GW_AUTHENTICATING; g->auth_pending=1; g->ready_deadline=now+30000;
    return 1;
}
enum gateway_action gateway_poll(struct gateway *g,uint64_t now) {
    if(g->phase==GW_BLOCKED) return GW_WAIT;
    if(g->phase==GW_OFFLINE) return now>=g->retry_at?GW_CONNECT:GW_WAIT;
    if(g->phase==GW_HELLO) return now>=g->hello_deadline?GW_RECONNECT:GW_WAIT;
    if(g->awaiting_ack && now>=g->ack_deadline) return GW_RECONNECT;
    if(g->phase==GW_AUTHENTICATING && now>=g->ready_deadline) return GW_RECONNECT;
    if(g->auth_pending) return g->resumable && g->has_sequence?GW_RESUME:GW_IDENTIFY;
    if(g->requested_heartbeat || now>=g->next_heartbeat) return GW_HEARTBEAT;
    return GW_WAIT;
}
void gateway_auth_sent(struct gateway *g,uint64_t now) {
    g->auth_pending=0; g->ready_deadline=now+30000;
}
void gateway_heartbeat_sent(struct gateway *g,uint64_t now) {
    /* A requested heartbeat must not extend an outstanding ACK deadline or
       move the regular schedule. Call only after the complete frame is sent. */
    if(!g->awaiting_ack) g->ack_deadline=now+g->interval;
    g->awaiting_ack=1;
    if(now>=g->next_heartbeat) g->next_heartbeat=now+g->interval;
    g->requested_heartbeat=0;
}
void gateway_ack(struct gateway *g) { g->awaiting_ack=0; }
void gateway_request_heartbeat(struct gateway *g) { g->requested_heartbeat=1; }
void gateway_dispatch(struct gateway *g,uint64_t sequence) {
    if(!g->has_sequence || sequence>g->sequence) g->sequence=sequence;
    g->has_sequence=1;
}
void gateway_ready(struct gateway *g,int can_resume) {
    g->phase=GW_READY; g->auth_pending=0; g->resumable=can_resume!=0; g->failures=0;
}
static void forget_session(struct gateway *g) { g->resumable=0; g->has_sequence=0; g->sequence=0; }
void gateway_disconnected(struct gateway *g,unsigned code,uint64_t now,uint32_t random) {
    uint32_t delay;
    g->awaiting_ack=0; g->requested_heartbeat=0; g->auth_pending=0;
    if(code==4004 || (code>=4010 && code<=4014)) {
        forget_session(g); g->phase=GW_BLOCKED; return;
    }
    if(code==4007 || code==4009 || code==1000 || code==1001) forget_session(g);
    if(g->failures<7) g->failures++;
    delay=1000u<<(g->failures-1); if(delay>60000) delay=60000;
    if(code==4008 && delay<60000) delay=60000;
    g->retry_at=now+delay+random%(delay/4+1); g->phase=GW_OFFLINE;
}
void gateway_invalid_session(struct gateway *g,int can_resume,uint64_t now,uint32_t random) {
    if(!can_resume) forget_session(g);
    gateway_disconnected(g,0,now,random);
    /* Invalid-session responses require a random 1..5 second delay. Keep a
       longer failure backoff already earned by repeated connection failures. */
    uint64_t retry=now+1000+random%4001;
    if(retry>g->retry_at) g->retry_at=retry;
}
