/* VSH detector and native Discord presence for CFW/Cobra. */
#include "presence.h"
#include "webman_status.h"
extern void presence_stack_sample(unsigned);
#include <ppu-lv2.h>
#include <sys/file.h>
#include <sys/thread.h>
#include <sys/systime.h>

#define STATE "/dev_hdd0/tmp/ps3_presence.json"
#define TEMP_STATE "/dev_hdd0/tmp/ps3_presence.json.tmp"
#define DISABLE "/dev_hdd0/tmp/ps3_presence.disable"
#define LOG "/dev_hdd0/tmp/ps3_presence.log"
#define TRACE "/dev_hdd0/tmp/ps3_presence_trace.bin"
#define TRACE_COUNT 256
/* Big-endian, fixed-width records. Single writer (detector), no credentials.
 * Kept as a named ELF symbol for postmortem memory inspection. The file is a
 * periodic snapshot; only a memory/core capture can contain the final events. */
struct trace_record { uint32_t sequence, seconds, stage, a, b; };
volatile struct {
    uint32_t magic, version, capacity, sequence;
    struct trace_record records[TRACE_COUNT];
} presence_trace = {0x50533354,1,TRACE_COUNT,0,{{0}}};
static uint32_t trace_seconds;
static void trace_mark(uint32_t stage,uint32_t a,uint32_t b) {
    uint32_t next=presence_trace.sequence+1;
    volatile struct trace_record *r=&presence_trace.records[(next-1)%TRACE_COUNT];
    r->sequence=0;
    __sync_synchronize();
    r->seconds=trace_seconds; r->stage=stage; r->a=a; r->b=b;
    __sync_synchronize();
    r->sequence=next;
    __sync_synchronize();
    presence_trace.sequence=next;
}
static void trace_publish(void) {
    int32_t fd; uint64_t written=0;
    if(!sysLv2FsOpen(TRACE,SYS_O_WRONLY|SYS_O_CREAT|SYS_O_TRUNC,&fd,0600,0,0)) {
        sysLv2FsWrite(fd,(const void *)&presence_trace,sizeof(presence_trace),&written);
        sysLv2FsClose(fd);
    }
}
static volatile uint32_t running;
static uint64_t worker_id;
static uint64_t network_worker_id;
static uint32_t log_size;
static struct presence_session session;
static volatile uint32_t session_lock;
static void lock_session(void) { while(__sync_lock_test_and_set(&session_lock,1)) { lv2syscall1(141,100); } }
static void unlock_session(void) { __sync_lock_release(&session_lock); }
void net_snapshot(struct presence_session *out) { lock_session(); *out=session; unlock_session(); }
extern uint32_t presence_thread_create_import;
extern uint32_t presence_thread_exit_import;
extern void presence_network_worker(uint64_t);

void *memset(void *p,int c,size_t n) { unsigned char *b=p; while(n--) *b++=(unsigned char)c; return p; }
void *memcpy(void *d,const void *s,size_t n) { unsigned char *a=d; const unsigned char *b=s; while(n--) *a++=*b++; return d; }
static size_t length(const char *s) { size_t n=0; while(s[n]) n++; return n; }
static int bounded_equal(const char *a,const char *b,size_t n) {
    size_t i; for(i=0;i<n;i++) { if(a[i]!=b[i]) return 0; if(!a[i]) return 1; } return 0;
}
static int address_ok(uint32_t p,uint32_t size) {
    return p>=0x10000 && p<0x20000000 && size<=0x20000000-p;
}
static uint32_t read32(uint32_t p) { return *(volatile uint32_t *)(uintptr_t)p; }
static void log_text(const char *s);
static void log_hex(uint32_t value) {
    char out[10]; unsigned i;
    for(i=0;i<8;i++) out[i]="0123456789abcdef"[(value>>(28-i*4))&15];
    out[8]='\n'; out[9]=0; log_text(out);
}

/* Based on the documented VSH export-table layout, with bounded walks.
 * Firmware support is intentionally limited; bounds are not a memory-map proof. */
