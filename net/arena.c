/* A bounded allocator owned exclusively by the network worker. */
#include "tls_port.h"
#include <string.h>
#define UNIT 32u
#define UNITS (PRESENCE_TLS_ARENA_SIZE/UNIT)
static unsigned char *arena;
static unsigned char occupied[(UNITS+7)/8];
static size_t used,peak;
struct header { size_t units; uintptr_t cookie; };
static int is_occupied(size_t i) { return occupied[i/8] & (1u<<(i%8)); }
static void mark(size_t start,size_t n,int busy) {
    for(size_t i=start;i<start+n;i++) {
        unsigned char mask=(unsigned char)(1u<<(i%8));
        if(busy) occupied[i/8]|=mask; else occupied[i/8]&=(unsigned char)~mask;
    }
}
void presence_arena_init(void *buffer) {
    arena=buffer; used=peak=0; memset(occupied,0,sizeof(occupied));
}
void *presence_calloc(size_t n,size_t size) {
    size_t bytes,need,start=0,run=0,i;
#ifdef __powerpc64__
    extern void presence_stack_sample(unsigned);
    presence_stack_sample(1);
#endif
    if(!arena || !n || !size || size>SIZE_MAX/n) return NULL;
    bytes=n*size;
    if(bytes>PRESENCE_TLS_ARENA_SIZE-UNIT) return NULL;
    need=(bytes+UNIT-1)/UNIT+1;
    for(i=0;i<UNITS;i++) {
        if(is_occupied(i)) { run=0; continue; }
        if(!run) start=i;
        if(++run==need) {
            struct header *h=(struct header *)(arena+start*UNIT);
            mark(start,need,1); h->units=need;
            h->cookie=(uintptr_t)h^0x70533370u;
            used+=need*UNIT; if(used>peak) peak=used;
            void *p=(unsigned char *)h+UNIT; memset(p,0,bytes); return p;
        }
    }
    return NULL;
}
void presence_free(void *p) {
    uintptr_t ptr=(uintptr_t)p,base=(uintptr_t)arena; size_t at,n;
    if(!p || !arena || ptr<base+UNIT || ptr>=base+PRESENCE_TLS_ARENA_SIZE || (ptr-base)%UNIT) return;
    struct header *h=(struct header *)(ptr-UNIT);
    if(h->cookie!=((uintptr_t)h^0x70533370u)) return;
    at=((uintptr_t)h-base)/UNIT; n=h->units;
    if(!n || n>UNITS-at || used<n*UNIT) return;
    memset(h,0,n*UNIT); mark(at,n,0); used-=n*UNIT;
}
size_t presence_arena_peak(void) { return peak; }
size_t presence_arena_used(void) { return used; }
