#ifndef PRESENCE_GATEWAY_JSON_H
#define PRESENCE_GATEWAY_JSON_H
#include <stddef.h>
#include <stdint.h>
/* Streaming extraction: unrelated account data is validated and discarded. */
struct gateway_event {
    unsigned opcode,has_sequence,heartbeat_interval,can_resume,data_kind;
    uint64_t sequence;
    char type[65],session_id[129],resume_url[257];
};
struct gateway_json {
    struct gateway_event event;
    struct { unsigned char state,path,key; unsigned seen; } stack[64];
    unsigned depth,root_done,failed,mode,is_key,capture,escape,unicode,unicode_value;
    unsigned utf8_left,utf8_value,utf8_min,root_seen;
    size_t total,token_n;
    char token[257];
};
void gateway_json_init(struct gateway_json *p);
int gateway_json_feed(struct gateway_json *p,const unsigned char *data,size_t n);
int gateway_json_finish(struct gateway_json *p);
#endif
