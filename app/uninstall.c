#include "settings.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Only files in this application's namespace, in its two shared directories. */
static int remove_files(const char *directory) {
    DIR *dir=opendir(directory);
    if(!dir) return errno==ENOENT;
    int ok=1; struct dirent *entry;
    for(;;) {
        errno=0;
        entry=readdir(dir);
        if(!entry) { if(errno) ok=0; break; }
        const char *name=entry->d_name;
        if(strncmp(name,"ps3_presence",12) || (name[12]!='.' && name[12]!='_')) continue;
        char path[512];
        int n=snprintf(path,sizeof(path),"%s/%s",directory,name);
        if(n<0 || n>=(int)sizeof(path)) { ok=0; break; }
        /* Unlink removes the entry itself; it cannot recurse or follow a link. */
        if(unlink(path) && errno!=ENOENT) ok=0;
    }
    if(closedir(dir)) ok=0;
    return ok;
}
int app_uninstall(char *message,size_t cap) {
    const char *error="Could not stop the plugin. Enable Cobra syscalls and retry.";
    if(!app_stop_plugin()) goto failed;
    error="Could not update startup entries. App retained; check boot_plugins.txt.";
    if(!app_remove_startup()) goto failed;
    error="Some plugin or configuration files could not be removed. Retry uninstall.";
    int plugins=remove_files("/dev_hdd0/plugins");
    int data=remove_files("/dev_hdd0/tmp");
    if(!plugins || !data) goto failed;
    error="Plugin and settings removed. App deletion failed; retry or delete it on XMB.";
    if(!app_delete_installation()) goto failed;
    snprintf(message,cap,"PS3 Presence removed. Press Circle to return to XMB.");
    return 1;
failed:
    snprintf(message,cap,"%s",error); return 0;
}
