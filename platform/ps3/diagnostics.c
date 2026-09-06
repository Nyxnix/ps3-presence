#include <stdint.h>
#include <sys/thread.h>
/* Samples live SP only; never writes into unused stack memory. This is a
 * sampled maximum, not a complete call-depth watermark or a fault handler. */
static struct { uintptr_t base; uint32_t size,peak; int32_t error; } stacks[2];
void presence_stack_sample(unsigned worker) {
    uintptr_t sp; sys_ppu_thread_stack_t info;
    if(worker>1) return;
    if(!stacks[worker].size) {
        int32_t r=sysThreadGetStackInformation(&info);
        __atomic_store_n(&stacks[worker].error,r,__ATOMIC_RELAXED);
        if(r) return;
        stacks[worker].base=(uintptr_t)info.addr;
        __atomic_store_n(&stacks[worker].size,info.size,__ATOMIC_RELAXED);
    }
    __asm__ volatile("mr %0,1":"=r"(sp));
    uintptr_t base=stacks[worker].base,top=base+stacks[worker].size;
    if(sp<base || sp>top) { __atomic_store_n(&stacks[worker].error,-1,__ATOMIC_RELAXED); return; }
    uint32_t used=(uint32_t)(top-sp);
    if(used>__atomic_load_n(&stacks[worker].peak,__ATOMIC_RELAXED))
        __atomic_store_n(&stacks[worker].peak,used,__ATOMIC_RELAXED);
}
uint32_t presence_stack_stat(unsigned worker,unsigned stat) {
    if(worker>1) return 0;
    if(stat==0) return __atomic_load_n(&stacks[worker].size,__ATOMIC_RELAXED);
    if(stat==1) return __atomic_load_n(&stacks[worker].peak,__ATOMIC_RELAXED);
    return (uint32_t)__atomic_load_n(&stacks[worker].error,__ATOMIC_RELAXED);
}
