/*
 * crypto.c
 * 
 * Copyright 2022 chehw <hongwei.che@gmail.com>
 * 
 * The MIT License (MIT)
 * Permission is hereby granted, free of charge, to any person obtaining a copy 
 * of this software and associated documentation files (the "Software"), to deal 
 * in the Software without restriction, including without limitation the rights 
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
 * copies of the Software, and to permit persons to whom the Software is 
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all 
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS 
 * IN THE SOFTWARE.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "utils_crypto.h"
#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>

/******************************************************************************
 * struct sha256_context
******************************************************************************/ 
int sha256_init(struct sha256_context *sha)
{
	return gnutls_hash_init((gnutls_hash_hd_t *)&sha->handle, GNUTLS_DIG_SHA256);
}
int sha256_update(struct sha256_context *sha, const void *data, size_t length)
{
	return gnutls_hash(sha->handle, data, length);
}
int sha256_final(struct sha256_context *sha, unsigned char digest[static 32])
{
	gnutls_hash_deinit(sha->handle, digest);
	return 0;
}
int sha256_hash(const void *data, size_t length, unsigned char digest[static 32])
{
	return gnutls_hash_fast(GNUTLS_DIG_SHA256, data, length, digest);
}


/******************************************************************************
 * struct hmac256_context
******************************************************************************/ 
int hmac256_init(struct hmac256_context *hmac, const void *key, size_t cb_key)
{
	return gnutls_hmac_init((gnutls_hmac_hd_t *)&hmac->handle, GNUTLS_MAC_SHA256, key, cb_key);
}
int hmac256_update(struct hmac256_context *hmac, const void *data, size_t length)
{
	return gnutls_hmac(hmac->handle, data, length);
}
int hmac256_final(struct hmac256_context *hmac, unsigned char digest[static 32])
{
	gnutls_hmac_deinit(hmac->handle, digest);
	return 0;
}
int hmac256_hash(const void *key, size_t cb_key, const void *data, size_t length, unsigned char digest[static 32])
{
	return gnutls_hmac_fast(GNUTLS_MAC_SHA256, key, cb_key, data, length, digest);
}


/******************************************************************************
 * struct aes256_gcm
******************************************************************************/ 
struct aes256_gcm *aes256_gcm_init(struct aes256_gcm *aes, 
	const char *secret, ssize_t cb_secret, 
	const char *salt, ssize_t cb_salt)
{
	static gnutls_cipher_algorithm_t algorithm = GNUTLS_CIPHER_AES_256_GCM;
	
	if(NULL == aes) aes = calloc(1, sizeof(*aes));
	assert(aes);

	aes->encrypt = aes256_gcm_encrypt2;
	aes->decrypt = aes256_gcm_decrypt2;
	
	if(secret && cb_secret == -1) cb_secret = strlen(secret);
	if(salt && cb_salt == -1) cb_salt = strlen(salt);
	
	if(cb_secret < 0) cb_secret = 0;
	if(cb_salt < 0) cb_salt = 0;
	
	sha256_hash(secret, cb_secret, aes->key);
	sha256_hash(salt, cb_salt, aes->iv);
	
	aes->key_size = gnutls_cipher_get_key_size(algorithm);
	aes->iv_size = gnutls_cipher_get_iv_size(algorithm);
	aes->block_size = gnutls_cipher_get_block_size(algorithm);
	
	assert(aes->key_size == 32);
	assert(aes->iv_size == 12);
	assert(aes->block_size == 16);
	
	aes256_gcm_reset(aes);
	return aes;
}
void aes256_gcm_cleanup(struct aes256_gcm *aes)
{
	if(aes->handle) gnutls_cipher_deinit(aes->handle);
	memset(aes, 0, sizeof(*aes));
	return;
}
int aes256_gcm_reset(struct aes256_gcm *aes)
{
	int rc = -1;
	if(aes->handle) {
		gnutls_cipher_deinit(aes->handle);
		aes->handle = NULL;
	}

	rc = gnutls_cipher_init((gnutls_cipher_hd_t *)&aes->handle, GNUTLS_CIPHER_AES_256_GCM, 
		&(gnutls_datum_t){.data = aes->key, .size = aes->key_size},
		&(gnutls_datum_t){.data = aes->iv, .size = aes->iv_size}
	);
	return rc;
}

