#include "presence_clock.h"
#include <string.h>
static int leap(int y) { return y%4==0 && (y%100!=0 || y%400==0); }
static int digits(const char *s,unsigned n) {
    int value=0; unsigned i;
    for(i=0;i<n;i++) { if(s[i]<'0' || s[i]>'9') return -1; value=value*10+s[i]-'0'; }
    return value;
}
static int64_t http_date(const char *s,size_t n) {
    static const char months[]="JanFebMarAprMayJunJulAugSepOctNovDec";
    static const char weekdays[]="SunMonTueWedThuFriSat";
    static const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int day,month,year,hour,minute,second,y,m; int64_t days=0;
    if(n!=29 || s[3]!=',' || s[4]!=' ' || s[7]!=' ' || s[11]!=' ' || s[16]!=' ' || s[19]!=':' || s[22]!=':' || memcmp(s+25," GMT",4)) return -1;
    day=digits(s+5,2); year=digits(s+12,4); hour=digits(s+17,2); minute=digits(s+20,2); second=digits(s+23,2);
    for(month=0;month<12 && memcmp(s+8,months+month*3,3);month++) {}
    if(year<2020 || year>2100 || month==12 || day<1 || day>lengths[month]+(month==1 && leap(year)) || hour<0 || hour>23 || minute<0 || minute>59 || second<0 || second>59) return -1;
    for(y=1970;y<year;y++) days+=365+leap(y);
    for(m=0;m<month;m++) days+=lengths[m]+(m==1 && leap(year));
    days+=day-1;
    if(memcmp(s,weekdays+((days+4)%7)*3,3)) return -1;
    return days*86400+hour*3600+minute*60+second;
}
int presence_clock_calibrate(struct presence_clock *clock,const char *headers,size_t n,int64_t local) {
    size_t at=0; int found=0; int64_t server=-1;
    if(clock->valid) return 1; /* Keep a stable correction across reconnects. */
    if(!headers || n>4096 || local<=0) return 0;
    while(at+1<n) {
        size_t end=at,v,trim;
        while(end+1<n && !(headers[end]=='\r' && headers[end+1]=='\n')) end++;
        if(end+1>=n) return 0;
        if(end==at) break;
        if(end-at>=5 && (headers[at]=='D' || headers[at]=='d') && (headers[at+1]=='A' || headers[at+1]=='a') && (headers[at+2]=='T' || headers[at+2]=='t') && (headers[at+3]=='E' || headers[at+3]=='e') && headers[at+4]==':') {
            if(found++) return 0;
            v=at+5; trim=end;
            while(v<trim && (headers[v]==' ' || headers[v]=='\t')) v++;
            while(trim>v && (headers[trim-1]==' ' || headers[trim-1]=='\t')) trim--;
            server=http_date(headers+v,trim-v);
        }
        at=end+2;
    }
    if(!found || server<0 || local<server-86400 || local>server+86400) return 0;
    clock->offset=server-local; clock->valid=1; return 1;
}
uint64_t presence_clock_start(const struct presence_clock *clock,uint64_t start) {
    if(!start || !clock->valid) return start;
    if(clock->offset<0) { uint64_t correction=(uint64_t)(-clock->offset); return start>correction?start-correction:0; }
    return start<=UINT64_MAX-(uint64_t)clock->offset?start+(uint64_t)clock->offset:0;
}
