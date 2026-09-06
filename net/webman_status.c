#include "webman_status.h"
#include <string.h>
/* Parse only webMAN's current-game heading, never its mounted-image links. */
static int valid_utf8(const char *out,size_t n) {
    unsigned left=0,value=0,min=0;
    for(size_t i=0;i<n;i++) {
        unsigned c=(unsigned char)out[i];
        if(left) {
            if((c&0xc0)!=0x80) return 0;
            value=(value<<6)|(c&63);
            if(!--left && (value<min || value>0x10ffff || (value>=0xd800 && value<=0xdfff))) return 0;
        } else if(c>=128) {
            if(c>=0xc2 && c<=0xdf) { left=1; value=c&31; min=0x80; }
            else if(c>=0xe0 && c<=0xef) { left=2; value=c&15; min=0x800; }
            else if(c>=0xf0 && c<=0xf4) { left=3; value=c&7; min=0x10000; }
            else return 0;
        }
    }
    return n!=0 && !left;
}
static int title_put(struct webman_status_stream *s,unsigned char c) {
    if(c<32 || c==127 || s->title_n==128) return 0;
    s->value.title[s->title_n++]=(char)c; return 1;
}
static int title_byte(struct webman_status_stream *s,unsigned char c) {
    static const char *const names[]={"&amp;","&quot;","&apos;","&lt;","&gt;"};
    static const char values[]="&\"'<>";
    if(!s->entity_n && c!='&') return title_put(s,c);
    s->entity[s->entity_n++]=(char)c;
    for(unsigned i=0;i<5;i++) {
        size_t n=strlen(names[i]);
        if(s->entity_n<=n && !memcmp(s->entity,names[i],s->entity_n)) {
            if(s->entity_n<n) return 1;
            s->entity_n=0; return title_put(s,(unsigned char)values[i]);
        }
    }
    /* Preserve literal ampersands, including a fresh entity after a bad prefix. */
    unsigned keep=c=='&';
    for(unsigned i=0;i<s->entity_n-keep;i++)
        if(!title_put(s,(unsigned char)s->entity[i])) return 0;
    s->entity_n=(unsigned char)keep; s->entity[0]='&'; return 1;
}

#define BAD 1u
#define BODY 2u
#define HTML_END 4u
#define GAME 8u
#define HEADING_END 16u
#define VALID_GAME 32u
#define XMB 64u
#define CPU 128u
#define RSX 256u
/* Prefix fallback preserves matches even when a repeated prefix crosses reads. */
static int token(unsigned char *state,const char *pattern,unsigned char c) {
    unsigned n=*state;
    while(n && (unsigned char)pattern[n]!=c) {
        unsigned next=n-1;
        while(next && memcmp(pattern,pattern+n-next,next)) next--;
        n=next;
    }
    if((unsigned char)pattern[n]==c) n++;
    *state=(unsigned char)n; return !pattern[n];
}
static int prefix_ci(const char *s,size_t n,const char *prefix) {
    for(size_t i=0;prefix[i];i++) {
        if(i>=n) return 0;
        unsigned char c=(unsigned char)s[i]; if(c>='A' && c<='Z') c+=32;
        if(c!=(unsigned char)prefix[i]) return 0;
    }
    return 1;
}
static int header_ok(const char *s,size_t n) {
    if(prefix_ci(s,n,"transfer-encoding:")) return 0;
    if(prefix_ci(s,n,"content-encoding:")) {
        size_t at=17;
        while(at<n && (s[at]==' ' || s[at]=='\t')) at++;
        while(n>at && (s[n-1]==' ' || s[n-1]=='\t' || s[n-1]=='\r')) n--;
        return n-at==8 && prefix_ci(s+at,8,"identity");
    }
    return 1;
}
void webman_status_init(struct webman_status_stream *s) { memset(s,0,sizeof(*s)); }
int webman_status_feed(struct webman_status_stream *s,const unsigned char *p,size_t n) {
    static const char status[]="HTTP/1.0 200 ";
    static const char game[]="<H2><a href=\"https://a0.ww.np.dl.playstation.net/tpl/np/";
    if(s->flags&BAD || (!p && n) || n>WEBMAN_STATUS_MAX_BYTES-s->total) { s->flags|=BAD; return 0; }
    for(size_t i=0;i<n;i++) {
        unsigned char c=p[i]; size_t pos=s->total++;
        if(!c || (pos<sizeof(status)-1 && (pos==7?(c!='0' && c!='1'):c!=(unsigned char)status[pos]))) goto bad;
        if(!(s->flags&BODY)) {
            if(c=='\n') {
                if(!header_ok(s->header,s->header_n)) goto bad;
                s->header_n=0;
            } else if(s->header_n<sizeof(s->header)) s->header[s->header_n++]=(char)c;
            if(token(&s->header_end,"\r\n\r\n",c)) s->flags|=BODY;
            continue;
        }
        if(token(&s->html_match,"</html>",c)) s->flags|=HTML_END;
        if(token(&s->xmb_match,"(XMB)",c)) s->flags|=XMB;
        if(token(&s->cpu_match,"CPU:",c)) s->flags|=CPU;
        if(token(&s->rsx_match,"RSX:",c)) s->flags|=RSX;
        if(!(s->flags&GAME)) {
            if(token(&s->game_match,game,c)) { s->flags|=GAME; s->stage=1; }
            continue;
        }
        if(s->flags&HEADING_END) continue;
        if(token(&s->heading_end,"</H2>",c)) {
            if(s->stage!=4) goto bad;
            s->flags|=HEADING_END; continue;
        }
        if(s->stage==1) {
            if(s->id_n<9) s->value.title_id[s->id_n++]=(char)c;
            else {
                if(c!='/' || !presence_title_id_valid(s->value.title_id)) goto bad;
                s->stage=2;
            }
        } else if(s->stage==2) {
            if(token(&s->search_match,"href=\"http://google.com/search?q=",c)) s->stage=3;
        } else if(s->stage==3) {
            if(c=='"') {
                for(unsigned j=0;j<s->entity_n;j++)
                    if(!title_put(s,(unsigned char)s->entity[j])) goto bad;
                s->entity_n=0;
                if(!valid_utf8(s->value.title,s->title_n)) goto bad;
                s->flags|=VALID_GAME; s->stage=4;
            } else {
                if(!title_byte(s,c)) goto bad;
            }
        }
    }
    return 1;
bad:
    s->flags|=BAD; return 0;
}
int webman_status_finish(struct webman_status_stream *s,struct observation *o) {
    memset(o,0,sizeof(*o));
    if(s->flags&BAD || (s->flags&(BODY|HTML_END))!=(BODY|HTML_END)) return 0;
    if(s->flags&GAME) {
        if((s->flags&(VALID_GAME|HEADING_END))!=(VALID_GAME|HEADING_END)) return 0;
        *o=s->value; o->mode=PRESENCE_GAME; return 1;
    }
    if((s->flags&(XMB|CPU|RSX))==(XMB|CPU|RSX)) {
        o->mode=PRESENCE_XMB; memcpy(o->title,"XMB",4); return 1;
    }
    return 0;
}
