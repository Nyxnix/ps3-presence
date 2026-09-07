#include "websocket.h"
#include <string.h>
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
