#ifndef PRESENCE_APP_SCREEN_H
#define PRESENCE_APP_SCREEN_H
#include <stdint.h>
int screen_init(void);
void screen_draw(unsigned selected,int token_set,int enabled,const char *message,int installer);
void screen_flip(void);
void screen_end(void);
void screen_render(uint32_t *,unsigned,unsigned,unsigned,int,int,const char *,int);
#endif
