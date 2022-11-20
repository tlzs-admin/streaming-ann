#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include <stdio.h>
#include <libsoup/soup.h>
#include <sys/socket.h>
#include <netdb.h>

#ifdef __cplusplus
extern "C" {
#endif

struct http_buffer
{
	size_t max_size;
	size_t length;
	size_t start_pos;
	char *data;
};
void http_buffer_clear(struct http_buffer *buffer);
int http_buffer_resize(struct http_buffer *buffer, size_t new_size);
char *http_buffer_get_data(const struct http_buffer *buffer);
int http_buffer_push_data(struct http_buffer *buffer, const void *data, size_t length);

enum http_stage
{
	http_stage_none = 0,
	http_stage_method_parsed = 1,
	http_stage_parse_request_headers,
	http_stage_parse_request_body,
	http_stage_parse_request_finished, 
	http_stage_send_response,
	http_stage_send_multipart,
	http_stage_final
};

struct http_server_context;
struct http_client_context
{
	int fd;
	struct sockaddr_storage addr;
	socklen_t addr_len;
	
	void *user_data;
	struct http_server_context *server;
	enum http_stage stage;
	int quit;
	int is_multipart;
	
	const char *method;
	char *path;
	GHashTable *query;
	char *protocol;
	SoupMessageHeaders *request_headers;
	struct http_buffer request_body[1];
	
	SoupMessageHeaders *response_headers;
	struct http_buffer response_body[1];
	
	
	int64_t timeout;
	// private data
	pthread_mutex_t mutex;
	struct http_buffer in_buf[1];
	struct http_buffer out_buf[1];
	int64_t timestamp_ms;
	
	// callbacks
	int (*on_read)(struct http_client_context *client, void *event);
	int (*on_write)(struct http_client_context *client, void *event);
	
	int (*on_http_request)(struct http_client_context *client, void *user_data);
	int (*on_destroy)(struct http_client_context *client, void *user_data);
	
};
struct http_client_context *http_client_context_new(int fd, struct http_server_context *server, void *user_data);
void http_client_context_free(struct http_client_context *peer);


#define HTTP_SERVER_MAX_LISTENING_FDS (64)
struct http_server_private;
struct http_server_context
{
	int server_fds[HTTP_SERVER_MAX_LISTENING_FDS];
	size_t num_fds;
	
	void *user_data;
	struct http_server_private *priv; ///< @todo
	int efd;	// epoll fd
	int use_ssl;
	int quit;
	
	pthread_mutex_t mutex;
	int async_mode;
	pthread_t th;

	size_t max_clients;
	ssize_t num_clients;
	struct http_client_context **clients;
	
	int (*listen)(struct http_server_context *http, const char *host, const char *port, int use_ssl);
	int (*run)(struct http_server_context *http, int async_mode);
	int (*stop)(struct http_server_context *http);
	
	int (*add_client)(struct http_server_context *http, struct http_client_context *client);
	int (*remove_client)(struct http_server_context *http, struct http_client_context *client);
	
	// callbacks
	int (*on_accepted)(struct http_server_context *http, struct http_client_context *client, void *event);
	int (*on_error)(struct http_server_context *http, void *event);
};
struct http_server_context *http_server_context_init(struct http_server_context *http, void *user_data);
void http_server_context_cleanup(struct http_server_context *http);

int http_server_set_client_writable(struct http_server_context *http, struct http_client_context *client, int writable);

#ifdef __cplusplus
}
#endif
#endif