int aes256_gcm_encrypt(struct aes256_gcm *aes, void *text, size_t length)
{
	return gnutls_cipher_encrypt(aes->handle, text, length);
}

int aes256_gcm_decrypt(struct aes256_gcm *aes, void *text, size_t length)
{
	return gnutls_cipher_decrypt(aes->handle, text, length);
}

int aes256_gcm_encrypt2(struct aes256_gcm *aes, 
	const void *plain_text, size_t length, 
	unsigned char **p_encrypted, size_t *p_encrypted_length)
{
	int rc = -1;
	const size_t block_size = aes->block_size;
	size_t total_blocks = (length + block_size - 1) / block_size;
	
	if(NULL == p_encrypted) {
		if(p_encrypted_length) *p_encrypted_length = (total_blocks + 1) * block_size;
		return p_encrypted_length?0:-1;
	}
	
	unsigned char *encrypted = *p_encrypted;
	if(NULL == encrypted) {
		encrypted = calloc(total_blocks + 1, block_size);	// (total blocks) + (1 padding blocks)
		assert(p_encrypted);
		*p_encrypted = encrypted;
	}
	
	const char *src = plain_text;
	unsigned char *dst = encrypted;
	size_t num_blocks = length / block_size;
	size_t num_bytes = num_blocks * block_size;
	assert(num_bytes <= length);
	
	if(num_bytes) {
		rc = gnutls_cipher_encrypt2(aes->handle, src, num_bytes, dst, num_bytes);
		assert(0 == rc);
		
		src += num_bytes;
		dst += num_bytes;
	}
	
	size_t bytes_left = length - num_bytes;
	assert(bytes_left < 16);
	
	size_t padding_size = 16;
	unsigned char paddings[32] = { 0 };
	if(bytes_left) memcpy(paddings, src, bytes_left);
	
	if(aes->use_padding) {
		// append PKCS#7 paddings	
		paddings[bytes_left] = 0x80;
		ssize_t cb_padding = 16 - bytes_left;
		
		if(cb_padding < 2) {
			cb_padding += 16;
			padding_size += 16;
		}
		paddings[padding_size - 1] = cb_padding;
	}
		
	rc = gnutls_cipher_encrypt2(aes->handle, paddings, padding_size, dst, padding_size);
	assert(0 == rc);
	dst += padding_size;
	
	
	if(p_encrypted_length) *p_encrypted_length = dst - encrypted;
	return rc;
}


int aes256_gcm_decrypt2(struct aes256_gcm *aes, 
	const unsigned char *encrypted, size_t cb_encrypted,
	char **p_plain_text, size_t *p_length)
{
	int rc = -1;
	const size_t block_size = aes->block_size;
	assert(encrypted);
	assert((cb_encrypted % block_size) == 0);
	if((cb_encrypted % block_size) != 0) return -1;
	
	if(0 == cb_encrypted) return 0;
	size_t total_blocks = (cb_encrypted + block_size - 1) / block_size;
	
	if(NULL == p_plain_text) {
		if(p_length) *p_length = (total_blocks * block_size);
		return p_length?0:-1;
	}
	
	char *plain_text = *p_plain_text;
	if(NULL == plain_text) {
		plain_text = calloc(total_blocks, block_size);
		assert(plain_text);
		*p_plain_text = plain_text;
	}
	
	rc = gnutls_cipher_decrypt2(aes->handle, encrypted, cb_encrypted, plain_text, cb_encrypted);
	assert(0 == rc);
	
	size_t length = cb_encrypted;
	if(aes->use_padding) {
		size_t cb_padding = plain_text[length - 1];
		assert(cb_padding < 18);
		
		if(cb_padding > 1) {
			assert((unsigned char)plain_text[length - cb_padding] == (unsigned char)0x80);
			plain_text[length - cb_padding] = '\0';
		}
		
		length -= cb_padding;
	}
	
	if(p_length) *p_length = length;
	return rc;
}