static uint32_t resolve(const char *library,uint32_t nid) {
    uint32_t table,i;
    if(read32(0x10000)!=0x7f454c46) { log_text("ELF guard: "); log_hex(read32(0x10000)); return 0; }
    table=read32(0x1008c);
    if(!address_ok(table,0x984+4)) return 0;
    table+=0x984;
    for(i=0;i<512;i++,table+=4) {
        uint32_t entry,name,ids,functions,j; uint16_t count;
        if(!address_ok(table,4)) return 0;
        entry=read32(table); if(!entry) break;
        if((entry&3) || !address_ok(entry,0x2c)) { log_text("Export entry rejected: "); log_hex(entry); return 0; }
        name=read32(entry+0x10);
        if(!address_ok(name,64)) { log_text("Export name rejected: "); log_hex(name); return 0; }
        if(!bounded_equal((const char *)(uintptr_t)name,library,64)) continue;
        count=*(volatile uint16_t *)(uintptr_t)(entry+6);
        ids=read32(entry+0x14); functions=read32(entry+0x18);
        if(count>8192 || (ids&3) || (functions&3) || !address_ok(ids,count*4) || !address_ok(functions,count*4)) return 0;
        for(j=0;j<count;j++) if(read32(ids+j*4)==nid) {
            uint32_t opd=read32(functions+j*4);
            if((opd&3) || !address_ok(opd,8) || !address_ok(read32(opd),4) || !address_ok(read32(opd+4),4)) { log_text("OPD rejected: "); log_hex(opd); return 0; }
            return opd;
        }
    }
    return 0;
}
uint32_t presence_export(const char *library,uint32_t nid) { return resolve(library,nid); }
int presence_is_running(void) { return running!=0; }

