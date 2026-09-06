#ifndef PRESENCE_WEBMAN_STATUS_H
#define PRESENCE_WEBMAN_STATUS_H
#include "presence.h"
/* Decode one HTML entity at a time; never retain the response body. */
#define WEBMAN_STATUS_MAX_BYTES 65536u
struct webman_status_stream {
    struct observation value;
    char entity[6],header[64];
    size_t total;
    unsigned flags;
    unsigned char header_n,header_end,game_match,html_match,xmb_match,cpu_match,rsx_match;
    unsigned char heading_end,search_match,id_n,stage,title_n,entity_n;
};
void webman_status_init(struct webman_status_stream *);
int webman_status_feed(struct webman_status_stream *,const unsigned char *,size_t);
int webman_status_finish(struct webman_status_stream *,struct observation *);
int presence_webman_detect(struct observation *);
void presence_webman_close(void);
#endif
