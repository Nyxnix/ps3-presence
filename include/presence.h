#ifndef PRESENCE_H
#define PRESENCE_H
#include <stdint.h>
#include <stddef.h>

enum presence_mode { PRESENCE_UNKNOWN, PRESENCE_XMB, PRESENCE_GAME };
struct observation {
    enum presence_mode mode;
    char title_id[10];
    char title[129];
};
struct presence_session {
    struct observation current, candidate;
    uint32_t candidate_count;
    uint64_t candidate_at, started_at;
    uint64_t generation;
    uint64_t heartbeat_at;
    unsigned cpu_c,rsx_c,temperatures_valid,model;
};
void presence_init(struct presence_session *s);
/* Unknown samples cancel a candidate, but do not invent a game exit. */
int presence_observe(struct presence_session *s, const struct observation *o,
                     uint64_t now_seconds);
/* Returns bytes written excluding NUL, or 0 on invalid input/insufficient space. */
size_t presence_json(const struct presence_session *s, char *out, size_t cap);
int presence_title_id_valid(const char id[10]);
int presence_temperatures(struct presence_session *,int cpu_c,int rsx_c);
unsigned presence_model_id(const char *platform);
const char *presence_model_name(unsigned model);
#endif
