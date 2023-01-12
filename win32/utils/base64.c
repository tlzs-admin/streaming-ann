/*
 * base64.c
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

#include "base64.h"



/******************************************************************************
 * base64 table / index
******************************************************************************/
static const char s_b64_table[] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
	'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
	'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
	'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
	'w', 'x', 'y', 'z', '0', '1', '2', '3',
	'4', '5', '6', '7', '8', '9', '+', '/'
};
static const unsigned char s_b64_index[256] = {
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,    // +,/
	52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,    // 0-9
	-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,    // [A-Z], 
	15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
	-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,    // [a-z], 
	41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,63,    
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};


static inline void base64_encode_block(const unsigned char data[static restrict 3], char dst[static restrict 4])
{
	dst[0] = s_b64_table[(data[0] >> 2) & 0x3F];
	dst[1] = s_b64_table[((data[0] & 0x03) << 4) | ((data[1] >> 4) & 0x0F)];
	dst[2] = s_b64_table[((data[1] & 0x0F) << 2) | ((data[2] >> 6) &0x03)];
	dst[3] = s_b64_table[(data[2] & 0x3F)];
	return;
}
static inline ssize_t base64_encode_block_partial(size_t bytes_left, const unsigned char data[static restrict 1], char dst[static restrict 4])
{
	assert(bytes_left == 1 || bytes_left == 2);
	dst[2] = dst[3] = '=';	// padding with '='
	
	dst[0] = s_b64_table[(data[0] >> 2) & 0x3F];
	if(bytes_left == 1) {
		dst[1] = s_b64_table[((data[0] & 0x03) << 4)];
		return 2;
	}
	
	dst[1] = s_b64_table[((data[0] & 0x03) << 4) | ((data[1] >> 4) & 0x0F)];
	dst[2] = s_b64_table[((data[1] & 0x0F) << 2)];
	return 3;
}
ssize_t base64_encode(const void *data, size_t length, char **p_b64)
{
	assert(data);
	if(length == 0 || length == -1) return 0;
	
	size_t num_blocks = length / 3;
	size_t cb_encoded = (num_blocks + 1) * 4;
	if(NULL == p_b64) return cb_encoded + 1;

	
	printf("max encoded bytes: %ld, length: %ld\n", (long)cb_encoded, (long)length);
	char *b64 = *p_b64;
	if(NULL == b64) {
		b64 = calloc(cb_encoded + 1, 1);
		assert(b64);
		*p_b64 = b64;
	}
	char * dst = b64;
	const unsigned char * src = (const unsigned char *)data;
	for(size_t i = 0; i < num_blocks; ++i) {
		base64_encode_block(src, dst);
		src += 3;
		dst += 4;
	}
	
	size_t bytes_left = length % 3;
	ssize_t cb = 0;
	if(bytes_left) {
		cb = base64_encode_block_partial(bytes_left, src, dst);
		assert(cb == 2 || cb == 3);
		
		src += bytes_left;
		dst += 4; // alway add paddings
	}
	*dst = '\0';
	cb_encoded = dst - b64;
	return cb_encoded;
}

static inline size_t base64_decode_block(const char b64[static restrict 4], unsigned char data[static restrict 3])
{
	unsigned char a = s_b64_index[(int)b64[0]];
	unsigned char b = s_b64_index[(int)b64[1]];
	unsigned char c = s_b64_index[(int)b64[2]];
	unsigned char d = s_b64_index[(int)b64[3]];
	if(a > 63 || b > 63 || c > 63 || d > 63) return -1;
	
	data[0] = (a << 2) | ((b >> 4) & 0x03);
	data[1] = ((b & 0x0F) << 4) | ((c >> 2) & 0x0F);
	data[2] = ((c & 0x03) << 6) | (d & 0x3F);
	return 0;
}

