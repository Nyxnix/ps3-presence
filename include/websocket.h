#ifndef PRESENCE_WEBSOCKET_H
#define PRESENCE_WEBSOCKET_H
#include <stdint.h>
#include <stddef.h>
#define WS_MESSAGE_MAX 4096
struct ws_parser {
    unsigned char header[10],message[WS_MESSAGE_MAX],control[125];
    size_t header_n,header_need,frame_n,frame_size,message_n;
    unsigned opcode,fragment_opcode,fin;
    int failed;
};
typedef int (*ws_event_fn)(void *,unsigned,const unsigned char *,size_t);
void ws_init(struct ws_parser *p);
int ws_feed(struct ws_parser *p,const unsigned char *data,size_t size,ws_event_fn event,void *context);
/* Writer consumes each entire chunk synchronously; zero means success. */
typedef int (*ws_write_fn)(void *,const unsigned char *,size_t);
int ws_write_client_frame(unsigned opcode,const unsigned char *data,size_t size,
                          const unsigned char mask[4],ws_write_fn write,void *context);
int ws_validate_upgrade(const char *headers,size_t size,const char *expected_accept);
#endif
