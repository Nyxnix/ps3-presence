#include "transport.h"
#include <string.h>
#define JSMN_STATIC
#define JSMN_STRICT
#include "jsmn.h"
static int key(const char *s,const jsmntok_t *t,const char *k) {
    return t->type==JSMN_STRING && (size_t)(t->end-t->start)==strlen(k) && !memcmp(s+t->start,k,strlen(k));
}
static int number(const char *s,const jsmntok_t *t,unsigned *out) {
    unsigned value=0; int i;
    if(t->type!=JSMN_PRIMITIVE || t->end<=t->start || t->end-t->start>6) return 0;
    if(t->end-t->start>1 && s[t->start]=='0') return 0;
    for(i=t->start;i<t->end;i++) { if(s[i]<'0' || s[i]>'9') return 0; value=value*10+(unsigned)(s[i]-'0'); }
    *out=value; return 1;
}
int gateway_is_ack(const unsigned char *bytes,size_t size) {
    const char *s=(const char *)bytes; jsmn_parser p; jsmntok_t t[32]; int n,i,seen=0; unsigned value;
    if(!bytes || size>4096) return 0;
    jsmn_init(&p); n=jsmn_parse(&p,s,size,t,32);
    if(n<1 || t[0].type!=JSMN_OBJECT) return 0;
    for(i=t[0].end;i<(int)size;i++) if(s[i]!=' ' && s[i]!='\t' && s[i]!='\r' && s[i]!='\n') return 0;
    for(i=1;i<n && t[i].start<t[0].end;) {
        int v=i+1; if(v>=n) return 0;
        if(key(s,&t[i],"op")) { if(seen++ || !number(s,&t[v],&value) || value!=11) return 0; }
        i=v+1; while(i<n && t[i].start<t[v].end) i++;
    }
    return seen==1;
}
int gateway_hello(const unsigned char *bytes,size_t size,unsigned *interval) {
    const char *s=(const char *)bytes; jsmn_parser p; jsmntok_t t[64]; int n,i,d=-1,op=-1,seen=0; unsigned value;
    if(!bytes || !interval || size>4096) return 0;
    jsmn_init(&p); n=jsmn_parse(&p,s,size,t,64);
    if(n<1 || t[0].type!=JSMN_OBJECT) return 0;
    for(i=t[0].end;i<(int)size;i++) if(s[i]!=' ' && s[i]!='\t' && s[i]!='\r' && s[i]!='\n') return 0;
    for(i=1;i<n && t[i].start<t[0].end;) {
        int v=i+1; if(v>=n) return 0;
        if(key(s,&t[i],"op")) { if(op!=-1 || !number(s,&t[v],&value) || value!=10) return 0; op=10; }
        if(key(s,&t[i],"d")) { if(d!=-1 || t[v].type!=JSMN_OBJECT) return 0; d=v; }
        i=v+1; while(i<n && t[i].start<t[v].end) i++;
    }
    if(op!=10 || d<0) return 0;
    for(i=d+1;i<n && t[i].start<t[d].end;) {
        int v=i+1; if(v>=n) return 0;
        if(key(s,&t[i],"heartbeat_interval")) {
            if(seen++ || !number(s,&t[v],&value) || value<1000 || value>300000) return 0;
            *interval=value;
        }
        i=v+1; while(i<n && t[i].start<t[v].end) i++;
    }
    return seen==1;
}
