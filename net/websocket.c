#include "websocket.h"
#include <string.h>
static int utf8(const unsigned char *p,size_t n) {
    size_t i=0; while(i<n) {
        unsigned c=p[i++],v,count,min;
        if(c<128) continue;
        if(c>=0xc2 && c<=0xdf) { v=c&31; count=1; min=0x80; }
        else if(c>=0xe0 && c<=0xef) { v=c&15; count=2; min=0x800; }
        else if(c>=0xf0 && c<=0xf4) { v=c&7; count=3; min=0x10000; }
        else return 0;
        if(count>n-i) return 0;
        while(count--) { c=p[i++]; if((c&0xc0)!=0x80) return 0; v=(v<<6)|(c&63); }
        if(v<min || v>0x10ffff || (v>=0xd800 && v<=0xdfff)) return 0;
    } return 1;
}
void ws_init(struct ws_parser *p) { memset(p,0,sizeof(*p)); p->header_need=2; }
static int begin_frame(struct ws_parser *p) {
    unsigned n=p->header[1]&127; size_t length=n,i;
    p->opcode=p->header[0]&15; p->fin=p->header[0]>>7;
    if((p->header[0]&0x70) || (p->header[1]&0x80)) return -1;
    if(p->opcode!=0 && p->opcode!=1 && p->opcode!=2 && p->opcode!=8 && p->opcode!=9 && p->opcode!=10) return -1;
    if(n>=126) {
        length=0;
        for(i=2;i<p->header_need;i++) {
            if(length>WS_MESSAGE_MAX/256) return -1;
            length=length*256+p->header[i];
        }
        if((n==126 && length<126) || (n==127 && length<65536)) return -1;
    }
    if(p->opcode>=8) {
        if(!p->fin || length>125 || (p->opcode==8 && length==1)) return -1;
    } else {
        if(p->opcode==0) { if(!p->fragment_opcode) return -1; }
        else { if(p->fragment_opcode) return -1; p->message_n=0; }
        if(length>WS_MESSAGE_MAX-p->message_n) return -1;
    }
    p->frame_size=length; p->frame_n=0; return 0;
}
static int finish_frame(struct ws_parser *p,ws_event_fn event,void *ctx) {
    int result=0;
    if(p->opcode>=8) {
        if(p->opcode==8 && p->frame_size>=2) {
            unsigned code=p->control[0]*256+p->control[1];
            if(code<1000 || code>=5000 || code==1004 || code==1005 || code==1006 || (code>=1016 && code<3000) || !utf8(p->control+2,p->frame_size-2)) return -1;
        }
        result=event(ctx,p->opcode,p->control,p->frame_size);
    } else if(p->fin) {
        unsigned type=p->opcode ? p->opcode : p->fragment_opcode;
        if(type==1 && !utf8(p->message,p->message_n)) return -1;
        result=event(ctx,type,p->message,p->message_n);
        p->fragment_opcode=0; p->message_n=0;
    } else if(p->opcode) p->fragment_opcode=p->opcode;
    p->header_n=0; p->header_need=2; p->frame_n=p->frame_size=0; return result;
}
int ws_feed(struct ws_parser *p,const unsigned char *data,size_t n,ws_event_fn event,void *ctx) {
    size_t at=0;
    if(p->failed || !event) return -1;
    while(at<n) {
        if(p->header_n<p->header_need) {
            p->header[p->header_n++]=data[at++];
            if(p->header_n==2) p->header_need=(p->header[1]&127)==126 ? 4 : (p->header[1]&127)==127 ? 10 : 2;
            if(p->header_n<p->header_need) continue;
            if(begin_frame(p)) { p->failed=1; return -1; }
        }
        while(at<n && p->frame_n<p->frame_size) {
            if(p->opcode>=8) p->control[p->frame_n]=data[at++];
            else p->message[p->message_n++]=data[at++];
            p->frame_n++;
        }
        if(p->frame_n==p->frame_size) {
            int r=finish_frame(p,event,ctx);
            if(r) { p->failed=1; return r; }
        }
    }
    return 0;
}
int ws_write_client_frame(unsigned op,const unsigned char *data,size_t n,
                          const unsigned char mask[4],ws_write_fn write,void *context) {
    unsigned char header[8],chunk[256]; size_t h=n<126?2:4,at=0,i; int result=-1;
    if(!write || !mask || (!data && n) || n>WS_MESSAGE_MAX ||
       (op!=1 && op!=2 && op!=8 && op!=9 && op!=10) || (op>=8 && n>125)) return -1;
    header[0]=(unsigned char)(0x80|op); header[1]=(unsigned char)(0x80|(n<126?n:126));
    if(n>=126) { header[2]=(unsigned char)(n>>8); header[3]=(unsigned char)n; }
    memcpy(header+h,mask,4);
    if(write(context,header,h+4)) goto done;
    while(at<n) {
        size_t count=n-at; if(count>sizeof(chunk)) count=sizeof(chunk);
        for(i=0;i<count;i++) chunk[i]=data[at+i]^mask[(at+i)%4];
        if(write(context,chunk,count)) goto done;
        at+=count;
    }
    result=0;
done:
    /* Do not retain masked credential-bearing payload bytes on the stack. */
    { volatile unsigned char *p=chunk; for(i=0;i<sizeof(chunk);i++) p[i]=0; }
    return result;
}
static int lower(int c) { return c>='A' && c<='Z'?c+32:c; }
static int equal(const char *a,size_t n,const char *b) {
    size_t i; if(n!=strlen(b)) return 0;
    for(i=0;i<n;i++) if(lower((unsigned char)a[i])!=lower((unsigned char)b[i])) return 0;
    return 1;
}
static int token(const char *a,size_t n,const char *expected) {
    size_t start=0,i=0,end;
    while(i<=n) {
        if(i==n || a[i]==',') {
            end=i; while(start<end && (a[start]==' ' || a[start]=='\t')) start++;
            while(end>start && (a[end-1]==' ' || a[end-1]=='\t')) end--;
            if(equal(a+start,end-start,expected)) return 1;
            start=i+1;
        } i++;
    } return 0;
}
int ws_validate_upgrade(const char *h,size_t n,const char *accept) {
    size_t at=0,end,colon,start; int connection=0,upgrade=0,accepted=0;
    if(n<16 || memcmp(h,"HTTP/1.1 101 ",13)) return 0;
    while(at+1<n && !(h[at]=='\r' && h[at+1]=='\n')) at++;
    at+=2;
    while(at+1<n) {
        end=at; while(end+1<n && !(h[end]=='\r' && h[end+1]=='\n')) end++;
        if(end+1>=n) return 0;
        if(end==at) return end+2==n && connection && upgrade && accepted;
        colon=at; while(colon<end && h[colon]!=':') colon++;
        if(colon==end) return 0;
        start=colon+1; while(start<end && (h[start]==' ' || h[start]=='\t')) start++;
        size_t stop=end; while(stop>start && (h[stop-1]==' ' || h[stop-1]=='\t')) stop--;
        if(equal(h+at,colon-at,"connection")) connection|=token(h+start,stop-start,"upgrade");
        else if(equal(h+at,colon-at,"upgrade")) { if(upgrade || !equal(h+start,stop-start,"websocket")) return 0; upgrade=1; }
        else if(equal(h+at,colon-at,"sec-websocket-accept")) {
            if(accepted || stop-start!=strlen(accept) || memcmp(h+start,accept,stop-start)) return 0;
            accepted=1;
        } else if(equal(h+at,colon-at,"sec-websocket-extensions") || equal(h+at,colon-at,"sec-websocket-protocol")) return 0;
        at=end+2;
    } return 0;
}
