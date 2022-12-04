/*
 * license-server.c
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

#include <libsoup/soup.h>
#include "license-manager.h"

#include <pthread.h>
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
#define server_lock()   pthread_mutex_lock(&s_mutex)
#define server_unlock() pthread_mutex_unlock(&s_mutex)

static void on_license_sign(SoupServer *server, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, gpointer user_data)
{
	static const char *mime_type = "application/octet-stream";
	struct license_manager *license_mgr = user_data;
	
	if(msg->method != SOUP_METHOD_POST) {
		soup_message_set_status(msg, SOUP_STATUS_FORBIDDEN);
		return;
	}
	
	SoupMessageHeaders *request_headers = msg->request_headers;
	const char *content_type = soup_message_headers_get_content_type(request_headers, NULL);
	long content_length = soup_message_headers_get_content_length(request_headers);
	
	printf("content-type: %s\n", content_type);
	
	if(NULL == content_type || strcasecmp(content_type, mime_type) != 0) {
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	if(content_length != sizeof(struct license_record) || NULL == msg->request_body->data) {
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	int status = 0;
	unsigned char sig[100] = { 0 };
	size_t cb_sig = sizeof(sig);
	
	struct license_record *record = (struct license_record *)msg->request_body->data;
	
	server_lock();
	status = license_manager_sign(license_mgr, record, sig, &cb_sig);
	server_unlock();
	
	if(status != license_status_signed) {
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	soup_message_set_response(msg, mime_type, SOUP_MEMORY_COPY, (const char *)sig, cb_sig);
	soup_message_set_status(msg, SOUP_STATUS_OK);
	
	return;
}

static void dump_hex2(const void *_data, size_t length, FILE *fp)
{
	const unsigned char *data = _data;
	if(NULL == data) return;
	
	if(NULL == fp) fp = stderr;
	for(int i = 0; i < length; ++i) {
		fprintf(fp, "%.2x", data[i]);
	}
	fprintf(fp, "\n");
	return;
}

static void on_license_verify(SoupServer *server, SoupMessage *msg, const char *path, GHashTable *query, SoupClientContext *client, gpointer user_data)
{
	static const char *mime_type = "application/octet-stream";
	struct license_manager *license_mgr = user_data;
	assert(license_mgr);
	
	if(msg->method != SOUP_METHOD_POST) {
		soup_message_set_status(msg, SOUP_STATUS_FORBIDDEN);
		return;
	}
	
	SoupMessageHeaders *request_headers = msg->request_headers;
	const char *content_type = soup_message_headers_get_content_type(request_headers, NULL);
	long content_length = soup_message_headers_get_content_length(request_headers);
	if(NULL == content_type || strcasecmp(content_type, mime_type) != 0) {
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	if(content_length <= sizeof(struct license_record) || NULL == msg->request_body->data) {
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	int status = 0;
	
	struct license_record *record = (struct license_record *)msg->request_body->data;
	unsigned char *sig = (unsigned char *)msg->request_body->data + sizeof(struct license_record);
	unsigned char *p_end = (unsigned char *)msg->request_body->data + content_length;
	assert(p_end > sig);
	size_t cb_sig = p_end - sig;
	
	license_record_dump(record, stderr);
	dump_hex2(sig, cb_sig, stderr);
	
	server_lock();
	status = license_manager_verify(license_mgr, record, sig, cb_sig);
	server_unlock();
	
	printf("cb_sig: %ld, status: %d\n", (long)cb_sig, status);
	if(status != license_status_verified) {
		soup_message_set_status(msg, SOUP_STATUS_NOT_ACCEPTABLE);
		return;
	}
	
	soup_message_set_response(msg, "text/plain", SOUP_MEMORY_STATIC, "OK", 2);
	soup_message_set_status(msg, SOUP_STATUS_OK);
	
	return;
}

static ssize_t load_ca_privkey(unsigned char privkey[static 32], const char *keyfile)
{
	ssize_t cb = 0;
	if(NULL == keyfile) keyfile = ".private/keys/ca-privkey.dat";
	FILE *fp = fopen(keyfile, "r");
	assert(fp);
	cb = fread(privkey, 1, 32, fp);
	assert(cb == 32);
	fclose(fp);
	return cb;
}

static ssize_t load_ca_pubkey(unsigned char pubkey[static 64], const char *keyfile)
{
	ssize_t cb = 0;
	if(NULL == keyfile) keyfile = "keys/ca-pubkey.dat";
	FILE *fp = fopen(keyfile, "r");
	assert(fp);
	cb = fread(pubkey, 1, 64, fp);
	assert(cb == 64);
	fclose(fp);
	return cb;
}

int main(int argc, char **argv)
{
	guint port = 8443;
	if(argc > 1) port = atol(argv[1]);
	
	struct license_manager *sign_mgr = license_manager_new(NULL);
	assert(sign_mgr);
	struct license_manager *verify_mgr = license_manager_new(NULL);
	assert(verify_mgr);
	
	ssize_t cb = 0;
	unsigned char privkey[32] = { 0 };
	unsigned char pubkey[64] = { 0 };
	
	cb = load_ca_privkey(privkey, NULL);
	assert(cb == 32);
	
	cb = load_ca_pubkey(pubkey, NULL);
	assert(cb == 64);
	
	license_manager_load_credentials(sign_mgr, privkey, NULL);
	license_manager_load_credentials(verify_mgr, NULL, pubkey);
	memset(privkey, 0, sizeof(privkey)); // clear sensitive data

	GError *gerr = NULL;
	SoupServer *server = soup_server_new(SOUP_SERVER_SERVER_HEADER, "license-srv/v0.1.0", NULL);
	soup_server_add_handler(server, "/license/sign", on_license_sign, sign_mgr, NULL);
	soup_server_add_handler(server, "/license/verify", on_license_verify, verify_mgr, NULL);
	
	gboolean ok = soup_server_listen_all(server, port, 0, &gerr);
	if(gerr) {
		fprintf(stderr, "soup_server_listen_all(port=%u) failed: %s\n", port, gerr->message);
		g_error_free(gerr);
		gerr = NULL;
	}
	assert(ok);
	
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(loop);
	
	g_main_loop_unref(loop);
	
	license_manager_free(sign_mgr);
	license_manager_free(verify_mgr);
	
	return 0;
}

