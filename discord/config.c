#include "discord_wire.h"
#include <string.h>
void discord_wipe(void *p,size_t n) { volatile unsigned char *b=p; while(n--) *b++=0; }
static int eq(const char *p,size_t n,const char *expected) { return strlen(expected)==n && !memcmp(p,expected,n); }
int discord_config_parse(struct discord_config *c,const char *bytes,size_t n) {
    size_t at=0,start,end,equal,i,length; unsigned seen=0,bit;
    if(!c) return 0;
    discord_wipe(c,sizeof(*c)); memcpy(c->status,"online",7);
    if(!bytes || !n || n>DISCORD_CONFIG_MAX) goto fail;
    while(at<n) {
        start=at; while(at<n && bytes[at]!='\n') at++; end=at; if(at<n) at++;
        if(end>start && bytes[end-1]=='\r') end--;
        if(start==end || bytes[start]=='#') continue;
        for(i=start;i<end;i++) if((unsigned char)bytes[i]<32 || (unsigned char)bytes[i]>126) goto fail;
        equal=start; while(equal<end && bytes[equal]!='=') equal++;
        if(equal==end) goto fail;
        length=end-equal-1; bit=0;
        if(eq(bytes+start,equal-start,"application_id")) {
            bit=1; if(length<17 || length>20 || bytes[equal+1]=='0') goto fail;
            for(i=equal+1;i<end;i++) if(bytes[i]<'0' || bytes[i]>'9') goto fail;
            memcpy(c->application_id,bytes+equal+1,length);
        } else if(eq(bytes+start,equal-start,"token")) {
            bit=2; if(length>256 || (length && length<20)) goto fail;
            for(i=equal+1;i<end;i++) {
                unsigned char ch=(unsigned char)bytes[i];
                if(!((ch>='a' && ch<='z') || (ch>='A' && ch<='Z') || (ch>='0' && ch<='9') || ch=='.' || ch=='_' || ch=='-')) goto fail;
            }
            memcpy(c->token,bytes+equal+1,length);
        } else if(eq(bytes+start,equal-start,"enabled") || eq(bytes+start,equal-start,"show_xmb")) {
            bit=eq(bytes+start,equal-start,"enabled")?4:8;
            if(length!=1 || (bytes[equal+1]!='0' && bytes[equal+1]!='1')) goto fail;
            if(bit==4) c->enabled=bytes[equal+1]=='1'; else c->show_xmb=bytes[equal+1]=='1';
        } else if(eq(bytes+start,equal-start,"status")) {
            bit=16;
            if(!eq(bytes+equal+1,length,"online") && !eq(bytes+equal+1,length,"idle") && !eq(bytes+equal+1,length,"dnd") && !eq(bytes+equal+1,length,"invisible")) goto fail;
            memset(c->status,0,sizeof(c->status)); memcpy(c->status,bytes+equal+1,length);
        } else if(eq(bytes+start,equal-start,"large_image")) {
            bit=32; if(length>256) goto fail;
            for(i=equal+1;i<end;i++) if(bytes[i]=='"' || bytes[i]=='\\' || bytes[i]==' ') goto fail;
            memcpy(c->large_image,bytes+equal+1,length);
        } else if(eq(bytes+start,equal-start,"small_image")) {
            bit=64; if(length>256) goto fail;
            for(i=equal+1;i<end;i++) if(bytes[i]=='"' || bytes[i]=='\\' || bytes[i]==' ') goto fail;
            memcpy(c->small_image,bytes+equal+1,length);
        } else goto fail;
        if(seen&bit) goto fail;
        seen|=bit;
    }
    if(!(seen&1) || (c->enabled && !c->token[0])) goto fail;
    return 1;
fail:
    discord_wipe(c,sizeof(*c)); return 0;
}
