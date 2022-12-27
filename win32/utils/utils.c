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
	if(NULL == data) {
		data = malloc(size + 1);
		assert(data);
		*p_dst = data;
	}
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


/******************************************************************************
 * read_password_stdin
******************************************************************************/
#if !defined(WIN32) && !defined(_WIN32)
#include <termios.h>
ssize_t read_password_stdin(char secret[], size_t size)
{
	if(NULL == secret || size < 1) return -1;
	
	ssize_t cb_secret = -1;
	struct termios old_attr, attr;
	memset(&old_attr, 0, sizeof(old_attr));
	int rc = tcgetattr(STDIN_FILENO, &old_attr);
	assert(0 == rc);
	attr = old_attr;
	
	attr.c_lflag &= ~ECHO; // hide input characters
	rc = tcsetattr(STDIN_FILENO, TCSANOW, &attr);
	assert(0 == rc);

	char *line = NULL;
	while((line = fgets(secret, size, stdin)))
	{
		printf("\n");
		cb_secret = strlen(line);
		if(cb_secret <= 0) {
			break;
		}
		if(line[cb_secret - 1] == '\n') --cb_secret;
		
		if(cb_secret != 0) break;	
	}
	
	// restore flags
	rc = tcsetattr(STDIN_FILENO, TCSANOW, &old_attr);	
	assert(0 == rc);
	
	if(cb_secret < 0) {
		perror("fgets() failed.");
	}
	return cb_secret;
}
#endif


#define IFADDRS_LIST_MAX_SIZE (256)
#if !defined(WIN32) && !defined(_WIN32)

#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>	// ==> struct sockaddr_ll
#include <netdb.h>
ssize_t query_mac_addrs(struct ifaddr_data **p_addrlist)
{
	assert(p_addrlist);
	
	struct ifaddrs *ifaddr = NULL;
	int rc = 0;
	
	ssize_t num_addrs = 0;
	struct ifaddr_data *addr_list = calloc(IFADDRS_LIST_MAX_SIZE, sizeof(*addr_list));
	assert(addr_list);
	
	rc = getifaddrs(&ifaddr);
	if(rc == -1) {
		perror("query_mac_addrs");
		return -1;
	}

	for(struct ifaddrs *ifa = ifaddr; (NULL != ifa) && (num_addrs < IFADDRS_LIST_MAX_SIZE); ifa = ifa->ifa_next)
	{
		if(NULL == ifa->ifa_addr) continue;
		int family = ifa->ifa_addr->sa_family;
		if(family != AF_PACKET) continue; // not mac addr
		
		struct sockaddr_ll *sll = (struct sockaddr_ll *)ifa->ifa_addr;
		if(sll->sll_hatype != 1) continue; // loopback etc.
		
		if(NULL == ifa->ifa_name) continue;
		struct ifaddr_data *addr = &addr_list[num_addrs++];

		strncpy(addr->name, ifa->ifa_name, sizeof(addr->name) - 1);
		addr->index = sll->sll_ifindex;
		assert(sll->sll_halen == 6);
		memcpy(addr->mac_addr, sll->sll_addr, 6);
	}
	
	// query IPv4 addrs
	for(struct ifaddrs *ifa = ifaddr; (NULL != ifa) && (num_addrs < IFADDRS_LIST_MAX_SIZE); ifa = ifa->ifa_next)
	{
		if(NULL == ifa->ifa_addr) continue;
		int family = ifa->ifa_addr->sa_family;
		if(family != AF_INET) continue; // not ipv4 addr
		
		for(size_t i = 0; i < num_addrs; ++i) {
			if(strncasecmp(addr_list[i].name, ifa->ifa_name, strlen(addr_list[i].name)) == 0) {
				memcpy(&addr_list[i].ip_addr, ifa->ifa_addr, sizeof(struct sockaddr_in));
				addr_list[i].addr_len = sizeof(struct sockaddr_in);
				break;
			}
		}
	}
	
	*p_addrlist = addr_list;
	freeifaddrs(ifaddr);
	return num_addrs;
}
#else // win32
static ssize_t get_win32_error_desc(unsigned long err_code, char **p_errmsg, size_t size) 
{
	DWORD dwFlags = FORMAT_MESSAGE_FROM_SYSTEM;
	if(size == 0) dwFlags |= FORMAT_MESSAGE_ALLOCATE_BUFFER;
	
	return FormatMessage(dwFlags, NULL, err_code, 
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), 
		(LPSTR)p_errmsg, size, NULL);
}

