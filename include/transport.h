#ifndef PRESENCE_TRANSPORT_H
#define PRESENCE_TRANSPORT_H
#include <stddef.h>
#include <stdint.h>
#include "discord_client.h"
/* Implemented by the PS3 adapter. */
int net_open(const char *host);
int net_send(void *unused,const unsigned char *data,size_t size);
int net_recv(void *unused,unsigned char *data,size_t size);
void net_close(void);
uint64_t net_milliseconds(void);
void net_sleep(void);
int net_cancelled(void);
int net_entropy(void *unused,unsigned char *data,size_t size);
void net_report(const char *stage,int error,uint32_t verify,size_t peak,uint64_t elapsed,unsigned heartbeat);
/* Arena must be initialized before calling. No Identify or credentials. */
int transport_probe(const char *verify_hostname);
int transport_client(struct discord_client *client,const struct discord_config *config);
void net_snapshot(struct presence_session *presence);
int net_config_changed(const struct discord_config *config);
void net_teardown_io(int enabled);
int gateway_hello(const unsigned char *data,size_t size,unsigned *interval);
int gateway_is_ack(const unsigned char *data,size_t size);
#endif
