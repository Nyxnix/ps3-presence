#ifndef PRESENCE_MEMORY_OWNER_H
#define PRESENCE_MEMORY_OWNER_H
#include <stdint.h>
struct memory_owner {
    uint32_t address, source, allocations, releases, release_failures;
    int32_t last_release_error;
};
/* An unsuccessful release must retain ownership and prevent replacement. */
int memory_owner_acquire(struct memory_owner *,uint32_t address,uint32_t source);
int memory_owner_release(struct memory_owner *,int32_t (*release)(uint32_t));
#endif