static ssize_t widechar_to_multibyte(LPCWCHAR wtext, ssize_t cb_wtext, char **p_utf8, size_t utf8_size)
{
	char *utf8 = *p_utf8;
	ssize_t cb = 0;
	UINT code_page = GetACP();
	if(NULL == utf8) {
		utf8_size = 0;
		cb = WideCharToMultiByte(code_page, 0, wtext, cb_wtext, utf8, utf8_size, NULL, NULL);
		if(cb <= 0) return -1;
		
		utf8 = calloc(cb + 1, 1);
		assert(utf8);
		*p_utf8 = utf8;
	}
	cb = WideCharToMultiByte(code_page, 0, wtext, cb_wtext, utf8, utf8_size, NULL, NULL);
	fprintf(stderr, "cb=%ld, utf8=%s\n", (long)cb, utf8);
	return cb;
}


ssize_t query_mac_addrs(struct ifaddr_data **p_addrlist)
{
	ULONG size = 0; 
	ULONG family = AF_UNSPEC;
	ULONG flags = GAA_FLAG_INCLUDE_ALL_INTERFACES;
	PIP_ADAPTER_ADDRESSES addrs = NULL;
	ULONG ret = GetAdaptersAddresses(family, flags, 0, addrs, &size);
	if(ret != ERROR_BUFFER_OVERFLOW) return -1;
	
	assert(size > 0);
	addrs = LocalAlloc(LPTR, size);
	assert(addrs);
	
	ret = GetAdaptersAddresses(family, flags, 0, addrs, &size);
	if(ret != ERROR_SUCCESS) {
		char *err_msg = NULL;
		ssize_t cb_msg = get_win32_error_desc(ret, &err_msg, 0);
		if(cb_msg > 0) {
			fprintf(stderr, "%s() failed: %s\n", __FUNCTION__, err_msg);
			LocalFree(err_msg);
		}
		return -1;
	}
	
	ssize_t count = 0;
	static const size_t max_list_size = IFADDRS_LIST_MAX_SIZE;
	struct ifaddr_data * list = calloc(max_list_size, sizeof(*list));
	assert(list);
	
	PIP_ADAPTER_ADDRESSES addr = addrs;
	for(; NULL != addr; addr = addr->Next) {
		if(addr->IfType != IF_TYPE_ETHERNET_CSMACD && addr->IfType != IF_TYPE_IEEE80211) continue;
		struct ifaddr_data *item = &list[count++];
		item->type = addr->IfType;
		
		char friendly_name[1024] = "";
		char *sz_name = friendly_name;
		ssize_t cb_name = 0;
		if(addr->FriendlyName) {
			cb_name = widechar_to_multibyte(addr->FriendlyName, -1, &sz_name, sizeof(friendly_name));
		}
		
		debug_printf("== AdapterName: %s\n", addr->AdapterName);
		debug_printf("    IFType: %d\n", (int)addr->IfType);
		
		if(addr->PhysicalAddressLength == 6) {
			memcpy(item->mac_addr, addr->PhysicalAddress, 6);
		}
		if(cb_name > 0) {
			strncpy(item->name, friendly_name, sizeof(item->name));
		}
	}
	
	if(count > 0) {
		*p_addrlist	= realloc(list, count * sizeof(*list));
	}else {
		free(list);
	}
	
	LocalFree(addrs);
	return count;
}

#endif
