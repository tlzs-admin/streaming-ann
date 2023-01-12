/*
 * send_mail.c
 * 
 * Copyright 2023 chehw <hongwei.che@gmail.com>
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

#include "smtp-client.h"
#include <curl/curl.h>
#include <time.h>

#include "utils.h"
#include "auto_buffer.h"
#include "base64.h"

static ssize_t mime_b64_encode(const char *charset, const char *text, size_t cb_text, size_t b64_block_size, char **p_output)
{
	/* 
	 * output fmt: 
	 * - single line format: 
	 *     "=?<charset>?B?<b64_block>?="
	 * 
	 * - multiple lines format:
	 *     "=?<charset>?B?<b64_block>?=\r\n"
	 *     " =?<charset>?B?<b64_block>?=\r\n"
	 *     ...
	 *     " =?<charset>?B?<b64_block>?="
	*/
	if(b64_block_size == 0) b64_block_size = 64;	// <== default block size
	
	if(NULL == charset || !charset[0]) charset = "UTF-8";
	if(cb_text == -1) cb_text = strlen(text);
	if(cb_text == 0) return 0;
	
	size_t cb_b64 = (cb_text + 2) / 3 * 4;
	ssize_t num_blocks = 1;
	num_blocks = (cb_b64 + b64_block_size - 1) / b64_block_size;
	
	size_t max_line_size = 3 // " =?"
			+ strlen(charset) 
			+ 3 // "?B?"
			+ b64_block_size
			+ 4; // "?=\r\n"
	
	size_t max_output_size = max_line_size * num_blocks;
	assert(max_output_size > 0);
	if(NULL == p_output) return max_output_size + 1;
	
	char *b64 = NULL;
	if(cb_text > 0) {
		cb_b64 = base64_encode(text, cb_text, &b64);
		assert(cb_b64 > 0);
	}
	
	char *output = *p_output;
	if(NULL == output) {
		output = calloc(max_output_size + 1, 1);
		assert(output);
		*p_output = output;
	}
	
	const char *src = b64;
	char *p = output;
	char *p_end = output + max_output_size + 1;
	size_t cb_block = (cb_b64 < b64_block_size)?cb_b64:b64_block_size;
	
	ssize_t cb = snprintf(p, p_end - p, "=?%s?B?", charset);
	assert(cb > 0 && (p + cb + cb_block + 2) < p_end);
	p += cb;
	
	memcpy(p, src, cb_block);
	p += cb_block;
	src += cb_block;
	cb_b64 -= cb_block;
	*p++ = '?'; *p++ = '=';
	
	while(cb_b64 > 0) {
		assert((p + 2) < p_end);
		*p++ = '\r'; *p++ = '\n';
		
		cb_block = (cb_b64 < b64_block_size)?cb_b64:b64_block_size;
		cb = snprintf(p, p_end - p, " =?%s?B?", charset);
		assert(cb > 0 && (p + cb + cb_block + 2) < p_end);
		
		p += cb;
		memcpy(p, src, cb_block);
		p += cb_block;
		src += cb_block;
		cb_b64 -= cb_block;
		
		*p++ = '?'; *p++ = '=';
	}
	
	if(b64) free(b64);
	assert(p < p_end);
	return (p - output);
}


/******************************************************************************
 * struct clib_string_array
******************************************************************************/

#define STRING_ARRAY_ALLOC_SIZE (256)
static int string_array_resize(struct clib_string_array *sarray, size_t new_size)
{
	if(NULL == sarray) return -1;
	if(new_size <= sarray->size) return 0;
	
	if(new_size == 0) new_size = STRING_ARRAY_ALLOC_SIZE;
	else new_size = (new_size + STRING_ARRAY_ALLOC_SIZE - 1) / STRING_ARRAY_ALLOC_SIZE * STRING_ARRAY_ALLOC_SIZE;
	
	char **items = realloc(sarray->items, new_size);
	assert(items);
	memset(items + sarray->size, 0, sizeof(*items) * (new_size - sarray->size));
	
	sarray->size = new_size;
	sarray->items = items;
	return 0;
}
static int string_array_push(struct clib_string_array *sarray, const char *text)
{
	assert(sarray);
	if(NULL == text) return 1;
	if(string_array_resize(sarray, sarray->length + 1)) return -1;
	
	char *item = strdup(text);
	assert(item);
	
	sarray->items[sarray->length++] = item;
	return 0;
}
struct clib_string_array *clib_string_array_init(struct clib_string_array *sarray, size_t size)
{
	if(NULL == sarray) sarray = calloc(1, sizeof(*sarray));
	assert(sarray);
	
