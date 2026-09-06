#ifndef PRESENCE_ARTWORK_SESSION_H
#define PRESENCE_ARTWORK_SESSION_H
#include "discord_wire.h"
/* Only the current game's external asset identifier; no images or history. */
struct artwork_session {
    char application_id[21],title_id[10],asset[257];
    uint64_t started_at,retry_at,paused_until;
    unsigned attempted;
};
void artwork_session_configure(struct artwork_session *,const struct discord_config *);
int artwork_session_pending(struct artwork_session *,const struct presence_session *,uint64_t now);
void artwork_session_result(struct artwork_session *,int result,uint64_t now);
void artwork_session_apply(const struct artwork_session *,struct discord_config *,const struct presence_session *);
#endif
