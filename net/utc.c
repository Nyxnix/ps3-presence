#include "tls_port.h"
#include <string.h>
static int leap(int y) { return y%4==0 && (y%100!=0 || y%400==0); }
struct tm *presence_utc(const int64_t *when,struct tm *out) {
    static const int days_in_month[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int y=1970,m=0; int64_t days,seconds;
    if(!when || !out || *when<0 || *when>253402300799LL) return NULL;
    memset(out,0,sizeof(*out)); days=*when/86400; seconds=*when%86400;
    out->tm_wday=(int)((days+4)%7);
    while(days>=365+leap(y)) days-=365+leap(y++);
    out->tm_yday=(int)days;
    while(days>=days_in_month[m]+(m==1 && leap(y))) { days-=days_in_month[m]+(m==1 && leap(y)); m++; }
    out->tm_year=y-1900; out->tm_mon=m; out->tm_mday=(int)days+1;
    out->tm_hour=(int)(seconds/3600); out->tm_min=(int)(seconds/60%60); out->tm_sec=(int)(seconds%60);
    return out;
}
struct tm *mbedtls_platform_gmtime_r(const int64_t *when,struct tm *out) { return presence_utc(when,out); }