	if(size == 0) size = STRING_ARRAY_ALLOC_SIZE;
	char **items = calloc(size, sizeof(*items));
	assert(items);
	
	sarray->size = size;
	sarray->items = items;
	sarray->length = 0;
	sarray->push = string_array_push;
	return sarray;
}
#undef STRING_ARRAY_ALLOC_SIZE
void clib_string_array_clear(struct clib_string_array *sarray)
{
	if(NULL == sarray) return;
	for(size_t i = 0; i < sarray->length; ++i) {
		free(sarray->items[i]);
		sarray->items[i] = NULL;
	}
	sarray->length = 0;
	sarray->size = 0;
	free(sarray->items);
	return;
}

/******************************************************************************
 * struct smtp_mail_info
******************************************************************************/
struct smtp_mail_info *smtp_mail_info_init(struct smtp_mail_info *mail, const char *from)
{
	if(NULL == mail) mail = calloc(1, sizeof(*mail));
	assert(mail);
	
	if(from) mail->from = strdup(from);
	
	clib_string_array_init(mail->to_list, 0);
	clib_string_array_init(mail->cc_list, 0);
	clib_string_array_init(mail->bcc_list, 0);
	
	return mail;
}
void smtp_mail_info_clear(struct smtp_mail_info *mail)
{
	if(NULL == mail) return;
	if(mail->from) {
		free(mail->from);
		mail->from = NULL;
	}
	clib_string_array_clear(mail->to_list);
	clib_string_array_clear(mail->cc_list);
	clib_string_array_clear(mail->bcc_list);
	
	if(mail->subject) {
		free(mail->subject);
		mail->subject = NULL;
	}
	if(mail->content_type) {
		free(mail->content_type);
		mail->content_type = NULL;
	}
	mail->boundary = NULL;
	mail->cb_boundary = 0;
	
	if(mail->parts) {
		for(ssize_t i = 0; i < mail->num_parts; ++i) {
			if(mail->part_headers) {
				struct clib_string_array *headers = &mail->part_headers[i];
				if(headers) clib_string_array_clear(headers);
			}
			auto_buffer_cleanup(&mail->parts[i]);
		}
		free(mail->part_headers);
		free(mail->parts);
		mail->part_headers = NULL;
		mail->parts = NULL;
		mail->num_parts = 0;
	}
	
	auto_buffer_cleanup(&mail->body);
	return;
}


/******************************************************************************
 * struct smtp_mail_info
******************************************************************************/
static int smtp_client_load_credential(struct smtp_client *smtp, const char *credentials_file, const char *file_password)
{
	assert(smtp);
	if(NULL == credentials_file) {
		const char *user = getenv("SMTP_USER");
		const char *secret = getenv("SMTP_SECRET");
		
		assert(user && user[0]);
		assert(secret && secret[0]);
		
		strncpy(smtp->user, user, sizeof(smtp->user) - 1);
		strncpy(smtp->secret, secret, sizeof(smtp->secret) - 1);
		return 0;
	}

	///<@ todo : 
	///   load secrets from credentials_file
	return -1;
}

#define MAX_LINE_SIZE (1024)
#define payload_append_fmt(payload, fmt, ...) ({ \
		ssize_t cb = 0; \
		int rc = auto_buffer_resize(payload, payload->start_pos + payload->length + MAX_LINE_SIZE); \
		assert(0 == rc); \
		char *p = (char *)payload->data + payload->start_pos + payload->length; \
		cb = snprintf(p, MAX_LINE_SIZE - 1, fmt, ##__VA_ARGS__); \
		assert(cb >= 0); \
		p[cb] = '\0'; \
		payload->length += cb; \
		cb; \
	})


