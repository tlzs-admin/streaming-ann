#ifndef CHLIB_AUTO_BUFFER_H_
#define CHLIB_AUTO_BUFFER_H_

#include <stdio.h>
#ifdef __cplusplus
extern "C"
#endif

typedef struct auto_buffer
{
	size_t size;
	size_t length;
	size_t start_pos;
	unsigned char * data;
}auto_buffer_t;

auto_buffer_t * auto_buffer_init(auto_buffer_t * buf, size_t size);
int auto_buffer_resize(auto_buffer_t * buf, size_t size);
void auto_buffer_cleanup(auto_buffer_t * buf);

int auto_buffer_push(auto_buffer_t * buf, const void * data, size_t length);
size_t auto_buffer_pop(auto_buffer_t * buf, unsigned char ** p_buf, size_t buf_size);
const unsigned char * auto_buffer_get_data(auto_buffer_t * buf);

#ifdef __cplusplus
}
#endif
#endif

