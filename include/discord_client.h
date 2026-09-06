#ifndef PRESENCE_DISCORD_CLIENT_H
#define PRESENCE_DISCORD_CLIENT_H
#include "gateway.h"
#include "gateway_json.h"
#include "discord_wire.h"
struct discord_client {
    struct gateway gateway;
    struct gateway_json json;
    char session_id[129],resume_host[254];
    uint64_t generation_sent,generation_pending,presence_at;
    unsigned presence_sent,close_code,reconnect,invalid_session,resume_attempt,pending,ws_opcode;
    uint64_t now;
    uint32_t random;
};
void discord_client_init(struct discord_client *c);
void discord_client_connected(struct discord_client *c,uint64_t now);
/* Return 0 on success, -1 on invalid input; ping replies are handled by I/O. */
int discord_client_receive(void *ctx,unsigned opcode,const unsigned char *bytes,size_t n,unsigned flags);
/* Returns payload length, 0 for no action, or -1 to disconnect. */
int discord_client_prepare(struct discord_client *c,const struct discord_config *config,const struct presence_session *presence,uint64_t now,char *out,size_t cap);
void discord_client_sent(struct discord_client *c,uint64_t now);
void discord_client_disconnected(struct discord_client *c,uint64_t now,uint32_t random);
int discord_resume_host(const char *url,char out[254]);
#endif