int smtp_mail_info_serialize(struct smtp_mail_info *mail, struct auto_buffer *payload, time_t timestamp)
{
	static const char *RFC_2822_COMPLIANT_DATE_FMT = "%a, %d %b %Y %T %z";
	assert(mail && payload);
	assert(mail->from);
	assert(mail->to_list->length > 0);
	
	auto_buffer_init(payload, 65536);
	
	if(timestamp <= 0) {
		struct timespec ts[1] = {{ 0 }};
		if(0 == clock_gettime(CLOCK_REALTIME, ts)) timestamp = ts->tv_sec;
		assert(timestamp > 0);
	}
	
	char sz_date[100] = "";
	struct tm t[1];
	memset(t, 0, sizeof(t));
	
	localtime_r(&timestamp, t);
	ssize_t cb = strftime(sz_date, sizeof(sz_date) - 1, RFC_2822_COMPLIANT_DATE_FMT, t);
	assert(cb > 0); 
	
	struct clib_string_array *list = NULL;
	char line[4096] = "";
	cb = snprintf(line, sizeof(line) - 1, "Date: %s\r\n", sz_date);
	auto_buffer_push(payload, line, cb);
	
	cb = snprintf(line, sizeof(line) - 1, "From: %s\r\n", mail->from);
	auto_buffer_push(payload, line, cb);


#define push_list_fields(payload, list) do { \
		char *item = list->items[0]; \
		assert(item); \
		payload_append_fmt(payload, "<%s>", item); \
		for(size_t ii = 1; ii < list->length; ++ii) { \
			item = list->items[ii]; \
			if(NULL == item) continue; \
			payload_append_fmt(payload, ", <%s>", item); \
		} \
		auto_buffer_push(payload, "\r\n", 2); \
	}while(0)

	list = mail->to_list;
	auto_buffer_push(payload, "To: ", 4);
	push_list_fields(payload, list);
	
	if(mail->cc_list->length > 0) {
		list = mail->cc_list;
		auto_buffer_push(payload, "Cc: ", 4);
		push_list_fields(payload, list);
	}
	
	if(mail->bcc_list->length > 0) {
		list = mail->bcc_list;
		auto_buffer_push(payload, "Bcc: ", 5);
		push_list_fields(payload, list);
	}
#undef push_list_fields

	int rnd_value = rand() % 1000;
	payload_append_fmt(payload, "Message-ID: <%.9ld-%.3d-%s>\r\n", (long)timestamp, rnd_value, mail->from);
	
	
	char *mime_subject = NULL;
	if(mail->subject) {
		cb = mime_b64_encode(mail->charset, mail->subject, -1, 0, &mime_subject);
		assert(cb > 0);
	}
	payload_append_fmt(payload, "Subject: %s\r\n", mime_subject?mime_subject:"");
	if(mime_subject) { free(mime_subject); mime_subject = NULL; }
	
	// append headers
	if(mail->content_type) {
		payload_append_fmt(payload, "Content-Type: %s\r\n", mail->content_type);
	}
	struct clib_string_array *headers = mail->headers;
	for(ssize_t ii = 0; ii < headers->length; ++ii) {
		payload_append_fmt(payload, "%s\r\n", headers->items[ii]);
	}
	auto_buffer_push(payload, "\r\n", 2);
	
	if(mail->is_multipart) {
		assert(mail->boundary);
		payload_append_fmt(payload, "--%s\r\n", mail->boundary);
		for(ssize_t i = 0; i < mail->num_parts; ++i) {
			struct clib_string_array *headers = &mail->part_headers[i];
			assert(headers);
			for(ssize_t ii = 0; ii < headers->length; ++ii) {
				payload_append_fmt(payload, "%s\r\n", headers->items[ii]);
			}
			auto_buffer_push(payload, "\r\n", 2);
			
			struct auto_buffer *part = &mail->parts[i];
			if(part && part->length > 0) {
				auto_buffer_push(payload, part->data, part->length);
			}
			auto_buffer_push(payload, "\r\n", 2);
		}
	}else {
		struct auto_buffer *body = &mail->body;
		if(body && body->length > 0) {
			auto_buffer_push(payload, body->data, body->length);
		}
		auto_buffer_push(payload, "\r\n", 2);
	}
	return 0;
}

struct payload_closure
{
	char *data;
	size_t length;
};

