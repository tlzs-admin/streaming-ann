#ifndef DEMO_WEBSERVER_UTILS_H_
#define DEMO_WEBSERVER_UTILS_H_

#include <stdio.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUTO_LOCKER(p_mutex) __attribute__((cleanup(auto_unlock_mutex))) pthread_mutex_t *_m_ = p_mutex; pthread_mutex_lock(_m_);

int make_sandbox(const char * document_root, uid_t real_uid, uid_t real_gid);
int verify_web_path(const char * path);
int get_user_id(const char * user_name, uid_t * p_uid, gid_t * p_gid);

struct page_data
{
	char * path; // key
	
	ssize_t length;
	unsigned char * data;
	
	char * path_name; // real path_name
	int is_dirty;
};
struct page_data * page_data_new(const char * path, ssize_t length, unsigned char * data, const char * path_name);
void page_data_clear(struct page_data * page);
void page_data_free(struct page_data * page);


struct pages_cache
{
	pthread_mutex_t mutex;
	char * document_root;
	uid_t uid;
	gid_t gid;
	
	int in_sandbox;
	
	void * search_root;
	void * user_data;
	
	int (* get)(struct pages_cache * pages, const char * path, const struct page_data ** p_page);
	int (* remove)(struct pages_cache * pages, const char * path);
	int (* update)(struct pages_cache * pages, const char * path, struct page_data * page);
	void (* clear_all)(struct pages_cache * pages);
};
struct pages_cache * pages_cache_init(struct pages_cache * pages, const char * document_root, uid_t uid, gid_t gid, void * user_data);
void pages_cache_cleanup(struct pages_cache * pages);

#ifdef __cplusplus
}
#endif
#endif
