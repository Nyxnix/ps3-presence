#ifndef PRESENCE_CLOCK_H
#define PRESENCE_CLOCK_H
#include <stddef.h>
#include <stdint.h>
/* Network-worker-owned calibration. Only feed authenticated TLS response headers. */
struct presence_clock { int64_t offset; unsigned valid; };
int presence_clock_calibrate(struct presence_clock *,const char *,size_t,int64_t);
uint64_t presence_clock_start(const struct presence_clock *,uint64_t);
int64_t net_clock_offset(void);
#endif
