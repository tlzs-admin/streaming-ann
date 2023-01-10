/*
 * auto_buffer.c
 * 
 * Copyright 2021 chehw <hongwei.che@gmail.com>
 * 
 * The MIT License
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of 
 * this software and associated documentation files (the "Software"), to deal in 
 * the Software without restriction, including without limitation the rights to 
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies 
 * of the Software, and to permit persons to whom the Software is furnished to 
 * do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all 
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
 * SOFTWARE.
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "auto_buffer.h"
#include "utils.h"



#define AUTO_BUFFER_ALLOC_SIZE (4096)
auto_buffer_t * auto_buffer_init(auto_buffer_t * buf, size_t size)
{
	if(NULL == buf) buf = calloc(1, sizeof(*buf));
	else memset(buf, 0, sizeof(*buf)); 

	assert(buf);
	int rc = auto_buffer_resize(buf, size);
	assert(0 == rc);
	
	return buf;
}
int auto_buffer_resize(auto_buffer_t * buf, size_t size)
{
	assert(buf);
	int rc = 0;
	if(size == -1 || size == 0) size = AUTO_BUFFER_ALLOC_SIZE;
	else size = (size + AUTO_BUFFER_ALLOC_SIZE - 1) / AUTO_BUFFER_ALLOC_SIZE * AUTO_BUFFER_ALLOC_SIZE;
	
	if(size <= buf->size) return 0;
	void * data = realloc(buf->data, size);
	assert(data);
	if(NULL == data) {
		rc = errno;
		return rc;
	}
	buf->data = data;
	buf->size = size;
	
	//~ debug_printf("%s(%p): data=%p", __FUNCTION__, buf, buf?buf->data:NULL);
	return 0;
}
void auto_buffer_cleanup(auto_buffer_t * buf)
{
	//~ debug_printf("%s(%p): data=%p", __FUNCTION__, buf, buf?buf->data:NULL);
	if(NULL == buf) return;
	if(buf->data) free(buf->data);
	memset(buf, 0, sizeof(*buf));
	return;
}

int auto_buffer_push(auto_buffer_t * buf, const void * data, size_t length)
{
	assert(buf);
	if(data && length == -1) length = strlen((char *)data);
	if(NULL== data || length == 0) return 0;
	
	size_t minimal_buf_size = buf->start_pos + buf->length + length;
	if(minimal_buf_size < length) {
		errno = EOVERFLOW;
		return -1;
	}
	
	int rc = auto_buffer_resize(buf, minimal_buf_size);
	if(rc) return rc;
	
	memcpy(buf->data + buf->start_pos + buf->length, data, length);
	buf->length += length;
	
	return 0;
}
size_t auto_buffer_pop(auto_buffer_t * buf, unsigned char ** p_data, size_t data_size)
{
	assert(buf);
	if(NULL == p_data) return buf->length;	// return needed size
	
	if(data_size == 0) data_size = buf->length;
	size_t length = data_size;
	if(length > buf->length) length = buf->length;
	
	unsigned char * data = *p_data;
	if(NULL == data) {
		data = calloc(length, 1);
		assert(data);
		*p_data = data;
	}
	
	memcpy(data, buf->data + buf->start_pos, length);
	buf->start_pos += length;
	buf->length -= length;
	
	if(buf->length == 0) buf->start_pos = 0;	// reset start_pos
	return length;
}

const unsigned char * auto_buffer_get_data(auto_buffer_t * buf)
{
	assert(buf);
	if(NULL == buf->data) return NULL;
	return (buf->data + buf->start_pos);
}
#undef AUTO_BUFFER_ALLOC_SIZE


#if defined(_TEST_AUTO_BUFFER) && defined(_STAND_ALONE)
int main(int argc, char ** argv) 
{
	auto_buffer_t buf[1], *p_buf;
	
	// test 1. auto_buffer on stack
	p_buf = auto_buffer_init(buf, 0);
	auto_buffer_cleanup(buf);
	
	// test 2. auto_buffer on heap, need to free
	p_buf = auto_buffer_init(NULL, 0);
	auto_buffer_cleanup(p_buf);
	free(p_buf);
	
	// test 3. auto_buffer push/pop
#define BUF_SIZE (1024)
	static const unsigned char padding_data[BUF_SIZE];
	auto_buffer_init(buf, 0);
	auto_buffer_push(buf, padding_data, sizeof(padding_data));
	auto_buffer_push(buf, padding_data, sizeof(padding_data));
	assert(buf->length == (BUF_SIZE * 2));
	
	unsigned char data[BUF_SIZE];
	unsigned char * p_data = data;
	auto_buffer_pop(buf, &p_data, sizeof(data));
	assert(buf->length == BUF_SIZE && buf->start_pos == BUF_SIZE);
	auto_buffer_cleanup(buf);
	
	return 0;
}
#endif
