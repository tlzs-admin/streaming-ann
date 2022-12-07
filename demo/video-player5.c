/*
 * video-player4.c
 * 
 * Copyright 2022 chehw <hongwei.che@gmail.com>
 * 
 * The MIT License (MIT)
 * 
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
 * 
 */

/* LANGUAGE=ja_JP:ja ./video-player4 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <locale.h>
#include "app.h"
#include "ann-plugin.h"

#include "utils.h"
#include "utils_crypto.h"
#include "license-manager.h"
static enum license_status check_license(struct app_context *app);

#define TEXT_DOMAIN "demo"
static struct app_context g_app[1];
int main(int argc, char ** argv)
{
	ann_plugins_helpler_init(NULL, "plugins", NULL);
	setlocale(LC_ALL,"");
	struct app_context *app = g_app;
	app_context_init(app, NULL);
	
	if(check_license(app) != license_status_verified) {
		fprintf(stderr, "check license failed\n");
		return -1;
	}
	
	
	char lang_path[PATH_MAX + 100] = "";
	snprintf(lang_path, sizeof(lang_path), "%s/langs", app->work_dir); 
	
	char *domain_path = bindtextdomain(TEXT_DOMAIN, lang_path);
	printf("langs.base_dir = %s\n", domain_path);
	
	// set domain for future gettext() calls 
	char *text_domain = textdomain(TEXT_DOMAIN);
	printf("text_domain: %s\n", text_domain);
	
	printf(_("Settings"));
	int rc = app->init(app, argc, argv);
	assert(0 == rc);
	
	
	rc = app->run(app);
	
	app_context_cleanup(app);
	return rc;
}

static int load_ca_pubkey(unsigned char pubkey[static 64])
{
	static unsigned char ca_root_hash[32] = {
		0x29,0x0d,0x08,0x33,0xf0,0x05,0xd5,0xc9,
		0xe5,0x7c,0xe1,0xec,0x84,0xde,0x5a,0xc4,
		0x7c,0x3f,0xaa,0xa0,0x26,0x30,0x08,0x5e,
		0x3e,0x26,0x97,0xba,0xaa,0xe5,0x5d,0x72
	};
	static const char *pubkey_file = "keys/ca-pubkey.dat";
	unsigned char *p_data = NULL;
	ssize_t cb = load_binary_data(pubkey_file, &p_data);
	assert(cb == 64);
	memcpy(pubkey, p_data, 64);
	
	
	unsigned char hash[32] = { 0 };
	sha256_hash(pubkey, 64, hash);
	
	if(memcmp(hash, ca_root_hash, 32) != 0) {
		fprintf(stderr, "invalid root ca\n");
		return -1;
	}
	
	memset(p_data, 0, 64);
	free(p_data);
	return 0;
}

ssize_t decrypt_signature(const unsigned char ca_pubkey[static 64], 
	const unsigned char *encrypted_sig, 
	size_t cb_encrypted_sig,
	unsigned char sig[static 80])
{
	///<@ todo: use ca_pubkey to decrypt the sig_data, if failed, then the ca_pubkey is fake.
	ssize_t cb_sig = cb_encrypted_sig;
	assert(cb_sig < 80);
	memcpy(sig, encrypted_sig, cb_sig);
	return cb_sig;
}

static void dump_mac_addr(const unsigned char mac_addr[static 6], FILE *fp)
{
	if(NULL == fp) fp = stdout;
	for(int i = 0; i < 6; ++i) {
		fprintf(fp, (i==0)?"%.2X":":%.2X", mac_addr[i]);
	}
}
static int check_license(struct app_context *app)
{
	struct license_manager *mgr = license_manager_new(NULL);
	assert(mgr);
	
	int rc = 0;
	unsigned char ca_pubkey[64] = { 0 };
	rc = load_ca_pubkey(ca_pubkey);
	if(rc != 0) return -1;
	
	rc = license_manager_load_credentials(mgr, NULL, ca_pubkey);
	assert(0 == rc);
	
	struct license_record record[1];
	memset(record, 0, sizeof(record));
	unsigned char sig[128] = { 0 };
	ssize_t cb_sig = 0;
	
	const char *license_dat_sig_file = "license/license.dat_sig";
	
	if(check_file(license_dat_sig_file) != 0) {
		
		rc = system("./license-manager");
		if(rc) {
			///< @todo : error handler
		}
		// show help 
		system("cat license/README.md");
		return -1;
	}
	
	unsigned char *p_data = NULL;
	ssize_t cb_data = load_binary_data(license_dat_sig_file, &p_data);
	assert(cb_data > sizeof(struct license_record));
	memcpy(record, p_data, sizeof(struct license_record));
	
	
	struct ifaddr_data *addrs = NULL;
	ssize_t num_addrs = query_mac_addrs(&addrs);
	assert(num_addrs > 0);
	
	fprintf(stderr, "record.mac_addr: ");
	dump_mac_addr(record->mac_addr, stderr);
	fprintf(stderr, "\n");
	
	int ok = 0;
	for(ssize_t i = 0; i < num_addrs; ++i) {
		fprintf(stderr, "addrs[%d]: ", (int)i);
		dump_mac_addr(addrs[i].mac_addr, stderr);
		fprintf(stderr, "\n");
		if(memcmp(addrs[i].mac_addr, record->mac_addr, 6) == 0) {
			ok = 1;
			break;
		}
	}
	if(!ok) {
		fprintf(stderr, "invalid license data (invalid mac addr)\n");
		return -1;
	}
	dump_mac_addr(record->mac_addr, stderr);
	fprintf(stderr, "\n");
	
	unsigned char *encrypted_sig = p_data + sizeof(struct license_record);
	ssize_t cb_encrypted_sig = cb_data - sizeof(struct license_record);
	
	///<@ todo: use ca_pubkey to decrypt the sig_data, if failed, then the ca_pubkey is fake.
	cb_sig = decrypt_signature(ca_pubkey, encrypted_sig, cb_encrypted_sig, sig);
	assert(cb_sig < 80);
	
	enum license_status status = license_manager_verify(mgr, record, sig, cb_sig);
	if(status != license_status_verified) return -1;
	
	
	
	
	return status;
}
