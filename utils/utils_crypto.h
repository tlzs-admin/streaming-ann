#ifndef UTILS_CRYPTO_H_
#define UTILS_CRYPTO_H_

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

struct sha256_context
{
	void *handle;
};
int sha256_init(struct sha256_context *sha);
int sha256_update(struct sha256_context *sha, const void *data, size_t length);
int sha256_final(struct sha256_context *sha, unsigned char digest[static 32]);
int sha256_hash(const void *data, size_t length, unsigned char digest[static 32]);

struct hmac256_context
{
	void *handle;
};
int hmac256_init(struct hmac256_context *hmac, const void *key, size_t cb_key);
int hmac256_update(struct hmac256_context *hmac, const void *data, size_t length);
int hmac256_final(struct hmac256_context *hmac, unsigned char digest[static 32]);
int hmac256_hash(const void *key, size_t cb_key, const void *data, size_t length, unsigned char digest[static 32]);

struct aes256_gcm 
{
	void *handle;
	unsigned char key[32];
	unsigned char iv[32]; // only use first 12 bytes 
	
	size_t key_size;
	size_t iv_size;
	size_t block_size;
	
	int use_padding; // PKCS#7 paddings
	
	int (*encrypt)(struct aes256_gcm *aes, const void *plain_text, size_t length, 
		unsigned char **p_encrypted, size_t *p_encrypted_length);
	int (*decrypt)(struct aes256_gcm *aes, const unsigned char *encrypted, size_t cb_encrypted,
		char **p_plain_text, size_t *p_length);
};
struct aes256_gcm *aes256_gcm_init(struct aes256_gcm *aes, 
	const char *key, ssize_t cb_key, 
	const char *salt, ssize_t cb_salt);
void aes256_gcm_cleanup(struct aes256_gcm *aes);
int aes256_gcm_reset(struct aes256_gcm *aes);

int aes256_gcm_encrypt(struct aes256_gcm *aes, void *text, size_t length);
int aes256_gcm_encrypt2(struct aes256_gcm *aes, 
	const void *plain_text, size_t length, 
	unsigned char **p_encrypted, size_t *p_encrypted_length);

int aes256_gcm_decrypt(struct aes256_gcm *aes, void *text, size_t length);
int aes256_gcm_decrypt2(struct aes256_gcm *aes, 
	const unsigned char *encrypted, size_t cb_encrypted,
	char **p_plain_text, size_t *p_length);

#ifdef __cplusplus
}
#endif
#endif
