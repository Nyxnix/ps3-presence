#include "settings.h"
#include <net/net.h>
#include <string.h>

static int ready(int fd,short events,unsigned *remaining) {
    struct pollfd p={fd,events,0};
    while(*remaining) {
        --*remaining;
        int r=netPoll(&p,1,20);
        if(r>0) return 1;
        if(r<0 && net_errno!=NET_EINTR) return 0;
    }
    return 0;
}
int app_request_restart(void) {
    /* Let VSH perform an orderly restart through the existing webMAN service. */
    static const char request[]="GET /restart.ps3?vsh HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    struct sockaddr_in address;
    char response[12];
    int ok=0,one=1,fd;
    unsigned remaining=150;
    size_t at=0;
    if(netInitialize()) return 0;
    fd=netSocket(AF_INET,SOCK_STREAM,0);
    if(fd<0) goto done;
    if(netSetSockOpt(fd,SOL_SOCKET,SO_NBIO,&one,sizeof(one))) goto done;
    memset(&address,0,sizeof(address));
    address.sin_len=sizeof(address); address.sin_family=AF_INET;
    address.sin_port=htons(80); address.sin_addr.s_addr=htonl(0x7f000001);
    if(netConnect(fd,(struct sockaddr *)&address,sizeof(address))) {
        if(net_errno!=NET_EINPROGRESS && net_errno!=NET_EWOULDBLOCK) goto done;
        int error=0; socklen_t len=sizeof(error);
        if(!ready(fd,POLLOUT,&remaining) ||
           netGetSockOpt(fd,SOL_SOCKET,SO_ERROR,&error,&len) || error) goto done;
    }
    while(at<sizeof(request)-1) {
        if(!ready(fd,POLLOUT,&remaining)) goto done;
        ssize_t n=netSend(fd,request+at,sizeof(request)-1-at,0);
        if(n>0) at+=(size_t)n;
        else if(!n || (net_errno!=NET_EWOULDBLOCK && net_errno!=NET_EINTR)) goto done;
    }
    at=0;
    while(at<sizeof(response)) {
        if(!ready(fd,POLLIN,&remaining)) goto done;
        ssize_t n=netRecv(fd,response+at,sizeof(response)-at,0);
        if(n>0) at+=(size_t)n;
        else if(!n || (net_errno!=NET_EWOULDBLOCK && net_errno!=NET_EINTR)) goto done;
    }
    ok=(!memcmp(response,"HTTP/1.0 200",12) || !memcmp(response,"HTTP/1.1 200",12));
done:
    if(fd>=0) netClose(fd);
    netDeinitialize();
    return ok;
}
