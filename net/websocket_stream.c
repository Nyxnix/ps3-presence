#include "websocket_stream.h"
#include <string.h>
static int utf8(struct ws_stream *p,unsigned c) {
    if(p->utf8_left) {
        if((c&0xc0)!=0x80) return -1;
        p->utf8_value=(p->utf8_value<<6)|(c&63);
        if(!--p->utf8_left && (p->utf8_value<p->utf8_min || p->utf8_value>0x10ffff || (p->utf8_value>=0xd800 && p->utf8_value<=0xdfff))) return -1;
    } else if(c>=128) {
        if(c>=0xc2 && c<=0xdf) { p->utf8_left=1; p->utf8_value=c&31; p->utf8_min=0x80; }
        else if(c>=0xe0 && c<=0xef) { p->utf8_left=2; p->utf8_value=c&15; p->utf8_min=0x800; }
        else if(c>=0xf0 && c<=0xf4) { p->utf8_left=3; p->utf8_value=c&7; p->utf8_min=0x10000; }
        else return -1;
    }
    return 0;
}
void ws_stream_init(struct ws_stream *p) { memset(p,0,sizeof(*p)); p->header_need=2; }
static int begin(struct ws_stream *p,ws_chunk_fn fn,void *ctx) {
    unsigned size=p->header[1]&127; size_t length=size,i;
    p->opcode=p->header[0]&15; p->fin=p->header[0]>>7;
    if((p->header[0]&0x70) || (p->header[1]&0x80)) return -1;
    if(p->opcode!=0 && p->opcode!=1 && p->opcode!=8 && p->opcode!=9 && p->opcode!=10) return -1;
    if(size>=126) {
        length=0;
        for(i=2;i<p->header_need;i++) {
            if(length>WS_STREAM_MAX/256) return -1;
            length=length*256+p->header[i];
        }
        if((size==126 && length<126) || (size==127 && length<65536)) return -1;
    }
    if(p->opcode>=8) {
        if(!p->fin || length>125 || (p->opcode==8 && length==1)) return -1;
    } else {
        if(!p->opcode) { if(!p->fragment) return -1; }
        else {
            if(p->fragment) return -1;
            p->message_n=0; p->utf8_left=0;
            if(fn(ctx,1,0,0,WS_CHUNK_BEGIN)) return -1;
        }
        if(length>WS_STREAM_MAX-p->message_n) return -1;
    }
    p->frame_size=length; p->frame_n=0; return 0;
}
static int end(struct ws_stream *p,ws_chunk_fn fn,void *ctx) {
    int r=0;
    if(p->opcode>=8) {
        if(p->opcode==8 && p->frame_size>=2) {
            unsigned code=p->control[0]*256+p->control[1]; struct ws_stream reason; size_t i;
            if(code<1000 || code>=5000 || code==1004 || code==1005 || code==1006 || (code>=1016 && code<3000)) return -1;
            ws_stream_init(&reason);
            for(i=2;i<p->frame_size;i++) if(utf8(&reason,p->control[i])) return -1;
            if(reason.utf8_left) return -1;
        }
        r=fn(ctx,p->opcode,p->control,p->frame_size,WS_CHUNK_BEGIN|WS_CHUNK_END);
    } else if(p->fin) {
        if(p->utf8_left) return -1;
        r=fn(ctx,1,0,0,WS_CHUNK_END); p->fragment=0;
    } else if(p->opcode) p->fragment=p->opcode;
    p->header_n=0; p->header_need=2; p->frame_n=p->frame_size=0; return r;
}
int ws_stream_feed(struct ws_stream *p,const unsigned char *data,size_t n,ws_chunk_fn fn,void *ctx) {
    size_t at=0;
    if(p->failed || !fn || (!data && n)) goto fail;
    while(at<n) {
        if(p->header_n<p->header_need) {
            p->header[p->header_n++]=data[at++];
            if(p->header_n==2) p->header_need=(p->header[1]&127)==126?4:(p->header[1]&127)==127?10:2;
            if(p->header_n<p->header_need) continue;
            if(begin(p,fn,ctx)) goto fail;
        }
        size_t chunk=p->frame_size-p->frame_n,i;
        if(chunk>n-at) chunk=n-at;
        if(p->opcode>=8) { if(chunk) memcpy(p->control+p->frame_n,data+at,chunk); }
        else if(chunk) {
            for(i=0;i<chunk;i++) if(utf8(p,data[at+i])) goto fail;
            if(fn(ctx,1,data+at,chunk,0)) goto fail;
            p->message_n+=chunk;
        }
        at+=chunk; p->frame_n+=chunk;
        if(p->frame_n==p->frame_size && end(p,fn,ctx)) goto fail;
    }
    return 0;
fail:
    p->failed=1; return -1;
}
