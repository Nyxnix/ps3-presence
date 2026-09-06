#include "artwork.h"
#include "gateway_json.h"
#include <string.h>
#define JSMN_STATIC
#define JSMN_STRICT
#include "../net/jsmn.h"
int artwork_url(const char id[10],char url[128]) {
    const char *region="US",*prefix="https://art.gametdb.com/ps3/cover/"; size_t n;
    if(!presence_title_id_valid(id)) return 0;
    if(id[2]=='E') region="EN"; else if(id[2]=='J') region="JA";
    else if(id[2]=='K') region="KO"; else if(id[2]=='A') region="ZH";
    n=strlen(prefix); memcpy(url,prefix,n); memcpy(url+n,region,2); n+=2;
    url[n++]='/'; memcpy(url+n,id,9); n+=9; memcpy(url+n,".jpg",5); return 1;
}
int artwork_parse(const unsigned char *json,size_t size,char asset[257]) {
    struct gateway_json check; jsmn_parser parser; jsmntok_t tokens[64]; int n,i; const char *s=(const char *)json;
    const char *prefix="{\"op\":0,\"d\":"; asset[0]=0;
    if(!json || size>4096) return 0;
    /* Reuse the streaming validator for strict JSON punctuation/literals. */
    gateway_json_init(&check);
    if(gateway_json_feed(&check,(const unsigned char *)prefix,strlen(prefix)) || gateway_json_feed(&check,json,size) || gateway_json_feed(&check,(const unsigned char *)"}",1) || gateway_json_finish(&check)) return 0;
    jsmn_init(&parser); n=jsmn_parse(&parser,s,size,tokens,64);
    if(n<2 || tokens[0].type!=JSMN_ARRAY || tokens[0].size!=1 || tokens[1].type!=JSMN_OBJECT) return 0;
    for(i=2;i+1<n && tokens[i].start<tokens[1].end;) {
        int v=i+1;
        if(tokens[i].type==JSMN_STRING && tokens[i].end-tokens[i].start==19 && !memcmp(s+tokens[i].start,"external_asset_path",19)) {
            size_t at=3,j; if(asset[0] || tokens[v].type!=JSMN_STRING) return 0;
            memcpy(asset,"mp:",3);
            for(j=(size_t)tokens[v].start;j<(size_t)tokens[v].end;j++) {
                unsigned char c=(unsigned char)s[j];
                if(c=='\\' && j+1<(size_t)tokens[v].end && s[j+1]=='/') { c='/'; j++; }
                if(at>=256 || !((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='/' || c=='.' || c==':')) { asset[0]=0; return 0; }
                asset[at++]=(char)c;
            }
            asset[at]=0;
            if(strncmp(asset,"mp:external/",12)) { asset[0]=0; return 0; }
        }
        i=v+1; while(i<n && tokens[i].start<tokens[v].end) i++;
    }
    return asset[0]!=0;
}
static int match(const unsigned char *s,size_t n,const char *key) {
    size_t i; if(n!=strlen(key)) return 0;
    for(i=0;i<n;i++) { unsigned c=s[i]; if(c>='A' && c<='Z') c+=32; if(c!=(unsigned char)key[i]) return 0; }
    return 1;
}
int artwork_http(unsigned char *s,size_t size,unsigned char **body,size_t *body_size) {
    size_t at=0,end,colon,v,length=0; int has_length=0,chunked=0,status,headers_done=0;
    if(size<16 || memcmp(s,"HTTP/1.1 ",9) || s[9]<'1' || s[9]>'5' || s[10]<'0' || s[10]>'9' || s[11]<'0' || s[11]>'9' || s[12]!=' ') return -1;
    status=(s[9]-'0')*100+(s[10]-'0')*10+s[11]-'0';
    while(at+1<size && !(s[at]=='\r' && s[at+1]=='\n')) at++;
    at+=2;
    while(at+1<size) {
        end=at; while(end+1<size && !(s[end]=='\r' && s[end+1]=='\n')) end++;
        if(end+1>=size) return -1;
        if(end==at) { at=end+2; headers_done=1; break; }
        colon=at; while(colon<end && s[colon]!=':') colon++;
        if(colon==end) return -1;
        v=colon+1; while(v<end && (s[v]==' ' || s[v]=='\t')) v++;
        size_t trimmed=end; while(trimmed>v && (s[trimmed-1]==' ' || s[trimmed-1]=='\t')) trimmed--;
        if(match(s+at,colon-at,"content-length")) {
            if(has_length++ || v==trimmed) return -1;
            for(;v<trimmed;v++) { if(s[v]<'0' || s[v]>'9' || length>8192/10) return -1; length=length*10+s[v]-'0'; }
        } else if(match(s+at,colon-at,"transfer-encoding")) {
            if(chunked || !match(s+v,trimmed-v,"chunked")) return -1;
            chunked=1;
        } else if(match(s+at,colon-at,"content-encoding") && !match(s+v,trimmed-v,"identity")) return -1;
        at=end+2;
    }
    if(!headers_done || at>size || (has_length && chunked)) return -1;
    *body=s+at;
    if(chunked) {
        size_t written=0;
        for(;;) {
            size_t chunk=0,digits=0;
            while(at<size && s[at]!='\r') {
                unsigned c=s[at++],x;
                if(c>='0' && c<='9') x=c-'0'; else if(c>='a' && c<='f') x=c-'a'+10;
                else if(c>='A' && c<='F') x=c-'A'+10; else return -1;
                if(++digits>6 || chunk>8192/16) return -1;
                chunk=chunk*16+x;
            }
            if(!digits || at+2>size || s[at]!='\r' || s[at+1]!='\n') return -1;
            at+=2;
            if(!chunk) {
                /* Trailer-free responses are expected; reject unhandled trailers. */
                if(at+2!=size || s[at]!='\r' || s[at+1]!='\n') return -1;
                *body_size=written; return status;
            }
            if(chunk>size-at || at+chunk+2>size || s[at+chunk]!='\r' || s[at+chunk+1]!='\n') return -1;
            memmove(*body+written,s+at,chunk); written+=chunk; at+=chunk+2;
        }
    }
    if(has_length && length!=size-at) return -1;
    *body_size=size-at; return status;
}
