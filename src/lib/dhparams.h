#ifndef DHPARAMS_H
#define DHPARAMS_H 1

#include <openssl/dh.h>

#ifdef __cplusplus
extern "C" {
#endif

    DH* get_dh1024(void);
    DH* get_dh2048(void);
    DH* get_dh4096(void);

#ifdef __cplusplus
}
#endif

#endif /* dhparams.h */
