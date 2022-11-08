/*
 * utils.c
 * 
 * Copyright 2019 chehw <htc.chehw@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>


#include <time.h>
#include <errno.h>
#include <unistd.h>

#include <sys/stat.h>
#include "utils.h"

FILE * g_log_fp;

static const char s_hex_chars[] = "0123456789abcdef";
static const unsigned char s_hex_table[256] = {
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	 0,  1,  2,  3,  4,  5,  6,  7,     8,  9, -1, -1, -1, -1, -1, -1,

	-1, 10, 11, 12, 13, 14, 15, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, 10, 11, 12, 13, 14, 15, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,

	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,

	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1, 
};

ssize_t bin2hex(const unsigned char * data, size_t length, char * hex)
{
	ssize_t	cb = (ssize_t)length * 2;
	for(ssize_t i = 0; i < length; ++i)
	{
		register unsigned char c = data[i];
		hex[i * 2 + 0] = s_hex_chars[((c >> 4) & 0x0F)];
		hex[i * 2 + 1] = s_hex_chars[((c) & 0x0F)];
	}
	hex[cb] = '\0';
	return cb;
}

ssize_t hex2bin(const char * hex, size_t length, unsigned char * data)
{
	ssize_t cb = length / 2;
	for(ssize_t i = 0; i < cb; ++i)
	{
		register unsigned char hi = s_hex_table[(unsigned char)hex[i * 2 + 0]];
		register unsigned char lo = s_hex_table[(unsigned char)hex[i * 2 + 1]];
		if(hi == -1 || lo == -1) return 0;
		
		data[i] = (hi << 4) | lo;
	}
	return cb;
}


/*************************************************
 * app_timer
*************************************************/
static app_timer_t g_timer[1];
double app_timer_start(app_timer_t * timer)
{
	struct timespec ts[1];
	memset(ts, 0, sizeof(ts));
	clock_gettime(CLOCK_MONOTONIC, ts);
	timer->begin = (double)ts->tv_sec + (double)ts->tv_nsec / 1000000000.0;
	return timer->begin;
}
double app_timer_stop(app_timer_t * timer)
{
	struct timespec ts[1];
	memset(ts, 0, sizeof(ts));
	clock_gettime(CLOCK_MONOTONIC, ts);
	timer->end = (double)ts->tv_sec + (double)ts->tv_nsec / 1000000000.0;
	return (timer->end - timer->begin);
}

double global_timer_start()
{
	return app_timer_start(g_timer);
}

double global_timer_stop(const char * prefix)
{
	double time_elapsed = app_timer_stop(g_timer);
	if(NULL == prefix) prefix = "()";
	fprintf(stderr, "== [%s] ==: time_elapsed = %.6f ms\n", 
		prefix,
		time_elapsed * 1000.0);
	return time_elapsed;
}

ssize_t load_binary_data(const char * filename, unsigned char **p_dst)
{
	struct stat st[1];
	int rc;
	rc = stat(filename, st);
	if(rc)
	{
		fprintf(stderr, "[ERROR]::%s(%d)::%s(%s)::stat::%s\n", 
			__FILE__, __LINE__, __FUNCTION__, filename,
			strerror(rc));
		return -1;
	}
	
	if(!S_ISREG(st->st_mode) )
	{
		fprintf(stderr, "[ERROR]::%s(%d)::%s(%s)::not regular file!\n", 
			__FILE__, __LINE__, __FUNCTION__, filename);
		return -1;
	}
	
	ssize_t size = st->st_size;
	if(size <= 0)
	{
		fprintf(stderr, "[ERROR]::%s(%d)::%s(%s)::invalid file-length: %ld!\n", 
			__FILE__, __LINE__, __FUNCTION__, filename,
			(long)size
		);
		return -1;
	}
	if(NULL == p_dst) return (size + 1);		// return buffer size	( append '\0' for ptx file)
	
	FILE * fp = fopen(filename, "rb");
	assert(fp);
	
	unsigned char * data = *p_dst;
	*p_dst = realloc(data, size + 1);
	assert(*p_dst);
	
	data = *p_dst;
	ssize_t length = fread(data, 1, size, fp);
	fclose(fp);
	
	assert(length == size);	
	data[length] = '\0';
	return length;
}

#define is_white_char(c) 	((c) == ' ' || (c) == '\t' || (c) == '\r' || (c) == '\n')
char * trim_left(char * p_begin, char * p_end)
{
	assert(p_begin);
	if(NULL == p_end) p_end = p_begin + strlen(p_begin);
	while(p_begin < p_end && is_white_char(*p_begin)) ++p_begin;
	
	return p_begin;
}
char * trim_right(char * p_begin, char * p_end)
{
	assert(p_begin);
	if(NULL == p_end) p_end = p_begin + strlen(p_begin);
	while(p_end > p_begin && is_white_char(p_end[-1])) *--p_end = '\0';
	return p_begin;
}

int check_file(const char * path_name)
{
	struct stat st[1];
	memset(st, 0, sizeof(st));
	
	int rc = stat(path_name, st);
	if(rc) return rc;
	
	if(S_ISREG(st->st_mode)) return 0;
	return 1;
}

int check_folder(const char * path_name, int auto_create)
{
	if(NULL == path_name || !path_name[0]) return -1;
	struct stat st[1];
	memset(st, 0, sizeof(st));
	
	int rc = stat(path_name, st);
	if(rc) {
		if(auto_create)
		{
			char command[4096] = "";
			snprintf(command, sizeof(command), "mkdir -p %s", path_name);
			rc = system(command);
		}
		return rc;
	}
	
	if(S_ISDIR(st->st_mode)) return 0;
	return 1;
}