static inline ssize_t base64_decode_block_partial(size_t bytes_left, const char b64[static restrict 2], unsigned char data[static restrict 3])
{
	assert(bytes_left == 2 || bytes_left == 3);
	
	unsigned char a, b, c = 0xff;
	a = s_b64_index[(int)b64[0]];
	b = s_b64_index[(int)b64[1]];
	if(a > 63 || b > 63) return -1;
	
	data[0] = (a << 2) | ((b >> 4) & 0x03);
	if(bytes_left == 2) return 1;
	
	c = s_b64_index[(int)b64[2]];
	if(c > 63) return -1;
	data[1] = ((b & 0x0F) << 4) | ((c >> 2) & 0x0F);
	return 2;
}
ssize_t base64_decode(const char *b64, size_t length, unsigned char **p_data)
{
	assert(b64);
	if(length == -1) length = strlen(b64);
	if(length == 0) return 0;
	if(b64[length - 1] == '=') --length;
	if(length && b64[length - 1] == '=') --length;
	if(length == 0) return -1;
	
	ssize_t cb_data = (length + 3) / 4 * 3;
	if(NULL == p_data) return cb_data;
	
	unsigned char *data = *p_data;
	if(NULL == data)
	{
		data = calloc(cb_data, 1);
		assert(data);
		*p_data = data;
	}
	
	const char *src = b64;
	unsigned char *dst = data;
	size_t num_blocks = length / 4;
	for(size_t i = 0; i < num_blocks; ++i){
		int rc = base64_decode_block(src, dst);
		if(rc) return -1;
		src += 4;
		dst += 3;
	}
	
	size_t bytes_left = length % 4;
	if(bytes_left) {
		if(bytes_left != 2 && bytes_left != 3) return -1;	// invalid format
		ssize_t cb = base64_decode_block_partial(bytes_left, src, dst);
		if(cb == -1) return -1;
		assert(cb == 1 || cb == 2);
		src += bytes_left;
		dst += cb;
	}
	
	assert(src == (b64 + length));
	cb_data = dst - data;
	return cb_data;
}



