#include "settings.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
void app_defaults(struct discord_config *c) {
    memset(c,0,sizeof(*c));
    strcpy(c->application_id,"1545898852773527622"); strcpy(c->status,"online");
    strcpy(c->large_image,"1545924328779161670"); strcpy(c->small_image,c->large_image);
    c->show_xmb=1;
}
static int read_file(const char *path,char *buf,size_t cap,size_t *n) {
    FILE *f=fopen(path,"rb"); if(!f) return errno==ENOENT?0:-1;
    *n=fread(buf,1,cap,f); int ok=!ferror(f) && *n<cap; fclose(f);
    if(ok) buf[*n]=0; return ok?1:-1;
}
int app_config_load(const char *path,struct discord_config *c) {
    char buf[DISCORD_CONFIG_MAX+1]; size_t n=0; int r=read_file(path,buf,sizeof(buf),&n);
    if(r==1 && !discord_config_parse(c,buf,n)) r=-1;
    if(!r) app_defaults(c);
    discord_wipe(buf,sizeof(buf)); return r;
}
/* A complete, flushed staging file precedes replacement. CellFs cannot always
 * replace an existing name: retain a rollback copy across that rename pair. */
static int replace_file(const char *path,const void *data,size_t n) {
    char temp[256],backup[256];
    if(snprintf(temp,sizeof(temp),"%s.new",path)>=(int)sizeof(temp) ||
       snprintf(backup,sizeof(backup),"%s.bak",path)>=(int)sizeof(backup)) return 0;
    FILE *f=fopen(temp,"wb"); if(!f) return 0;
    chmod(temp,0600);
    int ok=fwrite(data,1,n,f)==n && !fflush(f) && !fsync(fileno(f));
    if(fclose(f)) ok=0;
    if(!ok) { unlink(temp); return 0; }
    if(!rename(temp,path)) return 1;
    struct stat st;
    if(stat(path,&st)) { unlink(temp); return 0; }
    if(unlink(backup) && errno!=ENOENT) { unlink(temp); return 0; }
    if(rename(path,backup)) { unlink(temp); return 0; }
    if(!rename(temp,path)) return 1;
    rename(backup,path); unlink(temp); return 0;
}
int app_config_save(const char *path,const struct discord_config *c) {
    char buf[DISCORD_CONFIG_MAX+1]; struct discord_config checked;
    int n=snprintf(buf,sizeof(buf),"# PS3 Presence - editable via FTP\nenabled=%u\napplication_id=%s\ntoken=%s\nstatus=%s\nshow_xmb=%u\nlarge_image=%s\nsmall_image=%s\n",c->enabled,c->application_id,c->token,c->status,c->show_xmb,c->large_image,c->small_image);
    int ok=n>0 && n<=(int)DISCORD_CONFIG_MAX && discord_config_parse(&checked,buf,(size_t)n);
    if(ok) ok=replace_file(path,buf,(size_t)n);
    discord_wipe(buf,sizeof(buf)); discord_wipe(&checked,sizeof(checked)); return ok;
}
int app_boot_list(const char *in,size_t n,char *out,size_t cap) {
    size_t at=0,used=0; unsigned entries=0;
    if(memchr(in,0,n)) return 0;
    while(at<n) {
        size_t start=at; while(at<n && in[at]!='\n') at++;
        size_t end=at; if(at<n) at++;
        if(end>start && in[end-1]=='\r') end--;
        size_t left=start,right=end;
        while(left<right && (in[left]==' ' || in[left]=='\t')) left++;
        while(right>left && (in[right-1]==' ' || in[right-1]=='\t')) right--;
        size_t base=right; while(base>left && in[base-1]!='/') base--;
        size_t len=right-base;
        int own=(len==17 && !memcmp(in+base,"ps3_presence.sprx",17)) ||
            (len>27 && !memcmp(in+base,"ps3_presence_combined_",22) && !memcmp(in+right-5,".sprx",5));
        if(own) continue;
        if(left<right && in[left]!='#') entries++;
        if(end-start+1>=cap-used) return 0;
        memcpy(out+used,in+start,end-start); used+=end-start; out[used++]='\n';
    }
    if(entries>=6 || sizeof(APP_PLUGIN)>=cap-used) return 0;
    memcpy(out+used,APP_PLUGIN,sizeof(APP_PLUGIN)-1); used+=sizeof(APP_PLUGIN)-1;
    out[used++]='\n'; out[used]=0; return (int)used;
}
int app_install(char *message,size_t cap) {
    char *blob=0,boot[8192],updated[8192]; size_t n=0; struct discord_config c;
    int ok=0,have=app_config_load(APP_CONFIG,&c);
    const char *error="Could not read configuration. Fix it via FTP first.";
    if(have<0) goto done;
    int r=read_file(APP_BOOT,boot,sizeof(boot),&n);
    error="Could not read Cobra's startup list."; if(r<0) goto done;
    int length=app_boot_list(boot,n,updated,sizeof(updated));
    error="Startup list is full or invalid. Other plugins were kept."; if(!length) goto done;
    FILE *f=fopen(APP_BUNDLED,"rb");
    error="Bundled plugin is missing. Reinstall this package."; if(!f) goto done;
    if(fseek(f,0,SEEK_END)) { fclose(f); goto done; }
    long bytes=ftell(f); rewind(f);
    if(bytes<256 || bytes>1024*1024) { fclose(f); goto done; }
    blob=malloc((size_t)bytes); if(!blob) { fclose(f); goto done; }
    r=fread(blob,1,(size_t)bytes,f)==(size_t)bytes && !ferror(f); fclose(f);
    if(!r || memcmp(blob,"SCE\0",4)) goto done;
    if(mkdir("/dev_hdd0/plugins",0755) && errno!=EEXIST) goto done;
    error="Could not save plugin. Startup list was kept.";
    if(!replace_file(APP_PLUGIN,blob,(size_t)bytes)) goto done;
    error="Could not create configuration. Startup list was kept.";
    if(!have && !app_config_save(APP_CONFIG,&c)) goto done;
    error="Could not update startup list. Previous list was retained.";
    if(!replace_file(APP_BOOT,updated,(size_t)length)) goto done;
    ok=1; error="Installed. Restart your PS3 to activate this build.";
done:
    free(blob); discord_wipe(&c,sizeof(c)); snprintf(message,cap,"%s",error); return ok;
}
