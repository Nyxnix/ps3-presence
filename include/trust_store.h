#ifndef PRESENCE_TRUST_STORE_H
#define PRESENCE_TRUST_STORE_H
#include "mbedtls/x509_crt.h"
/* Anchors remain in immutable module storage for the entire TLS lifetime. */
int presence_load_roots(mbedtls_x509_crt *roots);
#endif