static size_t on_upload_mail(char *ptr, size_t size, size_t n, void *user_data)
{
	struct payload_closure *payload = user_data;
	
	size_t cb = size * n;
	if(cb == 0) return 0;
	if(cb > payload->length) cb = payload->length;
	
	if(cb > 0) {
		memcpy(ptr, payload->data, cb);
		payload->data += cb;
		payload->length -= cb;
	}
	return cb;
}

static int smtp_client_send(struct smtp_client *smtp, const struct smtp_mail_info *mail, struct auto_buffer *payload)
{
	int rc = 0;
	assert(smtp && smtp->server_url && smtp->user && smtp->secret);
	
	fprintf(stderr, "smtp server: %s\n", smtp->server_url);
	fprintf(stderr, "smtp user: %s\n", smtp->user);
	
	struct auto_buffer payload_buf = { 0 };
	if(NULL == payload) payload = auto_buffer_init(&payload_buf, 4096);
	assert(payload);
	
	if(payload->length == 0) {
		rc = smtp_mail_info_serialize((struct smtp_mail_info *)mail, payload, 0);
		assert(0 == rc);
	}
	
	struct payload_closure closure = {
		.data = (char *)payload->data,
		.length = payload->length
	};
	
	CURL *curl = curl_easy_init();
	struct curl_slist *recipients = NULL;
	assert(curl);
	
	curl_easy_setopt(curl, CURLOPT_URL, smtp->server_url);
	curl_easy_setopt(curl, CURLOPT_USERNAME, smtp->user);
	curl_easy_setopt(curl, CURLOPT_PASSWORD, smtp->secret);
	
	if(mail->from) curl_easy_setopt(curl, CURLOPT_MAIL_FROM, mail->from);
	
	// add recipients
	const struct clib_string_array *list = mail->to_list;
	assert(list->length > 0);
	for(size_t ii = 0; ii < list->length; ++ii) {
		recipients = curl_slist_append(recipients, list->items[ii]);
	}
	
	list = mail->cc_list;
	for(size_t ii = 0; ii < list->length; ++ii) {
		recipients = curl_slist_append(recipients, list->items[ii]);
	}
	
	list = mail->bcc_list;
	for(size_t ii = 0; ii < list->length; ++ii) {
		recipients = curl_slist_append(recipients, list->items[ii]);
	}
	curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
	
	// upload mail payload
	curl_easy_setopt(curl, CURLOPT_READFUNCTION, on_upload_mail);
	curl_easy_setopt(curl, CURLOPT_READDATA, &closure);
	curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
	
	if(smtp->use_ssl || smtp->use_tls) {
		curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
	}
	
	CURLcode ret = curl_easy_perform(curl);
	curl_slist_free_all(recipients);
	
	if(ret != CURLE_OK) {
		rc = ret;
		fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(ret));
	}

	curl_easy_cleanup(curl);
	if(payload == &payload_buf) auto_buffer_cleanup(&payload_buf);
	return rc;
}

static int smtp_client_set_server_url(struct smtp_client *smtp, const char *server_url, int use_tls)
{
	smtp->use_tls = use_tls;
	smtp->use_ssl = 0;
	
	if(NULL == server_url) server_url = getenv("SMTP_SERVER_URL");
	if(NULL == server_url) return 1;
	
	int use_ssl = (strncasecmp(server_url, "smtps://", 8) == 0);
	if(!use_ssl && strncasecmp(server_url, "smtp://", 7) != 0) {
		fprintf(stderr, "invalid protol: url=%s\n", server_url);
		return -1;
	}
	
	if(smtp->server_url) {
		free(smtp->server_url);
		smtp->server_url = NULL;
	}
	smtp->server_url = strdup(server_url);
	smtp->use_ssl = use_ssl;
	
	return 0;
}

struct smtp_client *smtp_client_init(struct smtp_client *smtp, const char *smtp_server_url, int use_tls, void *user_data)
{
	if(NULL == smtp) smtp = calloc(1, sizeof(*smtp));
	assert(smtp);
	
	smtp->user_data = user_data;
	smtp->set_server_url = smtp_client_set_server_url;
	smtp->load_credential = smtp_client_load_credential;
	smtp->send = smtp_client_send;
	
