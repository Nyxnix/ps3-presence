#ifndef PRESENCE_ARTWORK_H
#define PRESENCE_ARTWORK_H
#include "discord_wire.h"
/* Runs only when no Gateway socket is open; shares the bounded TLS arena. */
int artwork_fetch(const struct discord_config *config,const char title_id[10],char asset[257]);
int artwork_url(const char title_id[10],char url[128]);
int artwork_parse(const unsigned char *json,size_t size,char asset[257]);
/* Decode a complete HTTP response in place. Body points inside response. */
int artwork_http(unsigned char *response,size_t size,unsigned char **body,size_t *body_size);
int net_artwork_pending(const struct presence_session *presence);
void net_artwork_apply(struct discord_config *config,const struct presence_session *presence);
#endif
