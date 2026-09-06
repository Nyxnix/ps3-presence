#include "artwork_session.h"
#include <string.h>
static int current(const struct artwork_session *a,const struct presence_session *p) {
    return p->current.mode==PRESENCE_GAME && a->started_at==p->started_at &&
        !memcmp(a->title_id,p->current.title_id,sizeof(a->title_id));
}
void artwork_session_configure(struct artwork_session *a,const struct discord_config *c) {
    if(!memcmp(a->application_id,c->application_id,sizeof(a->application_id))) return;
    uint64_t pause=a->paused_until;
    memset(a,0,sizeof(*a)); a->paused_until=pause;
    memcpy(a->application_id,c->application_id,sizeof(a->application_id));
}
int artwork_session_pending(struct artwork_session *a,const struct presence_session *p,uint64_t now) {
    int game=p->current.mode==PRESENCE_GAME && presence_title_id_valid(p->current.title_id);
    if(!game || !current(a,p)) {
        memset(a->title_id,0,sizeof(a->title_id)); memset(a->asset,0,sizeof(a->asset));
        a->started_at=0; a->retry_at=0; a->attempted=0;
        if(game) { memcpy(a->title_id,p->current.title_id,sizeof(a->title_id)); a->started_at=p->started_at; }
    }
    return game && now>=a->paused_until &&
        (!a->attempted || (!a->asset[0] && now>=a->retry_at));
}
void artwork_session_result(struct artwork_session *a,int result,uint64_t now) {
    a->attempted=1;
    if(result>0 && a->asset[0]) { a->retry_at=0; return; }
    memset(a->asset,0,sizeof(a->asset));
    a->retry_at=now+(result==-429?86400:900);
    if(result==-429) a->paused_until=a->retry_at;
}
void artwork_session_apply(const struct artwork_session *a,struct discord_config *c,const struct presence_session *p) {
    if(current(a,p) && a->asset[0]) memcpy(c->large_image,a->asset,sizeof(c->large_image));
}
