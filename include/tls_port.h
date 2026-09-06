#ifndef PRESENCE_TLS_PORT_H
#define PRESENCE_TLS_PORT_H
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#define PRESENCE_TLS_ARENA_SIZE (64u*1024u)
void presence_arena_init(void *buffer);
void *presence_calloc(size_t count,size_t size);
void presence_free(void *p);
size_t presence_arena_peak(void);
size_t presence_arena_used(void);
int64_t presence_time(int64_t *out);
struct tm *presence_utc(const int64_t *when,struct tm *out);
#endif
