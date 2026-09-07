#include "settings.h"
#include "screen.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <io/pad.h>
#include <sys/memory.h>
#include <sysutil/sysutil.h>
#include <sysutil/osk.h>
static int running=1,keyboard=0,keyboard_done=0,cancelled=0;
static sys_mem_container_t container;
static uint16_t entered[257],initial[257],prompt[48];
static oskCallbackReturnParam output;
static void callback(uint64_t status,uint64_t param,void *user) {
    (void)param; (void)user;
    if(status==SYSUTIL_EXIT_GAME) { running=0; if(keyboard==1) oskAbort(); }
    if(status==SYSUTIL_OSK_INPUT_CANCELED) { cancelled=1; oskAbort(); }
    if((status==SYSUTIL_OSK_DONE || status==SYSUTIL_OSK_INPUT_CANCELED) && keyboard==1) {
        keyboard=2;
        if(oskUnloadAsync(&output)) { keyboard=0; keyboard_done=-1; }
    }
    if(status==SYSUTIL_OSK_UNLOADED) { keyboard=0; keyboard_done=1; if(cancelled) output.res=OSK_CANCELED; }
}
static int edit_token(const char *value) {
    oskParam param; oskInputFieldInfo input;
    memset(&param,0,sizeof(param)); memset(&input,0,sizeof(input));
    memset(entered,0,sizeof(entered)); memset(initial,0,sizeof(initial)); memset(prompt,0,sizeof(prompt));
    for(unsigned i=0;value[i] && i<256;i++) initial[i]=(unsigned char)value[i];
    const char *label="Discord token"; for(unsigned i=0;label[i];i++) prompt[i]=(unsigned char)label[i];
    input.message=prompt; input.startText=initial; input.maxLength=256;
    param.allowedPanels=OSK_PANEL_TYPE_PASSWORD; param.firstViewPanel=OSK_PANEL_TYPE_PASSWORD;
    param.prohibitFlags=OSK_PROHIBIT_RETURN|OSK_PROHIBIT_SPACE;
    memset(&output,0,sizeof(output)); output.len=256; output.str=entered;
    if(sysMemContainerCreate(&container,4*1024*1024)) return 0;
    oskSetInitialInputDevice(OSK_DEVICE_PAD); oskSetKeyLayoutOption(OSK_FULLKEY_PANEL);
    oskSetLayoutMode(OSK_LAYOUTMODE_HORIZONTAL_ALIGN_CENTER|OSK_LAYOUTMODE_VERTICAL_ALIGN_CENTER);
    if(oskLoadAsync(container,&param,&input)) { sysMemContainerDestroy(container); return 0; }
    cancelled=0; keyboard=1; return 1;
}
static unsigned read_buttons(void) {
    static unsigned held[MAX_PADS];
    padInfo info; padData pad; unsigned buttons=0;
    if(ioPadGetInfo(&info)) return 0;
    for(unsigned i=0;i<MAX_PADS;i++) {
        if(!info.status[i]) { held[i]=0; continue; }
        memset(&pad,0,sizeof(pad));
        if(!ioPadGetData(i,&pad) && pad.len) {
            held[i]=(pad.BTN_UP?1:0)|(pad.BTN_DOWN?2:0)|(pad.BTN_CROSS?4:0)|(pad.BTN_CIRCLE?8:0)|(pad.BTN_L1?16:0);
        }
        buttons|=held[i];
    }
    return buttons;
}
int main(void) {
    struct discord_config config; char message[160]="FTP: " APP_CONFIG;
    unsigned selected=0,previous=0; int install=0,readable,view=APP_SETTINGS;
    app_defaults(&config); readable=app_config_load(APP_CONFIG,&config);
    if(readable<0) strcpy(message,"Configuration is invalid. Repair the file via FTP.");
    if(!screen_init()) { screen_end(); return 1; }
    ioPadInit(7); sysUtilRegisterCallback(0,callback,0);
    /* Sample L1 for one second while the controller service becomes ready. */
    for(unsigned frame=0;frame<60 && running;frame++) {
        unsigned buttons=read_buttons(); if(buttons&16) install=1; previous=buttons;
        screen_draw(selected,config.token[0]!=0,config.enabled,"Starting... hold L1 to install",0);
        sysUtilCheckCallback(); screen_flip();
    }
    if(install && running) {
        view=APP_INSTALLER;
        screen_draw(0,config.token[0]!=0,config.enabled,"Installing...",1); screen_flip();
        int installed=app_install(message,sizeof(message));
        readable=app_config_load(APP_CONFIG,&config);
        if(installed) {
            for(unsigned frame=0;frame<120 && running;frame++) {
                screen_draw(0,0,0,"Restarting your PS3...",APP_RESTARTING);
                sysUtilCheckCallback(); screen_flip();
            }
            if(running) {
                if(app_request_restart()) {
                    /* Service VSH's exit callback while it shuts down normally. */
                    for(unsigned frame=0;frame<600 && running;frame++) {
                        screen_draw(0,0,0,"Restarting your PS3...",APP_RESTARTING);
                        sysUtilCheckCallback(); screen_flip();
                    }
                }
                strcpy(message,"Installed. Automatic restart failed. Restart your PS3 manually.");
            }
        }
    }
    while(running || keyboard) {
        unsigned buttons=read_buttons(),pressed=buttons&~previous; previous=buttons;
        if(keyboard_done) {
            sysMemContainerDestroy(container);
            if(keyboard_done==1 && output.res==OSK_OK) {
                struct discord_config fresh;
                int valid=app_config_load(APP_CONFIG,&fresh)>=0;
                if(valid) {
                    memset(fresh.token,0,sizeof(fresh.token));
                    for(unsigned i=0;i<256 && entered[i];i++) {
                        if(entered[i]>127) { valid=0; break; }
                        fresh.token[i]=(char)entered[i];
                    }
                    if(!fresh.token[0]) fresh.enabled=0;
                }
                if(valid && app_config_save(APP_CONFIG,&fresh)) { config=fresh; readable=1; strcpy(message,"Token saved."); }
                else strcpy(message,"Token was not saved. Check token and FTP configuration.");
                discord_wipe(&fresh,sizeof(fresh));
            } else strcpy(message,"Token edit cancelled.");
            discord_wipe(initial,sizeof(initial)); discord_wipe(entered,sizeof(entered)); keyboard_done=0;
        }
        if(!keyboard && running) {
            if(view==APP_REMOVED) {
                if(pressed&8) running=0;
            } else if(view==APP_CONFIRM_REMOVE) {
                if(pressed&8) { view=APP_SETTINGS; strcpy(message,"Uninstall cancelled."); }
                else if(pressed&4) {
                    screen_draw(selected,0,0,"Removing PS3 Presence...",APP_CONFIRM_REMOVE); screen_flip();
                    int removed=app_uninstall(message,sizeof(message));
                    discord_wipe(&config,sizeof(config));
                    readable=removed?0:app_config_load(APP_CONFIG,&config);
                    view=removed?APP_REMOVED:APP_SETTINGS;
                }
            } else {
                if(pressed&1) selected=(selected+2)%3;
                else if(pressed&2) selected=(selected+1)%3;
                if(pressed&8) running=0;
                else if(pressed&4 && selected==2) {
                    view=APP_CONFIRM_REMOVE;
                    strcpy(message,"This also deletes your saved Discord token.");
                } else if(pressed&4) {
                    if(app_config_load(APP_CONFIG,&config)<0) { readable=-1; strcpy(message,"Invalid configuration. Repair it via FTP first."); }
                    else {
                        readable=1;
                        if(selected==0) {
                            if(!edit_token(config.token)) strcpy(message,"Could not open keyboard. Try again.");
                        } else {
                            config.enabled=!config.enabled;
                            if(config.enabled && !config.token[0]) { config.enabled=0; strcpy(message,"Add your token before turning presence on."); }
                            else if(app_config_save(APP_CONFIG,&config)) strcpy(message,config.enabled?"Presence ON. The running plugin will reconnect.":"Presence OFF. The running plugin will clear activity.");
                            else { app_config_load(APP_CONFIG,&config); strcpy(message,"Could not save configuration."); }
                        }
                    }
                }
            }
        }
        screen_draw(selected,readable>=0 && config.token[0],readable>=0 && config.enabled,message,view);
        sysUtilCheckCallback(); screen_flip();
    }
    sysUtilUnregisterCallback(0); ioPadEnd(); screen_end(); discord_wipe(&config,sizeof(config)); return 0;
}
