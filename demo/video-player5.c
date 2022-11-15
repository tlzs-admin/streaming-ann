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

#define TEXT_DOMAIN "demo"
static struct app_context g_app[1];
int main(int argc, char ** argv)
{
	ann_plugins_helpler_init(NULL, "plugins", NULL);
	
	setlocale(LC_ALL,"");
	struct app_context *app = g_app;
	app_context_init(app, NULL);
	
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
