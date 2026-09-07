#ifndef PRESENCE_DIAGNOSTICS_H
#define PRESENCE_DIAGNOSTICS_H
#include <stdint.h>
#if PRESENCE_DIAGNOSTICS
void presence_stack_sample(unsigned worker);
uint32_t presence_stack_stat(unsigned worker,unsigned stat);
#else
static inline void presence_stack_sample(unsigned worker) { (void)worker; }
#endif
#endif
