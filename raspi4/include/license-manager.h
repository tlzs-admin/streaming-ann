#ifndef LICENSE_MGR_H_
#define LICENSE_MGR_H_

#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


enum license_status
{
	license_status_failed = -1,
	license_status_ok = 0,
	
	license_status_verified = 1, 
	license_status_signed = 1,
	
	license_status_verify_failed = 2,
};

struct license_manager;
struct license_record
{
	uint8_t pubkey[64];
	uint8_t mac_addr[6 + 2]; // append 2 bytes padding
	uint32_t cb_serial;
	uint8_t serial[40];
	
	uint64_t flags;
	int64_t issued_at; 		// timestamp
	int64_t expires_at;		// timestamp
	uint64_t cb_data;
	uint8_t user_data[];
};
void license_record_dump(const struct license_record *record, FILE *fp);

enum license_status license_data_generate_keypair(unsigned char secret[static 32], size_t cb_secret, uint8_t pubkey[static 64]);

struct license_record *license_record_init(struct license_record *record, const uint8_t pubkey[static 64], int64_t issued_at, int64_t expires);
void license_record_clear(struct license_record *record);

ssize_t license_data_ecdh_secret(
	const unsigned char privkey[static 32], 
	const uint8_t peers_pubkey[static 64], 
	const unsigned char *salt, size_t cb_salt, 
	uint8_t ecdh_secret[static 64]);
ssize_t license_record_encrypt(const struct license_record *record, const unsigned *ecdh_secret, unsigned char **p_encrypted_record);
int license_record_decrypt(const unsigned char *encrypted_record, size_t length, struct license_record record[static 1]);

enum license_status license_manager_sign(struct license_manager *license, 
	const struct license_record *data,
	unsigned char signature_der[static 80], size_t *p_signature_length);

enum license_status license_manager_verify(struct license_manager *license, 
	const struct license_record *data, 
	const unsigned char signature_der[], size_t signature_length);

enum license_status license_manager_load_credentials(
	struct license_manager *license, 
	const unsigned char privkey[static 32],
	const unsigned char pubkey[static 64]);

void license_manager_clear_credentials(struct license_manager *license);

struct license_manager * license_manager_new(void *user_data);
void license_manager_free(struct license_manager *license);

#ifdef __cplusplus
}
#endif
#endif
