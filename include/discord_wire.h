#ifndef PRESENCE_DISCORD_WIRE_H
#define PRESENCE_DISCORD_WIRE_H
#include "presence.h"
#define DISCORD_CONFIG_MAX 1024
struct discord_config {
    char application_id[21],token[257],status[10],large_image[257],small_image[257];
    unsigned enabled,show_xmb;
};
/* Strict, bounded key=value file. On failure the entire output is wiped. */
int discord_config_parse(struct discord_config *c,const char *bytes,size_t n);
void discord_wipe(void *p,size_t n);
size_t discord_identify(const struct discord_config *c,char *out,size_t cap);
size_t discord_resume(const struct discord_config *c,const char *session_id,uint64_t sequence,char *out,size_t cap);
size_t discord_heartbeat(int has_sequence,uint64_t sequence,char *out,size_t cap);
size_t discord_activity(const struct discord_config *c,const struct presence_session *s,char *out,size_t cap);
#endif
