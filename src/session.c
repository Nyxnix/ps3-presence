#include "presence.h"

static void zero(void *p, size_t n) { unsigned char *b=p; while(n--) *b++=0; }
static int equal(const char *a, const char *b, size_t n) {
    size_t i; for(i=0;i<n;i++) { if(a[i]!=b[i]) return 0; if(!a[i]) return 1; }
    return 1;
}
static int same(const struct observation *a, const struct observation *b) {
    return a->mode == b->mode &&
        (a->mode != PRESENCE_GAME || equal(a->title_id,b->title_id,10));
}
int presence_title_id_valid(const char id[10]) {
    unsigned i; if(id[9]) return 0;
    for(i=0;i<9;i++) if(!((id[i]>='A' && id[i]<='Z') || (id[i]>='0' && id[i]<='9'))) return 0;
    return 1;
}
void presence_init(struct presence_session *s) { zero(s,sizeof(*s)); }
/* Confirmed platform revisions only; ambiguous/unknown hardware stays "PS3". */
unsigned presence_model_id(const char *platform) {
    static const char ids[][7]={"Cok14","CokB10","CokC12","CokD10","CokE10","CokF10","CokG11","CokH11","CokJ13","CokJ20","CokK10","CokM10","CokM20","CokM30"};
    static const unsigned char models[]={1,2,3,4,5,6,7,8,9,9,10,11,11,11};
    for(unsigned i=0;i<sizeof(models);i++) if(equal(platform,ids[i],7)) return models[i];
    return 0;
}
const char *presence_model_name(unsigned model) {
    static const char *const names[]={"PS3","PS3 (CECHA/B)","PS3 (CECHC/E)","PS3 (CECHG)","PS3 (CECHH)","PS3 (CECHJ/K)","PS3 (CECHL/M/P/Q)","PS3 (CECH-20xx)","PS3 (CECH-21xx)","PS3 (CECH-25xx)","PS3 (CECH-30xx)","PS3 (CECH-40xx)"};
    return names[model<sizeof(names)/sizeof(*names)?model:0];
}
int presence_temperatures(struct presence_session *s,int cpu,int rsx) {
    unsigned valid=cpu>0 && cpu<128 && rsx>0 && rsx<128;
    unsigned c=valid?(unsigned)cpu:0,r=valid?(unsigned)rsx:0;
    if(s->temperatures_valid==valid && s->cpu_c==c && s->rsx_c==r) return 0;
    s->temperatures_valid=valid; s->cpu_c=c; s->rsx_c=r; s->generation++; return 1;
}
int presence_observe(struct presence_session *s,const struct observation *o,uint64_t now) {
    s->heartbeat_at=now;
    if((o->mode!=PRESENCE_XMB && o->mode!=PRESENCE_GAME) || (o->mode==PRESENCE_GAME && !presence_title_id_valid(o->title_id))) {
        s->candidate_count=0; return 0;
    }
    if(same(&s->current,o)) {
        s->candidate_count=0;
        /* A translated/corrected title does not restart the timer. */
        if(!equal(s->current.title,o->title,sizeof(o->title))) {
            s->current=*o; s->generation++; return 1;
        }
        return 0;
    }
    if(!s->candidate_count || !same(&s->candidate,o)) {
        s->candidate=*o; s->candidate_count=1; s->candidate_at=now; return 0;
    }
    s->current=*o;
    s->started_at=s->candidate_at;
    s->candidate_count=0; s->generation++;
    return 1;
}

struct writer { char *p; size_t n,cap; int failed; };
static void ch(struct writer *w,char c) {
    if(w->n+1>=w->cap) { w->failed=1; return; }
    w->p[w->n++]=c;
}
static void text(struct writer *w,const char *p) { while(*p) ch(w,*p++); }
static void number(struct writer *w,uint64_t n) {
    char b[21]; size_t i=0; do { b[i++]=(char)('0'+n%10); n/=10; } while(n);
    while(i) ch(w,b[--i]);
}
static void string(struct writer *w,const char *p,size_t limit) {
    static const char hex[]="0123456789abcdef";
    size_t i; ch(w,'"');
    for(i=0;i<limit && p[i];i++) {
        unsigned char c=(unsigned char)p[i];
        if(c=='"' || c=='\\') { ch(w,'\\'); ch(w,(char)c); }
        else if(c<32) { text(w,"\\u00"); ch(w,hex[c>>4]); ch(w,hex[c&15]); }
        else ch(w,(char)c);
    }
    ch(w,'"');
}
size_t presence_json(const struct presence_session *s,char *out,size_t cap) {
    struct writer w={out,0,cap,0};
    if(!out || !cap || !s) return 0;
    out[0]=0;
    text(&w,"{\"schema\":1,\"mode\":");
    text(&w,s->current.mode==PRESENCE_GAME?"\"game\"":s->current.mode==PRESENCE_XMB?"\"xmb\"":"\"unknown\"");
    text(&w,",\"title_id\":"); string(&w,s->current.title_id,10);
    text(&w,",\"title\":"); string(&w,s->current.title,129);
    text(&w,",\"started_at\":"); number(&w,s->started_at);
    text(&w,",\"generation\":"); number(&w,s->generation);
    text(&w,",\"heartbeat_at\":"); number(&w,s->heartbeat_at);
    text(&w,",\"cpu_c\":"); if(s->temperatures_valid) number(&w,s->cpu_c); else text(&w,"null");
    text(&w,",\"rsx_c\":"); if(s->temperatures_valid) number(&w,s->rsx_c); else text(&w,"null");
    text(&w,"}\n");
    if(w.failed) { out[0]=0; return 0; }
    out[w.n]=0; return w.n;
}
