/* Native VSH socket ABI: pointers in hostent are 32-bit, unlike PPU C pointers. */
#include "transport.h"
#include "tls_port.h"
#include "memory_owner.h"
#include "diagnostics.h"
#include "artwork.h"
#include "artwork_session.h"
#include "presence_clock.h"
#include "mbedtls/ssl.h"
#include <ppu-lv2.h>
#include <sys/file.h>
#include <sys/memory.h>
#include <string.h>
extern uint32_t presence_export(const char *,uint32_t);
extern int presence_is_running(void);
extern void presence_worker_exit(void);
extern uint64_t vsh_call(uint32_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
#define V(fn,a,b,c,d,e) vsh_call(fn,(uint64_t)(a),(uint64_t)(b),(uint64_t)(c),(uint64_t)(d),(uint64_t)(e),0,0)
static uint32_t sock_fn,connect_fn,send_fn,recv_fn,close_fn,dns_fn,opt_fn,poll_fn,errno_fn,getopt_fn;
static int socket_id=-1;
static struct memory_owner arena_owner;
static uint32_t close_failures;
static int32_t close_error;
static int teardown_io;
static struct artwork_session artwork;
int net_artwork_pending(const struct presence_session *p) {
    return artwork_session_pending(&artwork,p,(uint64_t)presence_time(0));
}
void net_artwork_apply(struct discord_config *config,const struct presence_session *p) {
    artwork_session_apply(&artwork,config,p);
}
static void fetch_artwork(const struct discord_config *config,const struct presence_session *p) {
    net_report("artwork_lookup",0,0,presence_arena_peak(),0,0);
    int result=artwork_fetch(config,p->current.title_id,artwork.asset);
    artwork_session_result(&artwork,result,(uint64_t)presence_time(0));
}
static int errnum(void) {
    uint32_t addr=(uint32_t)V(errno_fn,0,0,0,0,0);
    return addr?*(int32_t *)(uintptr_t)addr:-1;
}
int64_t presence_time(int64_t *out) {
    uint64_t sec=0,nsec=0; int64_t result;
    { lv2syscall2(145,(uint64_t)&sec,(uint64_t)&nsec); result=p1?-1:(int64_t)sec; }
    if(out) *out=result;
    return result;
}
uint64_t net_milliseconds(void) {
    static uint64_t cached_frequency;
    uint64_t ticks,frequency=__atomic_load_n(&cached_frequency,__ATOMIC_RELAXED);
    /* Syscall 147 returns the frequency, not elapsed time. Read the PPU
       timebase and divide without overflowing a long-running console. */
    if(!frequency) { lv2syscall0(147); frequency=p1; __atomic_store_n(&cached_frequency,frequency,__ATOMIC_RELAXED); }
    __asm__ volatile("mftb %0" : "=r"(ticks));
    return frequency?(ticks/frequency)*1000+(ticks%frequency)*1000/frequency:0;
}
void net_sleep(void) { lv2syscall1(141,10000); }
int net_cancelled(void) { return !presence_is_running() && !teardown_io; }
void net_teardown_io(int enabled) { teardown_io=enabled; }
int net_entropy(void *unused,unsigned char *data,size_t size) {
    (void)unused;
    while(size) {
        size_t n=size>64?64:size; int32_t r;
        { lv2syscall3(865,2,(uint64_t)data,n); r=(int32_t)p1; }
        if(r) return -1;
        data+=n; size-=n;
    } return 0;
}
int net_open(const char *host) {
    uint32_t ent,list,address; int one=1,r; uint64_t deadline;
    presence_stack_sample(1);
    if(socket_id>=0) { net_close(); if(socket_id>=0) return -111; }
    unsigned char sa[16]={16,2,1,187}; /* sockaddr_in: length, AF_INET, port 443 */
    struct { int32_t fd; int16_t events,revents; } pollfd;
    sock_fn=presence_export("sys_net",0x9c056962); connect_fn=presence_export("sys_net",0x64f66d35);
    send_fn=presence_export("sys_net",0xdc751b40); recv_fn=presence_export("sys_net",0xfba04f37);
    close_fn=presence_export("sys_net",0x6db6e8cd); dns_fn=presence_export("sys_net",0x71f4c717);
    opt_fn=presence_export("sys_net",0x88f03575); poll_fn=presence_export("sys_net",0x051ee3ee);
    errno_fn=presence_export("sys_net",0x6005cde1); getopt_fn=presence_export("sys_net",0x5a045bd1);
    if(!sock_fn || !connect_fn || !send_fn || !recv_fn || !close_fn || !dns_fn || !opt_fn || !poll_fn || !errno_fn || !getopt_fn) return -100;
    /* Existing system resolver: does not initialize/reset global networking.
     * Resolver timeout is system-managed; subsequent I/O has our own deadlines. */
    ent=(uint32_t)V(dns_fn,host,0,0,0,0); if(!ent) return -101;
    if(*(int32_t *)(uintptr_t)(ent+8)!=2 || *(int32_t *)(uintptr_t)(ent+12)!=4) return -102;
    list=*(uint32_t *)(uintptr_t)(ent+16); if(!list) return -103;
    address=*(uint32_t *)(uintptr_t)list; if(!address) return -104;
    memcpy(sa+4,(const void *)(uintptr_t)address,4);
    if(net_cancelled()) return -105;
    socket_id=(int32_t)V(sock_fn,2,1,0,0,0); if(socket_id<0) return -106;
    if((int32_t)V(opt_fn,socket_id,0xffff,0x1100,&one,4)<0) return -107;
    r=(int32_t)V(connect_fn,socket_id,sa,sizeof(sa),0,0);
    if(!r) return 0;
    r=errnum(); if(r!=36 && r!=35 && r!=37) return -200-r;
    deadline=net_milliseconds()+15000; pollfd.fd=socket_id; pollfd.events=4;
    while(!net_cancelled() && net_milliseconds()<deadline) {
        pollfd.revents=0; r=(int32_t)V(poll_fn,&pollfd,1,50,0,0);
        if(r<0) return -108;
        if(r>0 && pollfd.revents) {
            int32_t error=0; uint32_t len=4;
            if((int32_t)V(getopt_fn,socket_id,0xffff,0x1007,&error,&len)<0) return -109;
            return error?-300-error:0;
        }
    } return -110;
}
int net_send(void *unused,const unsigned char *data,size_t size) {
    (void)unused; presence_stack_sample(1); if(net_cancelled()) return -1;
    int r=(int32_t)V(send_fn,socket_id,data,size,0x80,0);
    if(r>=0) return r;
    r=errnum(); return r==35 || r==4?MBEDTLS_ERR_SSL_WANT_WRITE:-1;
}
int net_recv(void *unused,unsigned char *data,size_t size) {
    (void)unused; presence_stack_sample(1); if(net_cancelled()) return -1;
    int r=(int32_t)V(recv_fn,socket_id,data,size,0x80,0);
    if(r>=0) return r;
    r=errnum(); return r==35 || r==4?MBEDTLS_ERR_SSL_WANT_READ:-1;
}
void net_close(void) {
    if(socket_id<0 || !close_fn) return;
    close_error=(int32_t)V(close_fn,socket_id,0,0,0,0);
    if(!close_error) socket_id=-1;
    else close_failures++;
}
/* These cold report helpers otherwise get duplicated at every field at -O2. */
#if PRESENCE_DIAGNOSTICS
static __attribute__((noinline)) void append(char *out,size_t *n,const char *s) { while(*s && *n<1534) out[(*n)++]=*s++; }
static __attribute__((noinline)) void number(char *out,size_t *n,uint64_t value) { char b[21]; unsigned i=0; do { b[i++]=(char)('0'+value%10); value/=10; } while(value); while(i && *n<1534) out[(*n)++]=b[--i]; }
void net_report(const char *stage,int error,uint32_t verify,size_t peak,uint64_t elapsed,unsigned heartbeat) {
    char json[1536]; size_t n=0; int32_t fd; uint64_t written=0;
    append(json,&n,"{\"schema\":1,\"build\":\"0.4.0\",\"stage\":\""); append(json,&n,stage);
    append(json,&n,"\",\"error\":"); if(error<0) append(json,&n,"-"); number(json,&n,error<0?-(int64_t)error:error);
    append(json,&n,",\"verify_flags\":"); number(json,&n,verify);
    append(json,&n,",\"arena_capacity\":"); number(json,&n,PRESENCE_TLS_ARENA_SIZE);
    append(json,&n,",\"arena_peak\":"); number(json,&n,peak);
    append(json,&n,",\"arena_used\":"); number(json,&n,presence_arena_used());
    presence_stack_sample(1);
    append(json,&n,",\"arena_reserved\":"); number(json,&n,arena_owner.address?PRESENCE_TLS_ARENA_SIZE:0);
    append(json,&n,",\"allocation_source\":"); number(json,&n,arena_owner.source);
    append(json,&n,",\"allocations\":"); number(json,&n,arena_owner.allocations);
    append(json,&n,",\"releases\":"); number(json,&n,arena_owner.releases);
    append(json,&n,",\"release_failures\":"); number(json,&n,arena_owner.release_failures);
    append(json,&n,",\"release_error_code\":"); number(json,&n,(uint32_t)arena_owner.last_release_error);
    append(json,&n,",\"socket_close_failures\":"); number(json,&n,close_failures);
    append(json,&n,",\"socket_close_error_code\":"); number(json,&n,(uint32_t)close_error);
    append(json,&n,",\"detector_stack_size\":"); number(json,&n,presence_stack_stat(0,0));
    append(json,&n,",\"detector_stack_sampled_peak\":"); number(json,&n,presence_stack_stat(0,1));
    append(json,&n,",\"detector_stack_error_code\":"); number(json,&n,presence_stack_stat(0,2));
    append(json,&n,",\"network_stack_size\":"); number(json,&n,presence_stack_stat(1,0));
    append(json,&n,",\"network_stack_sampled_peak\":"); number(json,&n,presence_stack_stat(1,1));
    append(json,&n,",\"network_stack_error_code\":"); number(json,&n,presence_stack_stat(1,2));
    append(json,&n,",\"elapsed_ms\":"); number(json,&n,elapsed);
    append(json,&n,",\"heartbeat_interval\":"); number(json,&n,heartbeat);
    int64_t offset=net_clock_offset();
    append(json,&n,",\"clock_offset_seconds\":"); if(offset<0) append(json,&n,"-"); number(json,&n,offset<0?(uint64_t)-offset:(uint64_t)offset);
    append(json,&n,",\"checked_at\":"); number(json,&n,(uint64_t)presence_time(0)); append(json,&n,"}\n");
    if(!sysLv2FsOpen("/dev_hdd0/tmp/ps3_presence_net.json.tmp",SYS_O_WRONLY|SYS_O_CREAT|SYS_O_TRUNC,&fd,0600,0,0)) {
        int err=sysLv2FsWrite(fd,json,n,&written); sysLv2FsClose(fd);
        if(!err && written==n) {
            err=sysLv2FsRename("/dev_hdd0/tmp/ps3_presence_net.json.tmp","/dev_hdd0/tmp/ps3_presence_net.json");
            if((uint32_t)err==0x80010014 && !sysLv2FsUnlink("/dev_hdd0/tmp/ps3_presence_net.json")) sysLv2FsRename("/dev_hdd0/tmp/ps3_presence_net.json.tmp","/dev_hdd0/tmp/ps3_presence_net.json");
        }
    }
}
#endif
static int32_t release_pages(uint32_t address) { return sysMemoryFree(address); }
static int free_arena(void) {
    if(!arena_owner.address) return 0;
    int32_t error=memory_owner_release(&arena_owner,release_pages);
    if(!error) presence_arena_init(0);
    net_report(error?"release_failed":"arena_released",error,0,presence_arena_peak(),0,0);
    return error;
}
static int allocate_arena(void) {
    int32_t allocation; uint32_t container_fn,container=0,address=0,source=4;
    /* Failed frees retain ownership. No replacement allocation until released. */
    if(arena_owner.address && free_arena()) return -112;
    container_fn=presence_export("vsh",0xe7c34044);
    if(container_fn) container=(uint32_t)V(container_fn,4,0,0,0,0);
    allocation=container?sysMemAllocateFromContainer(PRESENCE_TLS_ARENA_SIZE,container,SYS_MEMORY_PAGE_SIZE_64K,&address):-1;
    if(allocation) { source=0; allocation=sysMemoryAllocate(PRESENCE_TLS_ARENA_SIZE,SYS_MEMORY_PAGE_SIZE_64K,&address); }
    if(!allocation) {
        memory_owner_acquire(&arena_owner,address,source);
        presence_arena_init((void *)(uintptr_t)address);
        net_report("arena_allocated",0,0,0,0,0); return 0;
    }
    net_report("allocation_failed",allocation,0,0,0,0); return allocation;
}
static int read_config(struct discord_config *config) {
    char bytes[DISCORD_CONFIG_MAX+1]; uint64_t count=0; int32_t fd; int result=0;
    discord_wipe(config,sizeof(*config));
    if(!sysLv2FsOpen("/dev_hdd0/tmp/ps3_presence.conf",SYS_O_RDONLY,&fd,0,0,0)) {
        int error=sysLv2FsRead(fd,bytes,sizeof(bytes),&count); sysLv2FsClose(fd);
        if(!error && count<=DISCORD_CONFIG_MAX) result=discord_config_parse(config,bytes,(size_t)count);
    }
    discord_wipe(bytes,sizeof(bytes)); return result;
}
int net_config_changed(const struct discord_config *config) {
    struct discord_config fresh; int changed=!read_config(&fresh) || memcmp(config,&fresh,sizeof(fresh));
    discord_wipe(&fresh,sizeof(fresh)); return changed;
}
void presence_network_worker(uint64_t arg) {
    struct discord_client client; struct discord_config config,last;
    unsigned have_last=0,waiting=0,blocked=0;
    (void)arg; presence_stack_sample(1); discord_client_init(&client); memset(&last,0,sizeof(last));
    while(!net_cancelled()) {
        int valid=read_config(&config);
        if(!valid || !config.enabled) {
            if(!waiting) { net_report("waiting_config",0,0,0,0,0); waiting=1; }
            have_last=0; discord_wipe(&last,sizeof(last));
        } else {
            waiting=0;
            artwork_session_configure(&artwork,&config);
            if(!have_last || memcmp(&last,&config,sizeof(config))) {
                discord_client_init(&client); last=config; have_last=1; blocked=0;
            }
            if(client.gateway.phase==GW_BLOCKED) {
                if(!blocked) { net_report("auth_blocked",-(int)client.close_code,0,0,0,0); blocked=1; }
            } else if(gateway_poll(&client.gateway,net_milliseconds())==GW_CONNECT) {
                if(!allocate_arena()) {
                    struct presence_session latest; net_snapshot(&latest);
                    if(net_artwork_pending(&latest)) fetch_artwork(&config,&latest);
                    if(!net_cancelled()) transport_client(&client,&config);
                    free_arena();
                }
                else gateway_disconnected(&client.gateway,0,net_milliseconds(),0);
            }
        }
        discord_wipe(&config,sizeof(config));
        uint64_t next=net_milliseconds()+1000;
        while(!net_cancelled() && net_milliseconds()<next) net_sleep();
    }
    for(unsigned retry=0;retry<3 && arena_owner.address;retry++) { if(!free_arena()) break; net_sleep(); }
    net_close();
    net_report(arena_owner.address?"stopped_release_failed":"stopped",arena_owner.last_release_error,0,presence_arena_peak(),0,0);
    discord_wipe(&config,sizeof(config)); discord_wipe(&last,sizeof(last)); discord_wipe(&client,sizeof(client));
    presence_worker_exit();
}