/******************************************************************************
 * base64url table / index
******************************************************************************/
static const char s_b64url_table[] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
	'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
	'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
	'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
	'w', 'x', 'y', 'z', '0', '1', '2', '3',
	'4', '5', '6', '7', '8', '9', '-', '_'
};
static const unsigned char s_b64url_index[256] = {
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,    // -
	52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,    // 0-9
	-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,    // [A-Z], 
	15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
	-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,    // [a-z], 
	41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,63,    // _
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

static inline void base64url_encode_block(const unsigned char data[static restrict 3], char dst[static restrict 4])
{
	dst[0] = s_b64url_table[(data[0] >> 2) & 0x3F];
	dst[1] = s_b64url_table[((data[0] & 0x03) << 4) | ((data[1] >> 4) & 0x0F)];
	dst[2] = s_b64url_table[((data[1] & 0x0F) << 2) | ((data[2] >> 6) &0x03)];
	dst[3] = s_b64url_table[(data[2] & 0x3F)];
	return;
}
static inline ssize_t base64url_encode_block_partial(size_t bytes_left, const unsigned char data[static restrict 3], char dst[static restrict 4])
{
	assert(bytes_left == 1 || bytes_left == 2);
	dst[0] = s_b64url_table[(data[0] >> 2) & 0x3F];
	if(bytes_left == 1) {
		dst[1] = s_b64url_table[((data[0] & 0x03) << 4)];
		return 2;
	}
	
	dst[1] = s_b64url_table[((data[0] & 0x03) << 4) | ((data[1] >> 4) & 0x0F)];
	dst[2] = s_b64url_table[((data[1] & 0x0F) << 2)];
	return 3;
}
ssize_t base64url_encode(const void *data, size_t length, char **p_b64url)
{
	assert(data);
	if(length == 0) return 0;
	
	size_t num_blocks = length / 3;
	size_t cb_encoded = (num_blocks + 1) * 4;
	if(NULL == p_b64url) return cb_encoded + 1;

	
	printf("max encoded bytes: %ld, length: %ld\n", (long)cb_encoded, (long)length);
	char *b64url = *p_b64url;
	if(NULL == b64url) {
		b64url = calloc(cb_encoded + 1, 1);
		assert(b64url);
		*p_b64url = b64url;
	}
	char * dst = b64url;
	const unsigned char * src = (const unsigned char *)data;
	for(size_t i = 0; i < num_blocks; ++i) {
		base64url_encode_block(src, dst);
		src += 3;
		dst += 4;
	}
	
	size_t bytes_left = length % 3;
	ssize_t cb = 0;
	if(bytes_left) {
		cb = base64url_encode_block_partial(bytes_left, src, dst);
		assert(cb == 2 || cb == 3);
		
		src += bytes_left;
		dst += cb;
	}
	*dst = '\0';
	cb_encoded = dst - b64url;
	return cb_encoded;
}



static inline size_t base64url_decode_block(const char b64url[static restrict 4], unsigned char data[static restrict 3])
{
	unsigned char a = s_b64url_index[(int)b64url[0]];
	unsigned char b = s_b64url_index[(int)b64url[1]];
	unsigned char c = s_b64url_index[(int)b64url[2]];
	unsigned char d = s_b64url_index[(int)b64url[3]];
	if(a > 63 || b > 63 || c > 63 || d > 63) return -1;
	
	data[0] = (a << 2) | ((b >> 4) & 0x03);
	data[1] = ((b & 0x0F) << 4) | ((c >> 2) & 0x0F);
	data[2] = ((c & 0x03) << 6) | (d & 0x3F);
	return 0;
}

static inline ssize_t base64url_decode_block_partial(size_t bytes_left, const char b64url[static restrict 4], unsigned char data[static restrict 3])
{
	assert(bytes_left == 2 || bytes_left == 3);
	unsigned char a, b, c = 0xff;
	a = s_b64url_index[(int)b64url[0]];
	b = s_b64url_index[(int)b64url[1]];
	if(a > 63 || b > 63) return -1;
	
	data[0] = (a << 2) | ((b >> 4) & 0x03);
	if(bytes_left == 2) return 1;
	
	c = s_b64url_index[(int)b64url[2]];
	if(c > 63) return -1;
	data[1] = ((b & 0x0F) << 4) | ((c >> 2) & 0x0F);
	return 2;
}
ssize_t base64url_decode(const char *b64url, size_t length, unsigned char **p_data)
{
	assert(b64url);
	if(length == -1) length = strlen(b64url);
	if(length == 0) return 0;
	ssize_t cb_data = (length + 3) / 4 * 3;
	if(NULL == p_data) return cb_data;
	
	unsigned char *data = *p_data;
	if(NULL == data)
	{
		data = calloc(cb_data, 1);
		assert(data);
		*p_data = data;
	}
	
	const char *src = b64url;
	unsigned char *dst = data;
	size_t num_blocks = length / 4;
	for(size_t i = 0; i < num_blocks; ++i){
		int rc = base64url_decode_block(src, dst);
		if(rc) return -1;
		src += 4;
		dst += 3;
	}
	
	size_t bytes_left = length % 4;
	if(bytes_left) {
		if(bytes_left != 2 && bytes_left != 3) return -1;	// invalid format
		ssize_t cb = base64url_decode_block_partial(bytes_left, src, dst);
		if(cb == -1) return -1;
		assert(cb == 1 || cb == 2);
		src += bytes_left;
		dst += cb;
	}
	
	assert(src == (b64url + length));
	cb_data = dst - data;
	return cb_data;
}



#if defined(TEST_BASE64_) && defined(_STAND_ALONE)
#include <iconv.h>
#include <errno.h>

#define TEST(func, ...) do { \
		fprintf(stderr, "\e[33m==== %s() ====\e[39m\n", #func); \
		int rc = func( __VA_ARGS__ ); \
		fprintf(stderr, "%s==> rc = %d\e[39m\n================\n\n", rc?"\e[31m":"\e[32m", rc); \
		assert(0 == rc); \
	} while(0)

int test_base64(void);
int test_base64url(void);
int main(int argc, char **argv)
{
	TEST(test_base64url);
	TEST(test_base64);
	return 0;
}



int test_base64(void)
{
	//~ static const char *text = "Many hands make light work.";
	//~ static const char *verify_b64 = "TWFueSBoYW5kcyBtYWtlIGxpZ2h0IHdvcmsu";
	
	static const char *text = "Many hands make light work";
	static const char *verify_b64 = "TWFueSBoYW5kcyBtYWtlIGxpZ2h0IHdvcms=";
	int rc = 0;
	ssize_t cb_text = strlen(text);
	char *b64 = NULL;
	ssize_t cb = base64_encode(text, cb_text, &b64);
	assert(cb > 0 && b64);
	printf("cb: %ld, b64: [%s]\n", (long)cb, b64);
	rc = strcmp(verify_b64, b64);
	assert(0 == rc);
	
	unsigned char data[100] = {0};
	unsigned char *p_data = data;
	cb = base64_decode(b64, cb, &p_data);
	assert(cb == cb_text);
	printf("text: %s\n", (char *)data);
	rc = strncmp((char*)data, text, cb_text);
	assert(0 == rc);
	
	free(b64);
	return rc;
}

int test_base64url(void)
{
	//~ static const char *header = "{\"alg\":\"RS256\",\"typ\":\"JWT\"}";
	//~ static const char *verify_header = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9";
	
	static const char *header = "{\"alg\":\"RS256\",\"typ\":\"JWT\"} ";
	static const char *verify_header = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9IA";
	
	int rc = 0;
	size_t cb_header = strlen(header);
	char *b64url = NULL;
	ssize_t cb = base64url_encode(header, cb_header, &b64url);
	assert(cb > 0 && b64url);
	printf("cb: %ld, b64: [%s]\n", (long)cb, b64url);
	
	rc = strcmp(b64url, verify_header);
	assert(0 == rc);
	
	unsigned char data[100] = {0};
	unsigned char *p_data = data;
	
	cb = base64url_decode(b64url, cb, &p_data);
	assert(cb == cb_header);
	printf("header: %s\n", (char *)data);
	rc = strncmp((char*)data, header, cb_header);
	assert(0 == rc);
	
	free(b64url);
	return rc;
}
#endif

