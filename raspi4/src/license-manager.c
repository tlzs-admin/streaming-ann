/*
 * license-manager.c
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

#include "license-manager.h"
#include <secp256k1.h>

#include <sys/random.h>

#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>	// ==> struct sockaddr_ll
#include <unistd.h>
#include <time.h>
#include <netdb.h>

#include "utils.h"
#include "utils_crypto.h"

static void dump_mac_addr(const unsigned char mac_addr[static 6], FILE *fp)
{
	if(NULL == fp) fp = stdout;
	for(int i = 0; i < 6; ++i) {
		fprintf(fp, (i==0)?"%.2X":":%.2X", mac_addr[i]);
	}
}

//~ static void dump_ip_addr(const struct sockaddr *addr, socklen_t addr_len, FILE *fp)
//~ {
	//~ if(NULL == fp) fp = stdout;
	//~ if(0 == addr_len) {
		//~ fprintf(fp, "(empty)");
		//~ return;
	//~ }

	//~ char hbuf[NI_MAXHOST] = "";
	
	//~ int rc = getnameinfo(addr, addr_len, hbuf, sizeof(hbuf), NULL, 0, NI_NUMERICHOST);
	//~ assert(0 == rc);
	//~ fprintf(fp, "inet: %s", hbuf);
//~ }


#define IFADDRS_LIST_MAX_SIZE (32)
//~ struct ifaddr_data
//~ {
	//~ unsigned char mac_addr[6];
	//~ int index;
	//~ char name[64];
	//~ struct sockaddr_storage ip_addr; 
	//~ socklen_t addr_len; 
//~ };

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


struct license_manager
{
	void *user_data;
	secp256k1_context *secp;
	int has_privkey;
	int has_pubkey;
	uint8_t privkey[32];
	secp256k1_pubkey pubkey;
};

enum license_status license_record_generate_keypair(unsigned char secret[static 32], size_t cb_secret, uint8_t pubkey[static 64])
{
	assert(secret && pubkey);
	if(0 == cb_secret) cb_secret = getrandom(secret, 32, 0);
	assert(cb_secret == 32);
	
	secp256k1_context *secp = secp256k1_context_create(SECP256K1_FLAGS_TYPE_CONTEXT | SECP256K1_FLAGS_BIT_CONTEXT_SIGN);
	
	int retries = 10;
	while(retries-- > 0)
	{
		if(secp256k1_ec_seckey_verify(secp, secret)) break;
		
		cb_secret = getrandom(secret, 32, 0);
		if(cb_secret == -1) {
			perror("license_data_generate_keypair");
			secp256k1_context_destroy(secp);
			return -1;
		}
		assert(cb_secret == 32);
	}
	if(!secp256k1_ec_pubkey_create(secp, (secp256k1_pubkey *)pubkey, secret)) {
		secp256k1_context_destroy(secp);
		return -1;
	}
	return 0;
}

static ssize_t query_cpu_serial(unsigned char serial[static 16], size_t size)
{
	static const char command[4096] = "cat /proc/cpuinfo | grep -i Serial | cut -d ':' -d ' ' -f2";
	FILE *fp = popen(command, "r");
	int rc = 0;
	ssize_t cb_line = 0;
	if(fp) {
		char buf[1024] = "";
		char *line = fgets(buf, sizeof(buf) - 1, fp);
		if(line) {
			cb_line = strlen(line);
			if(cb_line > 0 && line[cb_line - 1] == '\n') line[--cb_line] = '\0';
			if(cb_line > 0) {
				if(cb_line > size) cb_line = size;
				memcpy(serial, line, cb_line);
			}
		}
		rc = pclose(fp);
		if(rc) {
			perror(__FUNCTION__);
			return -1;
		}
	}
	return cb_line;
}

struct license_record *license_record_init(struct license_record *record, const uint8_t pubkey[static 64], int64_t issued_at, int64_t expires)
{
	static const int64_t default_expires = 86400 * 90; // 90 days
	
	if(NULL == record) record = calloc(1, sizeof(*record));
	assert(record);
	
	if(pubkey) {
		memcpy(record->pubkey, pubkey, sizeof(record->pubkey));
	}
	
	struct timespec ts = { .tv_sec = 0 };
	if(issued_at <= 0) {
		clock_gettime(CLOCK_REALTIME, &ts);
		issued_at = ts.tv_sec;
	}
	if(expires <= 0) expires = default_expires;
	
	record->issued_at = issued_at;
	record->expires_at = issued_at + expires;
	
	struct ifaddr_data *addr_list = NULL;
	ssize_t num_addrs = query_mac_addrs(&addr_list);
	for(ssize_t i = 0; i < num_addrs; ++i) {
		struct ifaddr_data *addr = &addr_list[i];
		if(strncasecmp(addr->name, "eth", 3) == 0 
			|| strncasecmp(addr->name, "en", 2 == 0)
			|| strncasecmp(addr->name, "wlp", 3) == 0) {
			//~ fprintf(stderr, "default interface: %s, mac_addr: ", addr->name); dump_mac_addr(addr->mac_addr, stderr);  
			//~ fprintf(stderr, ", ip_addr: "); dump_ip_addr((struct sockaddr *)&addr->ip_addr, addr->addr_len, stderr);
			//~ fprintf(stderr, "\n");
			memcpy(record->mac_addr, addr->mac_addr, 6);
			break;
		}
	}
	
	// query cpu serial number (raspi only)
	record->cb_serial = query_cpu_serial(record->serial, sizeof(record->serial));
	
	return record;
}
void license_record_clear(struct license_record *record);


struct license_manager * license_manager_new(void *user_data)
{
	struct license_manager *license = calloc(1, sizeof(*license));
	license->user_data = user_data;
	
	if(license->secp) return license;
	
	secp256k1_context *secp = secp256k1_context_create(SECP256K1_FLAGS_TYPE_CONTEXT
		| SECP256K1_FLAGS_BIT_CONTEXT_SIGN 
		| SECP256K1_FLAGS_BIT_CONTEXT_VERIFY);
	assert(secp);
	license->secp = secp;
	
	return license;
}

void license_manager_clear_credentials(struct license_manager *license)
{
	memset(license->privkey, 0, 32);
	memset(&license->pubkey, 0, sizeof(license->pubkey));
	license->has_privkey = 0;
	license->has_pubkey = 0;
	return;
}

enum license_status license_manager_load_credentials(
	struct license_manager *license, 
	const unsigned char privkey[static 32],
	const unsigned char pubkey[static 64])
{
	assert(license && license->secp);
	int status = 0;
	
	license_manager_clear_credentials(license);
	secp256k1_pubkey *secp_pubkey = &license->pubkey;
	
	if(privkey) {
		memcpy(license->privkey, privkey, 32);
		license->has_privkey = 1;
		
		if(pubkey) {
			memcpy(secp_pubkey, pubkey, sizeof(*secp_pubkey));
		}else {
			status = secp256k1_ec_pubkey_create(license->secp, secp_pubkey, privkey);
		}
		
		if(status < 0) return license_status_failed;
		license->has_pubkey = 1;
		
		return license_status_ok;
	}
	
	if(pubkey) {
		memcpy(secp_pubkey, pubkey, sizeof(*secp_pubkey));
		license->has_pubkey = 1;
		return license_status_ok;
	}
	
	return license_status_failed;
}


void license_manager_free(struct license_manager *license)
{
	if(NULL == license) return;
	if(license->secp) {
		secp256k1_context_destroy(license->secp);
		license->secp = NULL;
	}
	license_manager_clear_credentials(license);
	free(license);
}


enum license_status license_manager_sign(struct license_manager *license, 
	const struct license_record *record,
	unsigned char signature_der[static 80], size_t *p_signature_length)
{
	assert(license && license->secp);
	assert(record);
	assert(signature_der && p_signature_length);
	
	int status = 0;
	secp256k1_ecdsa_signature sig;
	memset(&sig, 0, sizeof(sig));
	
	unsigned char digest[32] = { 0 };
	sha256_hash(record, sizeof(*record), digest);
	
	assert(license->has_privkey);
	status = secp256k1_ecdsa_sign(license->secp, &sig, digest, license->privkey, NULL, NULL);
	if(status != 1) return license_status_failed;
	
	status = secp256k1_ecdsa_signature_serialize_der(license->secp, signature_der, p_signature_length, &sig);
	if(status != 1) return license_status_failed;
	
	return license_status_signed;
}

enum license_status license_manager_verify(struct license_manager *license, 
	const struct license_record *record, 
	const unsigned char *signature_der, size_t signature_length)
{
	assert(license && license->secp);
	assert(record);
	assert(signature_der && signature_length > 0);
	
	int status = 0;
	secp256k1_ecdsa_signature sig;
	memset(&sig, 0, sizeof(sig));
	
	status = secp256k1_ecdsa_signature_parse_der(license->secp, &sig, signature_der, signature_length);
	if(status != 1) return license_status_failed;
	
	unsigned char digest[32] = { 0 };
	sha256_hash(record, sizeof(*record), digest);
	assert(license->has_pubkey);
	
	status = secp256k1_ecdsa_verify(license->secp, &sig, digest, &license->pubkey);
	if(status < 0) return license_status_failed;
	if(status == 0) return license_status_verify_failed;
	
	return license_status_verified;
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

void license_record_dump(const struct license_record *record, FILE *fp)
{
	static const char *RFC2822_COMPLIANT_FMT = "%a, %d %b %Y %T %z";
	if(NULL == fp) fp = stdout;
	dump_hex2(record->pubkey, 64, fp);
	dump_mac_addr(record->mac_addr, fp); fprintf(fp, "\n");
	
	char sz_time[100] = "";
	struct tm t[1];
	
	if(record->issued_at > 0) {
		memset(t, 0, sizeof(t));
		localtime_r((time_t *)&record->issued_at, t);
		strftime(sz_time, sizeof(sz_time), RFC2822_COMPLIANT_FMT, t);
		fprintf(fp, "issued at: %s\n", sz_time);
	}
	
	if(record->expires_at > 0) {
		memset(t, 0, sizeof(t));
		localtime_r((time_t *)&record->expires_at, t);
		strftime(sz_time, sizeof(sz_time), RFC2822_COMPLIANT_FMT, t);
		fprintf(fp, "expires at: %s\n", sz_time);
	}
	
	if(record->cb_serial > 0) {
		fprintf(stderr, "serial: %.*s\n", (int)record->cb_serial, record->serial);
	}
	return;
}


#if defined(TEST_LICENSE_MANAGER_) && defined(_STAND_ALONE)
static ssize_t load_ca_privkey(unsigned char privkey[static 32], const char *keyfile)
{
	ssize_t cb = 0;
	if(NULL == keyfile) keyfile = ".private/keys/ca-privkey.dat";
	FILE *fp = fopen(keyfile, "r");
	if(NULL == fp) return -1;
	
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
	if(NULL == fp) return -1;
	
	cb = fread(pubkey, 1, 64, fp);
	assert(cb == 64);
	fclose(fp);
	return cb;
}

static ssize_t save_to_file(const void *data, size_t length, const char *filename)
{
	ssize_t cb = -1;
	FILE *fp = fopen(filename, "wb");
	assert(fp);
	if(NULL == fp) return -1;
	
	cb = fwrite(data, 1, length, fp);
	if(cb == -1) {
		perror("save file");
	}
	fclose(fp);
	return cb;
}

int main(int argc, char **argv)
{
	struct license_record record[1];
	memset(record, 0, sizeof(record));
	
	unsigned char privkey[32] = { 0 };
	unsigned char pubkey[64] = { 0 };
	
	int status = 0;
	
	const char *privkey_file = "keys/client.privkey";
	const char *pubkey_file = "keys/client.pubkey";
	
	if(check_file(pubkey_file) != 0) {
		status = license_record_generate_keypair(privkey, 0, pubkey);
		assert(status >= 0);
		save_to_file(privkey, 32, privkey_file);
		save_to_file(pubkey, 64, pubkey_file);
	}else 
	{
		ssize_t cb = 0;
		unsigned char *p_data = privkey;
		cb = load_binary_data(privkey_file, &p_data);
		assert(cb == 32);
		
		p_data = pubkey;
		cb = load_binary_data(pubkey_file, &p_data);
		assert(cb == 64);
	}
	
	const char *record_file = "license/license.dat";
	if(check_file(record_file) != 0) {
		license_record_init(record, pubkey, 0, 0);
		FILE *fp = fopen(record_file, "w");
		assert(fp);
		fwrite(record, sizeof(struct license_record), 1, fp);
		fclose(fp);
	}else {
		unsigned char *p_data = (unsigned char *)record;
		ssize_t cb = load_binary_data(record_file, &p_data);
		assert(cb == sizeof(struct license_record));
	}
	license_record_dump(record, stderr);
	
	struct license_manager *admin = NULL;
	struct license_manager *client = NULL;
	
	unsigned char ca_privkey[32] = { 0 };
	unsigned char ca_pubkey[64] = { 0 };
	
	ssize_t cb = 0;
	cb = load_ca_privkey(ca_privkey, NULL);
	if(cb == 32) {
		admin = license_manager_new(NULL);
		license_manager_load_credentials(admin, ca_privkey, NULL);
	}
	
	cb = load_ca_pubkey(ca_pubkey, NULL);
	if(cb == 64) {
		client = license_manager_new(NULL);
		license_manager_load_credentials(client, NULL, ca_pubkey);
	}

	unsigned char sig[100] = "";
	size_t cb_sig = 0;
	
	// test sign
	if(admin) {
		cb_sig = sizeof(sig);
		status = license_manager_sign(admin, record, sig, &cb_sig);
		printf("sign status: %d\n", status);
		assert(status == license_status_signed);
		
		fprintf(stderr, "signature(DER): ");
		dump_hex2(sig, cb_sig, stderr);
	}
	
	// test verify
	if(client) {
		const char *sig_file = "sig.dat";
		if(0 == check_file(sig_file)) {
			unsigned char *p_data = sig;
			cb_sig = load_binary_data(sig_file, &p_data);
		}
		
		if(cb_sig > 0) {
			status = license_manager_verify(client, record, sig, cb_sig);
			printf("verify status: %d\n", status);
			assert(status == license_status_verified);
		}
	}
	
	if(admin) license_manager_free(admin);
	if(client) license_manager_free(client);

	return 0;
}
#endif

