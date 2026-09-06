#ifndef PRESENCE_GATEWAY_H
#define PRESENCE_GATEWAY_H
#include <stdint.h>
/* One owner (the network thread), with monotonic milliseconds only. */
enum gateway_phase { GW_OFFLINE, GW_HELLO, GW_AUTHENTICATING, GW_READY, GW_BLOCKED };
enum gateway_action { GW_WAIT, GW_CONNECT, GW_IDENTIFY, GW_RESUME, GW_HEARTBEAT, GW_RECONNECT };
struct gateway {
    enum gateway_phase phase;
    uint64_t retry_at, hello_deadline, ready_deadline, next_heartbeat, ack_deadline;
    uint64_t sequence;
    uint32_t interval, failures;
    unsigned has_sequence, resumable, awaiting_ack, requested_heartbeat, auth_pending;
};
void gateway_init(struct gateway *g);
void gateway_connected(struct gateway *g,uint64_t now);
int gateway_on_hello(struct gateway *g,uint32_t interval,uint64_t now,uint32_t random);
enum gateway_action gateway_poll(struct gateway *g,uint64_t now);
void gateway_auth_sent(struct gateway *g,uint64_t now);
void gateway_heartbeat_sent(struct gateway *g,uint64_t now);
void gateway_ack(struct gateway *g);
void gateway_request_heartbeat(struct gateway *g);
void gateway_dispatch(struct gateway *g,uint64_t sequence);
void gateway_ready(struct gateway *g,int can_resume);
/* 4004/4010..4014 require changed configuration; no automatic retry. */
void gateway_disconnected(struct gateway *g,unsigned close_code,uint64_t now,uint32_t random);
void gateway_invalid_session(struct gateway *g,int can_resume,uint64_t now,uint32_t random);
#endif
