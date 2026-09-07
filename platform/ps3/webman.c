/* Separate, detector-owned nonblocking socket. No DNS and no mutable VSH
 * game interface calls: only a read-only request to loopback webMAN. */
#include "webman_status.h"
#include "transport.h"
#include <string.h>
extern uint32_t presence_export(const char *,uint32_t);
extern int presence_is_running(void);
#include "diagnostics.h"
extern uint64_t vsh_call(uint32_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
#define V(fn,a,b,c,d,e) vsh_call(fn,(uint64_t)(a),(uint64_t)(b),(uint64_t)(c),(uint64_t)(d),(uint64_t)(e),0,0)
static uint32_t sock_fn,connect_fn,send_fn,recv_fn,close_fn,opt_fn,poll_fn,errno_fn,getopt_fn;
static int fd=-1;
static struct webman_status_stream response;
static int error_number(void) {
    uint32_t p=(uint32_t)V(errno_fn,0,0,0,0,0);
    return p?*(int32_t *)(uintptr_t)p:-1;
}
static int resolve_sockets(void) {
    if(sock_fn) return 1;
    connect_fn=presence_export("sys_net",0x64f66d35); send_fn=presence_export("sys_net",0xdc751b40);
    recv_fn=presence_export("sys_net",0xfba04f37); close_fn=presence_export("sys_net",0x6db6e8cd);
    opt_fn=presence_export("sys_net",0x88f03575); poll_fn=presence_export("sys_net",0x051ee3ee);
    errno_fn=presence_export("sys_net",0x6005cde1); getopt_fn=presence_export("sys_net",0x5a045bd1);
    if(!connect_fn || !send_fn || !recv_fn || !close_fn || !opt_fn || !poll_fn || !errno_fn || !getopt_fn) return 0;
    sock_fn=presence_export("sys_net",0x9c056962); return sock_fn!=0;
}
void presence_webman_close(void) {
    if(fd>=0 && close_fn && !(int32_t)V(close_fn,fd,0,0,0,0)) fd=-1;
}
int presence_webman_detect(struct observation *o) {
    static const char request[]="GET /cpursx.ps3 HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\nAccept-Encoding: identity\r\n\r\n";
    unsigned char address[16]={16,2,0,80,127,0,0,1};
    struct { int32_t fd; int16_t events,revents; } event;
    unsigned char input[256];
    size_t sent=0; int result=-1,r,one=1; uint64_t deadline=net_milliseconds()+3000;
    memset(o,0,sizeof(*o)); presence_stack_sample(0);
    webman_status_init(&response);
    if(!resolve_sockets()) return -100;
    presence_webman_close(); if(fd>=0) return -111; /* Never overwrite an unclosed handle. */
    fd=(int32_t)V(sock_fn,2,1,0,0,0); if(fd<0) return -106;
    if((int32_t)V(opt_fn,fd,0xffff,0x1100,&one,4)<0) goto done;
    r=(int32_t)V(connect_fn,fd,address,sizeof(address),0,0);
    if(r) {
        r=error_number(); if(r!=36 && r!=35 && r!=37) goto done;
        event.fd=fd; event.events=4;
        do {
            event.revents=0; r=(int32_t)V(poll_fn,&event,1,20,0,0);
            if(r<0) goto done;
            if(r>0 && event.revents) {
                int32_t error=0; uint32_t len=4;
                if((int32_t)V(getopt_fn,fd,0xffff,0x1007,&error,&len)<0 || error) goto done;
                break;
            }
        } while(presence_is_running() && net_milliseconds()<deadline);
        if(!event.revents) goto done;
    }
    while(presence_is_running() && net_milliseconds()<deadline) {
        if(sent<sizeof(request)-1) {
            r=(int32_t)V(send_fn,fd,request+sent,sizeof(request)-1-sent,0x80,0);
            if(r>0) { sent+=(size_t)r; continue; }
        } else {
            r=(int32_t)V(recv_fn,fd,input,sizeof(input),0x80,0);
            if(r>0) {
                if(!webman_status_feed(&response,input,(size_t)r)) { result=-3; goto done; }
                continue;
            }
            if(!r) {
                result=webman_status_finish(&response,o)?0:-3; goto done;
            }
        }
        if(!r) goto done;
        r=error_number(); if(r!=35 && r!=4) goto done;
        net_sleep();
    }
done:
    presence_webman_close();
    if(fd>=0) result=-111;
    memset(&response,0,sizeof(response)); memset(input,0,sizeof(input));
    if(result) memset(o,0,sizeof(*o));
    return result;
}
