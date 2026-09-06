/* One shared read-only copy for Gateway TLS and artwork HTTPS. */
#include "trust_store.h"
#include "roots.h"
int presence_load_roots(mbedtls_x509_crt *roots) {
    for(size_t i=0;i<sizeof(presence_root_spans)/sizeof(presence_root_spans[0]);i++) {
        int r=mbedtls_x509_crt_parse_der_nocopy(roots,presence_root_spans[i].bytes,presence_root_spans[i].size);
        if(r) return r;
    }
    return 0;
}
