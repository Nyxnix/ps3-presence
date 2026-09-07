#ifndef PRESENCE_APP_SETTINGS_H
#define PRESENCE_APP_SETTINGS_H
#include "discord_wire.h"
#define APP_ID "PS3RPC001"
#define APP_CONTENT_ID "UP0001-PS3RPC001_00-PS3PRESENCE000001"
#define APP_CONFIG "/dev_hdd0/tmp/ps3_presence.conf"
#define APP_DIRECTORY "/dev_hdd0/game/" APP_ID
#define APP_PLUGIN "/dev_hdd0/plugins/ps3_presence.sprx"
#define APP_BUNDLED "/dev_hdd0/game/" APP_ID "/USRDIR/ps3_presence.sprx"
#define APP_BOOT "/dev_hdd0/boot_plugins.txt"
void app_defaults(struct discord_config *);
int app_config_load(const char *,struct discord_config *);
int app_config_save(const char *,const struct discord_config *);
int app_boot_list(const char *,size_t,char *,size_t);
int app_boot_remove(const char *,size_t,char *,size_t);
int app_plugin_name(const char *);
int app_remove_startup(void);
int app_install(char *,size_t);
int app_uninstall(char *,size_t);
int app_stop_plugin(void);
int app_delete_installation(void);
int app_request_restart(void);
#endif
