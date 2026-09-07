#include "settings.h"
#include <ppu-lv2.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* PS3MAPI returns LV2 ENOENT for an empty slot, not libc's ENOENT. */
#define EMPTY_PLUGIN_SLOT 0x80010006u

/* Cobra/PS3MAPI interface documented by webMAN MOD. Slot zero belongs to VSH. */
int app_stop_plugin(void) {
    int32_t result;
    { lv2syscall2(8,0x7777,0x0011); result=(int32_t)p1; }
    if(result<0x0111 || result>0xffff) return 0;
    for(unsigned slot=1;slot<=6;slot++) {
        char name[32]={0},path[512]={0};
        { lv2syscall5(8,0x7777,0x0047,slot,(uint64_t)name,(uint64_t)path); result=(int32_t)p1; }
        name[sizeof(name)-1]=0; path[sizeof(path)-1]=0;
        if((uint32_t)result==EMPTY_PLUGIN_SLOT && !name[0] && !path[0]) continue;
        if(result) return 0;
        const char *base=strrchr(path,'/'); base=base?base+1:path;
        if(strcmp(name,"ps3_presence") && !app_plugin_name(base)) continue;
        { lv2syscall2(8,0x364f,slot); result=(int32_t)p1; }
        if(result) return 0;
        memset(name,0,sizeof(name)); memset(path,0,sizeof(path));
        { lv2syscall5(8,0x7777,0x0047,slot,(uint64_t)name,(uint64_t)path); result=(int32_t)p1; }
        if((result && (uint32_t)result!=EMPTY_PLUGIN_SLOT) || name[0] || path[0]) return 0;
    }
    return 1;
}
/* Only called on APP_DIRECTORY and its children; never follows directory links. */
static int remove_app_directory(const char *directory,unsigned depth) {
    if(depth>16) return 0;
    DIR *dir=opendir(directory);
    if(!dir) return errno==ENOENT;
    int ok=1;
    for(;;) {
        errno=0;
        struct dirent *entry=readdir(dir);
        if(!entry) { if(errno) ok=0; break; }
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
        char path[1024];
        int n=snprintf(path,sizeof(path),"%s/%s",directory,entry->d_name);
        if(n<0 || n>=(int)sizeof(path) || strchr(entry->d_name,'/')) { ok=0; break; }
        if(entry->d_type==DT_DIR) {
            if(!remove_app_directory(path,depth+1)) ok=0;
        } else if(unlink(path) && errno!=ENOENT) ok=0;
    }
    if(closedir(dir)) ok=0;
    if(ok && rmdir(directory) && errno!=ENOENT) ok=0;
    return ok;
}
int app_delete_installation(void) {
    /* The game-data utility cannot remove this running HDD application. */
    return remove_app_directory(APP_DIRECTORY,0);
}