	int rc = smtp_client_set_server_url(smtp, smtp_server_url, use_tls);
	assert(rc >= 0);
	
	return smtp;
}
void smtp_client_cleanup(struct smtp_client *smtp)
{
	if(smtp->server_url) {
		free(smtp->server_url);
		smtp->server_url = NULL;
	}
	
	memset(smtp->user, 0, sizeof(smtp->user));
	memset(smtp->secret, 0, sizeof(smtp->secret));
	
	return;
}


void smtp_mail_info_dump(const struct smtp_mail_info *mail, struct auto_buffer *payload, FILE *fp)
{
	assert(mail);
	if(NULL == fp) fp = stderr;
	
	int rc = 0;
	struct auto_buffer payload_buf = { 0 };
	if(NULL == payload) {
		payload = auto_buffer_init(&payload_buf, 4096);
		assert(payload);
	}
	
	if(payload->length == 0) {
		rc = smtp_mail_info_serialize((struct smtp_mail_info *)mail, payload, 0);
		assert(0 == rc);
	}
	
	ssize_t cb = fwrite(payload->data, 1, payload->length, fp);
	assert(cb > 0);
	
	if(payload == &payload_buf) auto_buffer_cleanup(&payload_buf);
	return;
}

#if defined(TEST_SMTP_CLIENT_) && defined(_STAND_ALONE)

#include <getopt.h>
static void print_usuage(const char *app_name)
{
	fprintf(stderr, "Usuage: %s (-H:T:C:B:S:M:F:f:c:h)\n"
		"    -H [--url=]  smtp://example.com:587 \n"
		"    -F [--from=] from@example.com \n"
		"    -T [--to=]   to@example.com \n"
		"    -C [--cc=]   cc.user1@example \n"
		"    -B [--bcc=]  bcc.user1@example.com \n"
		"    -S [--subject=] 'mail::subject' \n"
		"    -M [--message=] 'mail::body' \n"
		"    -f [--file=] 'mail::body_file.txt' \n"
		"    -c [--credentials=] credentials_file.json \n"
		"    -s [--charset=] charset (default: utf-8) \n"
		"    -h [--help=] \n"
		"\n", app_name);
	return;
}



#if defined(TEST_MIME_B64_ENCODE)
#include <iconv.h>
static void test_mime_b64_encode(const char *text, const char *charset, size_t block_size)
{
	int rc = 0;
	if(NULL == text || !text[0]) text = "こんにちは";
	if(NULL == charset || !charset[0]) charset = "UTF-8";
	
	size_t cb = 0;
	char *mime_text = NULL;
	
	size_t cb_text = strlen(text);
	assert(cb_text > 0);
	
	fprintf(stderr, "charset: %s, block_size: %ld\ntext: %s\n", charset, (long)block_size, text);
	
	if(strcasecmp(charset, "utf-8") != 0 && strcasecmp(charset, "utf8") != 0)
	{
		size_t in_size = cb_text + 1;
		size_t out_size = in_size * 2;
		
		printf("in size: %ld, out size: %ld\n", (long)in_size, (long)out_size);
		
		char *p_in = (char *)text;
		char *out_buf = calloc(out_size, 1);
		char *p_out = out_buf;
		
		iconv_t cd = iconv_open(charset, "utf-8");
		cb = iconv(cd, &p_in, &in_size, &p_out, &out_size);
		
		if(cb == -1) {
			perror("iconv");
			exit(1);
		}
		printf("in size: %ld, out size: %ld\n", (long)in_size, (long)out_size);
		
		ssize_t out_length = p_out - out_buf;
		fprintf(stderr, "input : %s\n", text);
		for(size_t i = 0; i < (cb_text + 1); ++i) {
			fprintf(stderr, "%.2x ", (unsigned char)text[i]);
		}
		fprintf(stderr, "\n");
		
		for(size_t i = 0; i < out_length; ++i) {
			fprintf(stderr, "%.2x ", (unsigned char)out_buf[i]);
		}
		fprintf(stderr, "\n");
		
		cb = mime_b64_encode(charset, out_buf, out_length, block_size, &mime_text);
		free(out_buf);
		
		iconv_close(cd);
	}else {
		cb = mime_b64_encode(charset, text, cb_text, block_size, &mime_text);
	}
	
	
	printf("cb: %ld, mime_text: %s\n", (long)cb, mime_text);
	free(mime_text);
	exit(rc);
}
#else 
#define test_mime_b64_encode(...) do { } while(0)
#endif


