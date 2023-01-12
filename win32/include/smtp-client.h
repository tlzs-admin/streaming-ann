#ifndef SMTP_CLIENT_H_
#define SMTP_CLIENT_H_

#include <stdio.h>
#include <curl/curl.h>

#include "auto_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct clib_string_array
{
	size_t size;
	size_t length;
	char **items;
	int (*push)(struct clib_string_array *list, const char *text);
};
struct clib_string_array *clib_string_array_init(struct clib_string_array *sarray, size_t size);
void clib_string_array_clear(struct clib_string_array *sarray);

struct smtp_mail_info {
	char *from;
	struct clib_string_array to_list[1];
	struct clib_string_array cc_list[1];
	struct clib_string_array bcc_list[1];
	
	const char *charset;
	char *subject;
	size_t cb_subject;
	
	char *content_type;
	char *boundary;
	size_t cb_boundary;
	
	int is_multipart;
	struct clib_string_array headers[1];
	struct auto_buffer body;
	
	ssize_t num_parts;
	struct clib_string_array *part_headers;
	struct auto_buffer *parts;
};
int smtp_mail_info_serialize(struct smtp_mail_info *mail, struct auto_buffer *payload, time_t timestamp);

struct smtp_mail_info *smtp_mail_info_init(struct smtp_mail_info *mail, const char *from);
void smtp_mail_info_clear(struct smtp_mail_info *mail);

int smtp_mail_info_set_content_type(struct smtp_mail_info *mail, 
	const char *content_type, 
	const char *boundary);
int smtp_mail_info_add_part(struct smtp_mail_info *mail, 
	size_t num_headers, const char **headers, 
	const void *data, size_t cb_data);


struct smtp_client
{
	void *user_data;
	void *priv;
	
	char *server_url;
	char user[128];
	char secret[128];
	
	int use_ssl;
	int use_tls;
	
	int (*load_credential)(struct smtp_client *smtp, const char *credentials_file, const char *file_password);
	int (*set_server_url)(struct smtp_client *smtp, const char *server_url, int use_tls);
	int (*send)(struct smtp_client *smtp, const struct smtp_mail_info *mail, struct auto_buffer *payload);
};

struct smtp_client *smtp_client_init(struct smtp_client *smtp, const char *smtp_server_url, int use_tls, void *user_data);
void smtp_client_cleanup(struct smtp_client *smtp);

#ifdef __cplusplus
}
#endif
#endif
