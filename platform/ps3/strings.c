/* Freestanding libc subset used by the configured Mbed TLS build. */
#include <stddef.h>
#include <stdint.h>
size_t strlen(const char *s) { size_t n=0; while(s[n]) n++; return n; }
void *memchr(const void *s,int c,size_t n) { const unsigned char *p=s; while(n--) { if(*p==(unsigned char)c) return (void *)p; p++; } return NULL; }
int memcmp(const void *a,const void *b,size_t n) { const unsigned char *x=a,*y=b; while(n--) { if(*x!=*y) return *x-*y; x++; y++; } return 0; }
int strcmp(const char *a,const char *b) { while(*a && *a==*b) { a++; b++; } return (unsigned char)*a-(unsigned char)*b; }
int strncmp(const char *a,const char *b,size_t n) { while(n--) { if(*a!=*b || !*a) return (unsigned char)*a-(unsigned char)*b; a++; b++; } return 0; }
char *strchr(const char *s,int c) { do { if(*s==(char)c) return (char *)s; } while(*s++); return NULL; }
char *strstr(const char *s,const char *t) { size_t n=strlen(t); if(!n) return (char *)s; for(;*s;s++) if(!strncmp(s,t,n)) return (char *)s; return NULL; }
void *memmove(void *d,const void *s,size_t n) { unsigned char *a=d; const unsigned char *b=s; if((uintptr_t)a<(uintptr_t)b) { while(n--) *a++=*b++; } else { while(n) { n--; a[n]=b[n]; } } return d; }
/* Human-readable certificate/ASN.1 formatting is deliberately unavailable;
 * the probe uses structured numeric status and never calls those helpers. */
int snprintf(char *s,size_t n,const char *format,...) { (void)format; if(n) s[0]=0; return -1; }
