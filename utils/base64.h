#ifndef CLIB_BASE64_H_
#define CLIB_BASE64_H_

#include <stdio.h>
#ifdef __cplusplus 
extern "C" {
#endif

ssize_t base64_encode(const void *data, size_t length, char **p_b64);
ssize_t base64_decode(const char *b64, size_t length, unsigned char **p_data);

ssize_t base64url_encode(const void *data, size_t length, char **p_b64);
ssize_t base64url_decode(const char *b64, size_t length, unsigned char **p_data);

#ifdef __cplusplus 
}
#endif
#endif