/* Explicit assembly bridge: VSH uses OPD32, PSL1GHT uses ELFv1 OPD64. */
extern uint64_t vsh_call(uint32_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
#define call vsh_call
void presence_worker_exit(void) { call(presence_thread_exit_import,0,0,0,0,0,0,0); }
static void sleep_us(uint64_t n) { lv2syscall1(141,n); }
static void exit_thread(void) { lv2syscall1(41,0); }
static int disabled(void) {
    int32_t fd; if(sysLv2FsOpen(DISABLE,0,&fd,0,0,0)) return 0;
    sysLv2FsClose(fd); return 1;
}
static void log_text(const char *s) {
    int32_t fd; uint64_t n=0; size_t size=length(s);
    if(log_size+size>65536) return;
    if(!sysLv2FsOpen(LOG,SYS_O_WRONLY|SYS_O_CREAT|SYS_O_APPEND,&fd,0600,0,0)) {
        sysLv2FsWrite(fd,s,size,&n); sysLv2FsClose(fd); log_size+=(uint32_t)n;
    }
}
static void publish_file(void) {
    char json[1024]; int32_t fd; uint64_t written=0;
    trace_publish();
    size_t n=presence_json(&session,json,sizeof(json));
    if(!n || sysLv2FsOpen(TEMP_STATE,SYS_O_WRONLY|SYS_O_CREAT|SYS_O_TRUNC,&fd,0600,0,0)) return;
    int32_t err=sysLv2FsWrite(fd,json,n,&written);
    sysLv2FsClose(fd);
    if(!err && written==n) {
        err=sysLv2FsRename(TEMP_STATE,STATE);
        /* CellFs does not replace an existing destination. Readers retry the
         * brief missing-file window; only replace after a complete temp write. */
        if((uint32_t)err==0x80010014) {
            if(!sysLv2FsUnlink(STATE)) err=sysLv2FsRename(TEMP_STATE,STATE);
        }
        if(err) { log_text("State rename failed: "); log_hex((uint32_t)err); }
    }
}
static void detect(struct observation *o) {
    trace_mark(40,0,0);
    int result=presence_webman_detect(o);
    trace_mark(41,(uint32_t)result,(uint32_t)o->mode);
}
static int sample_temperatures(void) {
    uint32_t cpu=0,rsx=0; int32_t a,b; int changed;
    trace_mark(30,0,0);
    { lv2syscall2(383,0,(uint64_t)&cpu); a=(int32_t)p1; }
    { lv2syscall2(383,1,(uint64_t)&rsx); b=(int32_t)p1; }
    trace_mark(31,(uint32_t)a,(uint32_t)b);
    lock_session(); changed=presence_temperatures(&session,a?-1:(int)(cpu>>24),b?-1:(int)(rsx>>24)); unlock_session();
    return changed;
}
static void worker(uint64_t arg) {
    (void)arg; int32_t fd; unsigned unavailable=0; uint64_t published_at=0,thermal_at=0;
    if(!sysLv2FsOpen(LOG,SYS_O_WRONLY|SYS_O_CREAT|SYS_O_TRUNC,&fd,0600,0,0)) sysLv2FsClose(fd);
    log_text("PS3 Presence 0.4.0: loopback webMAN detector; native Discord client.\n");
    log_text("Detector trace v1 enabled; no direct game_plugin calls.\n");
    trace_mark(1,0,0);
    lock_session(); presence_init(&session);
    { unsigned char info[24]={0}; int32_t result;
      /* Read-only system information: firmware, platform ID, build. No IDPS. */
      { lv2syscall1(387,(uint64_t)info); result=(int32_t)p1; }
      if(!result) session.model=presence_model_id((const char *)info+8);
    }
    unlock_session(); publish_file();
    {
        uint64_t *entry=(uint64_t *)(void *)presence_network_worker;
        uint32_t opd[2]={(uint32_t)entry[0],(uint32_t)entry[1]};
        if((int32_t)call(presence_thread_create_import,(uint64_t)&network_worker_id,(uint64_t)opd,0,1200,32768,1,(uint64_t)"ps3_presence_net")) {
            network_worker_id=0; log_text("Network worker creation failed.\n");
        }
    }
    while(running && !disabled()) {
        presence_stack_sample(0);
        struct observation o; uint64_t seconds=0,nanoseconds=0;
        int thermal_changed=0;
        { lv2syscall2(145,(uint64_t)&seconds,(uint64_t)&nanoseconds); }
        trace_seconds=(uint32_t)seconds;
        if(!thermal_at || seconds<thermal_at || seconds-thermal_at>=30) { thermal_changed=sample_temperatures(); thermal_at=seconds; }
        detect(&o);
        trace_mark(21,(uint32_t)o.mode,0);
        lock_session();
        int changed=0;
        if(o.mode==PRESENCE_UNKNOWN) {
            if(unavailable<12) unavailable++;
            if(unavailable==12 && session.current.mode!=PRESENCE_UNKNOWN) {
                memset(&session.current,0,sizeof(session.current));
                session.started_at=0; session.generation++; changed=1;
            }
        } else unavailable=0;
        changed|=presence_observe(&session,&o,seconds); unlock_session();
        trace_mark(22,(uint32_t)changed,0);
        if(changed) {
            publish_file(); published_at=seconds; log_text("State changed: "); log_text(o.title_id); log_text(" "); log_text(o.title); log_text("\n");
        }
        else if(thermal_changed || seconds<published_at || seconds-published_at>=30) { publish_file(); published_at=seconds; }
        sleep_us(5000000);
    }
    presence_webman_close();
    log_text("Detector stopped.\n");
    trace_mark(99,0,0);
    lock_session(); presence_init(&session); unlock_session(); publish_file();
    running=0; presence_worker_exit();
}
int module_start(uint64_t args,void *argp) {
    uint32_t creator; uint64_t *entry=(uint64_t *)(void *)worker;
    uint32_t opd[2]={(uint32_t)entry[0],(uint32_t)entry[1]}; int32_t result;
    (void)args; (void)argp;
    log_text("Module start entered.\n");
    if(disabled()) { exit_thread(); return 0; }
    creator=presence_thread_create_import;
    if(!creator) { log_text("Thread creation export unresolved.\n"); exit_thread(); return 0; }
    running=1;
    result=(int32_t)call(creator,(uint64_t)&worker_id,(uint64_t)opd,0,1100,16384,1,(uint64_t)"ps3_presence");
    if(result) { log_text("Thread creation failed.\n"); running=0; worker_id=0; }
    /* Cobra VSH loader convention: exit its start thread through the syscall. */
    exit_thread(); return 0;
}
int module_stop(uint64_t args,void *argp) {
    uint64_t result=0,info[5]={0x28,2,0,0,0}; int32_t module_id;
    (void)args; (void)argp;
    running=0;
    if(worker_id) sysThreadJoin(worker_id,&result);
    if(network_worker_id) sysThreadJoin(network_worker_id,&result);
    { lv2syscall1(461,(uint64_t)module_stop); module_id=(int32_t)p1; }
    if(module_id>=0) { lv2syscall3(482,module_id,0,(uint64_t)info); }
    exit_thread(); return 0;
}
