/*
 * check-json-format.c
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

#include <json-c/json.h>


int main(int argc, char **argv)
{
	const char *filename = NULL;
	
	json_tokener *jtok = json_tokener_new();
	for(int i = 1; i < argc; ++i) {
		filename = argv[i];
		
		int line_index = 0;
		char buf[4096] = "";
		char *line = NULL;
		
		printf("\e[33m==== filename: %s\e[39m\n", filename);
		FILE *fp = fopen(filename, "r");
		if(NULL == fp) {
			perror("open file");
			continue;
		}
		
		json_object *jobject = NULL;
		enum json_tokener_error jerr = json_tokener_error_parse_eof;
		
		while((line = fgets(buf, sizeof(buf) - 1, fp))) 
		{
			fprintf(stderr, "  parse line: %s", line);
			jobject = json_tokener_parse_ex(jtok, line, strlen(line));
			jerr = json_tokener_get_error(jtok);
			
			if(jerr != json_tokener_continue) break;
			++line_index;
			
		}
		
		if(jerr != json_tokener_success) {
			fprintf(stderr, "\e[31m" "ERROR(line=%d): %s" "\e[39m" "\n", line_index, json_tokener_error_desc(jerr));
		}else {
			fprintf(stderr, "\e[32m" "OK" "\e[39m" "\n");
		}
		
		json_tokener_reset(jtok);
		if(jobject) json_object_put(jobject);
	}
	
	json_tokener_free(jtok);
	return 0;
}

