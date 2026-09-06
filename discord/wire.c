#include "discord_wire.h"
#include <string.h>
struct writer { char *out; size_t n,cap; int failed; };
static void ch(struct writer *w,char c) { if(w->n+1>=w->cap) w->failed=1; else w->out[w->n++]=c; }
/* Keep one bounded copy loop instead of expanding it for every JSON field. */
static __attribute__((noinline)) void text(struct writer *w,const char *s) { while(*s) ch(w,*s++); }
static void number(struct writer *w,uint64_t value) {
    char b[21]; size_t n=0; do { b[n++]=(char)('0'+value%10); value/=10; } while(value);
    while(n) ch(w,b[--n]);
}
static void string(struct writer *w,const char *s,size_t cap) {
    const char *hex="0123456789abcdef"; size_t i; ch(w,'"');
    for(i=0;i<cap && s[i];i++) {
        unsigned char c=(unsigned char)s[i];
        if(c=='"' || c=='\\') { ch(w,'\\'); ch(w,(char)c); }
        else if(c<32) { text(w,"\\u00"); ch(w,hex[c>>4]); ch(w,hex[c&15]); }
        else ch(w,(char)c);
    }
    if(i==cap) w->failed=1;
    ch(w,'"');
}
static size_t finish(struct writer *w) {
    if(w->failed) { discord_wipe(w->out,w->cap); return 0; }
    w->out[w->n]=0; return w->n;
}
size_t discord_identify(const struct discord_config *c,char *out,size_t cap) {
    struct writer w={out,0,cap,0}; if(!out || !cap || !c || !c->enabled || !c->token[0]) return 0;
    text(&w,"{\"op\":2,\"d\":{\"token\":"); string(&w,c->token,sizeof(c->token));
    text(&w,",\"properties\":{\"os\":\"Linux\",\"browser\":\"PS3 Presence\",\"device\":\"PS3\"},\"compress\":false,\"presence\":{\"status\":");
    string(&w,c->status,sizeof(c->status));
    text(&w,",\"since\":0,\"activities\":[],\"afk\":false}}}"); return finish(&w);
}
size_t discord_resume(const struct discord_config *c,const char *session_id,uint64_t sequence,char *out,size_t cap) {
    struct writer w={out,0,cap,0}; if(!out || !cap || !c || !session_id || !*session_id || !c->enabled || !c->token[0]) return 0;
    text(&w,"{\"op\":6,\"d\":{\"token\":"); string(&w,c->token,sizeof(c->token));
    text(&w,",\"session_id\":"); string(&w,session_id,129); text(&w,",\"seq\":"); number(&w,sequence);
    text(&w,"}}"); return finish(&w);
}
size_t discord_heartbeat(int has_sequence,uint64_t sequence,char *out,size_t cap) {
    struct writer w={out,0,cap,0}; if(!out || !cap) return 0;
    text(&w,"{\"op\":1,\"d\":"); if(has_sequence) number(&w,sequence); else text(&w,"null"); text(&w,"}"); return finish(&w);
}
size_t discord_activity(const struct discord_config *c,const struct presence_session *s,char *out,size_t cap) {
    struct writer w={out,0,cap,0}; if(!out || !cap || !c || !s) return 0;
    text(&w,"{\"op\":3,\"d\":{\"since\":0,\"activities\":[");
    if(s->current.mode==PRESENCE_GAME || (s->current.mode==PRESENCE_XMB && c->show_xmb)) {
        text(&w,"{\"application_id\":"); string(&w,c->application_id,sizeof(c->application_id));
        text(&w,",\"name\":"); string(&w,s->current.mode==PRESENCE_GAME?s->current.title:"Playstation 3 XMB",sizeof(s->current.title));
        text(&w,",\"type\":0,\"details\":"); string(&w,presence_model_name(s->model),32);
        text(&w,",\"state\":\"CPU: ");
        if(s->temperatures_valid) { number(&w,s->cpu_c); text(&w,"\\u00b0C"); } else text(&w,"--");
        text(&w," | RSX: ");
        if(s->temperatures_valid) { number(&w,s->rsx_c); text(&w,"\\u00b0C"); } else text(&w,"--");
        ch(&w,'"');
        if(s->started_at && s->started_at<=UINT64_MAX/1000) {
            text(&w,",\"timestamps\":{\"start\":"); number(&w,s->started_at*1000); ch(&w,'}');
        }
        if(c->large_image[0]) {
            text(&w,",\"assets\":{\"large_image\":"); string(&w,c->large_image,sizeof(c->large_image));
            text(&w,",\"large_text\":"); string(&w,s->current.mode==PRESENCE_GAME?s->current.title_id:"PlayStation 3",s->current.mode==PRESENCE_GAME?sizeof(s->current.title_id):14);
            if(c->small_image[0] && s->current.mode==PRESENCE_GAME) {
                text(&w,",\"small_image\":"); string(&w,c->small_image,sizeof(c->small_image));
                text(&w,",\"small_text\":\"PS3 Game\"");
            }
            ch(&w,'}');
        }
        ch(&w,'}');
    }
    text(&w,"],\"status\":"); string(&w,c->status,sizeof(c->status)); text(&w,",\"afk\":false}}"); return finish(&w);
}
