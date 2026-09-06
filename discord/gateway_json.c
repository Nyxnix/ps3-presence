#include "gateway_json.h"
#include <string.h>
/* Object: key/end, colon, value, comma/end, key. Array: value/end,
   comma/end, value. A completed parent is marked before entering a child. */
enum { KEY_END,COLON,VALUE,OBJ_END,KEY,ARRAY_VALUE_END,ARRAY_END,ARRAY_VALUE };
enum { K_NONE,K_OP,K_D,K_SEQ,K_TYPE,K_INTERVAL,K_SESSION,K_URL };
static int white(unsigned c) { return c==' ' || c=='\n' || c=='\r' || c=='\t'; }
static unsigned key_id(const char *s) {
    if(!strcmp(s,"op")) return K_OP;
    if(!strcmp(s,"d")) return K_D;
    if(!strcmp(s,"s")) return K_SEQ;
    if(!strcmp(s,"t")) return K_TYPE;
    if(!strcmp(s,"heartbeat_interval")) return K_INTERVAL;
    if(!strcmp(s,"session_id")) return K_SESSION;
    if(!strcmp(s,"resume_gateway_url")) return K_URL;
    return K_NONE;
}
static unsigned selected(struct gateway_json *p) {
    unsigned d=p->depth;
    if(!d) return 0;
    unsigned path=p->stack[d-1].path,key=p->stack[d-1].key;
    if(path==1 && key>=K_OP && key<=K_TYPE) return key;
    if(path==2 && key>=K_INTERVAL) return key;
    return 0;
}
static int value_allowed(struct gateway_json *p) {
    unsigned s;
    if(!p->depth) return !p->root_done;
    s=p->stack[p->depth-1].state;
    return s==VALUE || s==ARRAY_VALUE_END || s==ARRAY_VALUE;
}
static void value_done(struct gateway_json *p) {
    if(!p->depth) p->root_done=1;
    else {
        unsigned d=p->depth-1;
        p->stack[d].state=p->stack[d].state==VALUE?OBJ_END:ARRAY_END;
    }
}
static int append(struct gateway_json *p,unsigned c) {
    if(p->token_n<256 && (p->mode!=1 || p->is_key || p->capture)) p->token[p->token_n]=(char)c;
    p->token_n++;
    if(p->capture && !p->is_key && (p->token_n>256 || c>=128 || c==0)) return -1;
    return 0;
}
static int unsigned_number(const char *s,uint64_t *value) {
    uint64_t n=0; size_t i;
    if(!*s || (*s=='0' && s[1])) return 0;
    for(i=0;s[i];i++) {
        if(s[i]<'0' || s[i]>'9' || n>(UINT64_MAX-(unsigned)(s[i]-'0'))/10) return 0;
        n=n*10+(unsigned)(s[i]-'0');
    }
    *value=n; return 1;
}
static int valid_number(const char *s) {
    size_t i=0;
    if(s[i]=='-') i++;
    if(s[i]=='0') i++;
    else { if(s[i]<'1' || s[i]>'9') return 0; while(s[i]>='0' && s[i]<='9') i++; }
    if(s[i]=='.') { i++; if(s[i]<'0' || s[i]>'9') return 0; while(s[i]>='0' && s[i]<='9') i++; }
    if(s[i]=='e' || s[i]=='E') {
        i++; if(s[i]=='+' || s[i]=='-') i++;
        if(s[i]<'0' || s[i]>'9') return 0;
        while(s[i]>='0' && s[i]<='9') i++;
    }
    return !s[i];
}
static int scalar(struct gateway_json *p,int string) {
    uint64_t number=0; unsigned k=p->capture; char *s=p->token;
    if(p->token_n>256) { if(!p->is_key && (k || !string)) return -1; s[0]=0; }
    else s[p->token_n]=0;
    if(p->is_key) {
        unsigned d=p->depth-1,id=p->token_n<=256 && memchr(s,0,p->token_n)?K_NONE:key_id(s),bit=1u<<id;
        if(p->stack[d].path && id && (p->stack[d].seen&bit)) return -1;
        p->stack[d].seen|=bit; p->stack[d].key=(unsigned char)id; p->stack[d].state=COLON;
        if(p->stack[d].path==1) p->root_seen=p->stack[d].seen;
        return 0;
    }
    if(!string && strcmp(s,"null") && strcmp(s,"true") && strcmp(s,"false") && !valid_number(s)) return -1;
    if(k==K_SEQ && string) return -1;
    if(k==K_OP || k==K_INTERVAL || (k==K_SEQ && strcmp(s,"null"))) {
        if(string || !unsigned_number(s,&number)) return -1;
        if(k==K_OP) { if(number>255) return -1; p->event.opcode=(unsigned)number; }
        if(k==K_INTERVAL) { if(number<1000 || number>300000) return -1; p->event.heartbeat_interval=(unsigned)number; }
        if(k==K_SEQ) { p->event.sequence=number; p->event.has_sequence=1; }
    } else if(k==K_D) {
        p->event.data_kind=string?7:!strcmp(s,"null")?1:!strcmp(s,"false")?2:!strcmp(s,"true")?3:6;
        p->event.can_resume=p->event.data_kind==3;
    } else if(k==K_TYPE || k==K_SESSION || k==K_URL) {
        char *out=k==K_TYPE?p->event.type:k==K_SESSION?p->event.session_id:p->event.resume_url;
        size_t cap=k==K_TYPE?sizeof(p->event.type):k==K_SESSION?sizeof(p->event.session_id):sizeof(p->event.resume_url);
        if(k==K_TYPE && !string && !strcmp(s,"null")) { value_done(p); return 0; }
        if(!string || p->token_n>=cap) return -1;
        memcpy(out,s,p->token_n+1);
    }
    value_done(p); return 0;
}
static int string_byte(struct gateway_json *p,unsigned c) {
    if(p->unicode) {
        unsigned v;
        if(c>='0' && c<='9') v=c-'0'; else if(c>='a' && c<='f') v=c-'a'+10;
        else if(c>='A' && c<='F') v=c-'A'+10; else return -1;
        p->unicode_value=p->unicode_value*16+v;
        if(!--p->unicode) return append(p,p->unicode_value<128?p->unicode_value:128);
        return 0;
    }
    if(p->escape) {
        p->escape=0;
        if(c=='u') { p->unicode=4; p->unicode_value=0; return 0; }
        if(c=='b') c=8; else if(c=='f') c=12; else if(c=='n') c=10;
        else if(c=='r') c=13; else if(c=='t') c=9;
        else if(c!='"' && c!='\\' && c!='/') return -1;
        return append(p,c);
    }
    if(p->utf8_left) {
        if((c&0xc0)!=0x80) return -1;
        p->utf8_value=(p->utf8_value<<6)|(c&63);
        if(!--p->utf8_left && (p->utf8_value<p->utf8_min || p->utf8_value>0x10ffff || (p->utf8_value>=0xd800 && p->utf8_value<=0xdfff))) return -1;
        return append(p,c);
    }
    if(c=='"') { p->mode=0; return scalar(p,1); }
    if(c=='\\') { p->escape=1; return 0; }
    if(c<32) return -1;
    if(c>=128) {
        if(c>=0xc2 && c<=0xdf) { p->utf8_left=1; p->utf8_value=c&31; p->utf8_min=0x80; }
        else if(c>=0xe0 && c<=0xef) { p->utf8_left=2; p->utf8_value=c&15; p->utf8_min=0x800; }
        else if(c>=0xf0 && c<=0xf4) { p->utf8_left=3; p->utf8_value=c&7; p->utf8_min=0x10000; }
        else return -1;
    }
    return append(p,c);
}
void gateway_json_init(struct gateway_json *p) { memset(p,0,sizeof(*p)); p->event.opcode=256; }
int gateway_json_feed(struct gateway_json *p,const unsigned char *data,size_t n) {
    size_t i=0;
    if(p->failed || (!data && n) || n>8*1024*1024-p->total) goto fail;
    p->total+=n;
    while(i<n) {
        unsigned c=data[i],s=p->depth?p->stack[p->depth-1].state:255;
        if(p->mode==1) { if(string_byte(p,c)) goto fail; i++; continue; }
        if(p->mode==2) {
            if(!white(c) && c!=',' && c!=']' && c!='}') {
                if(p->token_n>=256 || append(p,c)) goto fail;
                i++; continue;
            }
            p->mode=0; if(scalar(p,0)) goto fail; continue;
        }
        if(white(c)) { i++; continue; }
        if(c=='}' || c==']') {
            if(!p->depth || (c=='}' && s!=KEY_END && s!=OBJ_END) || (c==']' && s!=ARRAY_VALUE_END && s!=ARRAY_END)) goto fail;
            p->depth--; i++; continue;
        }
        if(c==',') {
            if(s!=OBJ_END && s!=ARRAY_END) goto fail;
            p->stack[p->depth-1].state=s==OBJ_END?KEY:ARRAY_VALUE; i++; continue;
        }
        if(c==':') { if(s!=COLON) goto fail; p->stack[p->depth-1].state=VALUE; i++; continue; }
        if(c=='"') {
            p->is_key=s==KEY_END || s==KEY;
            if(!p->is_key && !value_allowed(p)) goto fail;
            p->capture=p->is_key?0:selected(p); p->token_n=0; p->mode=1; i++; continue;
        }
        if(!value_allowed(p)) goto fail;
        if(c=='{' || c=='[') {
            unsigned path=!p->depth?1:selected(p)==K_D?2:0;
            if(!p->depth && c!='{') goto fail;
            if(p->depth==64) goto fail;
            if(selected(p) && selected(p)!=K_D) goto fail;
            if(selected(p)==K_D) p->event.data_kind=c=='{'?4:5;
            value_done(p); memset(&p->stack[p->depth],0,sizeof(p->stack[0]));
            p->stack[p->depth].state=c=='{'?KEY_END:ARRAY_VALUE_END;
            p->stack[p->depth].path=(unsigned char)(c=='{'?path:0); p->depth++; i++; continue;
        }
        if(!p->depth) goto fail;
        p->is_key=0; p->capture=selected(p); p->token_n=0; p->mode=2;
    }
    return 0;
fail:
    p->failed=1; return -1;
}
int gateway_json_finish(struct gateway_json *p) {
    if(p->failed || p->mode || p->depth || !p->root_done || p->event.opcode>255 || !(p->root_seen&(1u<<K_OP))) return -1;
    return 0;
}
