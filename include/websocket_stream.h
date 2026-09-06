#ifndef PRESENCE_WEBSOCKET_STREAM_H
#define PRESENCE_WEBSOCKET_STREAM_H
#include <stddef.h>
#define WS_STREAM_MAX (8u*1024u*1024u)
#define WS_CHUNK_BEGIN 1u
#define WS_CHUNK_END 2u
struct ws_stream {
    unsigned char header[10],control[125];
    size_t header_n,header_need,frame_n,frame_size,message_n;
    unsigned opcode,fragment,fin,utf8_left,utf8_value,utf8_min,failed;
};
/* BEGIN/END can be zero-length calls. Control frames arrive as one call. */
typedef int (*ws_chunk_fn)(void *,unsigned,const unsigned char *,size_t,unsigned);
void ws_stream_init(struct ws_stream *p);
int ws_stream_feed(struct ws_stream *p,const unsigned char *data,size_t n,ws_chunk_fn event,void *context);
#endif
