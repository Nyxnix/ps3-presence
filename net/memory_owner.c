#include "memory_owner.h"
int memory_owner_acquire(struct memory_owner *m,uint32_t address,uint32_t source) {
    if(m->address || !address) return -1;
    m->address=address; m->source=source; m->allocations++; return 0;
}
int memory_owner_release(struct memory_owner *m,int32_t (*release)(uint32_t)) {
    if(!m->address) return 0;
    int32_t error=release(m->address); m->last_release_error=error;
    if(error) { m->release_failures++; return error; }
    m->address=0; m->source=0; m->releases++; return 0;
}