int main(int argc, char **argv)
{
	const char *charset = "UTF-8";
	
#if defined(TEST_MIME_B64_ENCODE)
	const char *text = "こんにちは";
	size_t block_size = 64;
	if(argc > 1) text = argv[1];
	if(argc > 2) charset = argv[2];
	if(argc > 3) block_size = atol(argv[3]);
	test_mime_b64_encode(text, charset, block_size);
	exit(0);
#endif

	int rc = 0;
	curl_global_init(CURL_GLOBAL_ALL);
	struct smtp_client smtp_buf[1];
	memset(smtp_buf, 0, sizeof(*smtp_buf));
	
	struct smtp_client *smtp = smtp_client_init(smtp_buf, NULL, 1, NULL);
	assert(smtp);
	
	struct smtp_mail_info mail_buf[1];
	memset(mail_buf, 0, sizeof(mail_buf));
	
	static struct option options[] = {
		{"url", required_argument, 0, 'H'},
		{"from", required_argument, 0, 'F'},
		{"to", required_argument, 0, 'T'},
		{"cc", required_argument, 0, 'C'},
		{"bcc", required_argument, 0, 'B'},
		{"subject", required_argument, 0, 'S'},
		{"message", required_argument, 0, 'M'},
		{"body-file", required_argument, 0, 'f'},
		{"credentials", required_argument, 0, 'c'},
		{"charset", required_argument, 0, 's'},
		{"help", no_argument, 0, 'h'},
		{NULL}
	};
	
	const char *server_url = NULL;
	const char *from = NULL;
	const char *credentials_file = NULL;
	const char *subject = NULL;
	char *body = NULL;
	ssize_t cb_body = 0;
	char *body_file = NULL;
	struct smtp_mail_info *mail = smtp_mail_info_init(mail_buf, NULL);
	while(1) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "H:T:C:B:S:M:F:f:c:h", options, &option_index);
		if(c == -1) break;
		switch(c) {
		case 'H': server_url = optarg; break;
		case 'F': from = optarg; break;
		case 'T': mail->to_list->push(mail->to_list, optarg); break;
		case 'C': mail->cc_list->push(mail->cc_list, optarg); break;
		case 'B': mail->bcc_list->push(mail->bcc_list, optarg); break;
		case 'S': subject = optarg; break;
		case 'M': body = optarg; break;
		case 'f': body_file = optarg; break;
		case 'c': credentials_file = optarg; break;
		case 's': charset = optarg; break;
		case 'h': print_usuage(argv[0]); exit(1); break;
		default:
			fprintf(stderr, "invalid arguments '%c'(%.2x)\n", c, (unsigned char)c);
			exit(1);
		}
	}
	
	rc = smtp->set_server_url(smtp, server_url, 1);
	assert(0 == rc);
	rc = smtp->load_credential(smtp, credentials_file, NULL);
	assert(0 == rc);
	
	if(NULL == from) from = getenv("SMTP_MAIL_FROM");
	assert(from);
	mail->from = strdup(from);
	
	if(subject) mail->subject = strdup(subject);
	mail->charset = charset;
	
	unsigned char *body_data = NULL;
	if(NULL == body) {
		fprintf(stderr, "body file: %s\n", body_file);
		if(body_file) {
			cb_body = load_binary_data(body_file, &body_data);
			assert(cb_body > 0);
			
			body = (char *)body_data;
		}
	}
	
	if(NULL == body) body = "test message\n";
	if(0 == cb_body) cb_body = strlen(body);
	
	auto_buffer_push(&mail->body, body, cb_body);
	if(body_data) { free(body_data); body_data = NULL; };
	


	struct auto_buffer payload[1];
	memset(payload, 0, sizeof(payload));
	smtp_mail_info_dump(mail, payload, stderr);
	
	rc = smtp->send(smtp, mail, payload);

	auto_buffer_cleanup(payload);
	
	smtp_mail_info_clear(mail);
	smtp_client_cleanup(smtp);
	
	curl_global_cleanup();
	return rc;
}
#endif

