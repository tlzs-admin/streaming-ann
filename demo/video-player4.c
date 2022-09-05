/*
 * video-player.c
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "app.h"
#include "ann-plugin.h"

#include <libintl.h>	// gettext()
#ifndef _
#define _(str) gettext(str)
#endif


#define TEXT_DOMAIN "demo"
static struct app_context g_app[1];
int main(int argc, char ** argv)
{
	ann_plugins_helpler_init(NULL, "plugins", NULL);
	
	struct app_context *app = g_app;
	app_context_init(app, NULL);
	
	char *domain_path = bindtextdomain(TEXT_DOMAIN, "langs");
	printf("langs.base_dir = %s\n", domain_path);
	
	// set domain for future gettext() calls 
	char *text_domain = textdomain(TEXT_DOMAIN);
	printf("text_domain: %s\n", text_domain);
	
	int rc = app->init(app, argc, argv);
	assert(0 == rc);
	rc = app->run(app);
	
	app_context_cleanup(app);
	return rc;
}
