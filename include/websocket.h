#ifndef PRESENCE_WEBSOCKET_H
#define PRESENCE_WEBSOCKET_H
#include <stdint.h>
#include <stddef.h>
#define WS_MESSAGE_MAX 4096
/* Writer consumes each entire chunk synchronously; zero means success. */
typedef int (*ws_write_fn)(void *,const unsigned char *,size_t);
int ws_write_client_frame(unsigned opcode,const unsigned char *data,size_t size,
                          const unsigned char mask[4],ws_write_fn write,void *context);
int ws_validate_upgrade(const char *headers,size_t size,const char *expected_accept);
#endif
