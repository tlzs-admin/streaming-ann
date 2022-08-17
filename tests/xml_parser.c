/*
 * xml_parser.c
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

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>


#define AUTO_FREE(type) AUTO_FREE_##type type
#define AUTO_FREE_IMPL(type, free_func) static void auto_free_##type(void * p_ptr) { \
		type ptr = *(type *)p_ptr; \
		if(ptr) {  free_func(ptr); *(void **)ptr = NULL; } \
	}

#define AUTO_FREE_xmlDocPtr __attribute__((cleanup(auto_free_xmlDocPtr)))
AUTO_FREE_IMPL(xmlDocPtr, xmlFreeDoc)

typedef json_object *json_object_ptr; 
#define AUTO_FREE_json_object_ptr __attribute__((cleanup(auto_free_json_object_ptr)))
AUTO_FREE_IMPL(json_object_ptr, json_object_put)


static const char * s_xml_types[] = {
	[0] = "UNKNOWN_XML_TYPE",
	[XML_ELEMENT_NODE] = "XML_ELEMENT_NODE", 
    [XML_ATTRIBUTE_NODE] = "XML_ATTRIBUTE_NODE", 
    [XML_TEXT_NODE] = "XML_TEXT_NODE", 
    [XML_CDATA_SECTION_NODE] = "XML_CDATA_SECTION_NODE", 
    [XML_ENTITY_REF_NODE] = "XML_ENTITY_REF_NODE", 
    [XML_ENTITY_NODE] = "XML_ENTITY_NODE", 
    [XML_PI_NODE] = "XML_PI_NODE", 
    [XML_COMMENT_NODE] = "XML_COMMENT_NODE", 
    [XML_DOCUMENT_NODE] = "XML_DOCUMENT_NODE", 
    [XML_DOCUMENT_TYPE_NODE] = "XML_DOCUMENT_TYPE_NODE", 
    [XML_DOCUMENT_FRAG_NODE] = "XML_DOCUMENT_FRAG_NODE", 
    [XML_NOTATION_NODE] = "XML_NOTATION_NODE", 
    [XML_HTML_DOCUMENT_NODE] = "XML_HTML_DOCUMENT_NODE", 
    [XML_DTD_NODE] = "XML_DTD_NODE", 
    [XML_ELEMENT_DECL] = "XML_ELEMENT_DECL", 
    [XML_ATTRIBUTE_DECL] = "XML_ATTRIBUTE_DECL", 
    [XML_ENTITY_DECL] = "XML_ENTITY_DECL", 
    [XML_NAMESPACE_DECL] = "XML_NAMESPACE_DECL", 
    [XML_XINCLUDE_START] = "XML_XINCLUDE_START", 
    [XML_XINCLUDE_END] = "XML_XINCLUDE_END", 
    [XML_DOCB_DOCUMENT_NODE] = "XML_DOCB_DOCUMENT_NODE", 
};

static const char *xml_type_to_string(xmlElementType type)
{
	if(type < 0 || type > XML_DOCB_DOCUMENT_NODE) type = 0;
	return s_xml_types[type];
}


/**
 * build: 
 *   gcc -std=gnu99 -g -Wall -o xml_parser xml_parser.c $(pkg-config --cflags --libs libxml-2.0 json-c)
 */

int main(int argc, char **argv)
{
	// parse onvif device discovery result
	const char * xml_file = "xml/ProbeMatches.xml";
	if(argc > 1) xml_file = argv[1];
	
	AUTO_FREE(xmlDocPtr) doc = NULL;
	doc = xmlParseFile(xml_file);
	assert(doc);
	
	xmlDocDump(stdout, doc);
	xmlNodePtr root = xmlDocGetRootElement(doc);
	
	printf("doc: %p (name=%s)\nchildren=%p, parent=%p, prev=%p, next=%p, \nroot=%p\n",
		doc, doc->name, 
		doc->children, doc->parent, doc->prev, doc->next,
		root);
		
	printf("root: name=%s, type: %s, ns=%s(href=%s)\n", 
		root->name,
		xml_type_to_string(root->type),
		root->ns?(char*)root->ns->prefix: "(null)",
		root->ns?(char*)root->ns->href: "(null)"
		);

	AUTO_FREE(json_object_ptr) jroot = json_object_new_object();
	json_object_object_add(jroot, "name", json_object_new_string((char*)root->name));
	json_object_object_add(jroot, "type", json_object_new_int(root->type));
	json_object_object_add(jroot, "type_str", json_object_new_string(xml_type_to_string(root->type)));
	json_object_object_add(jroot, "ns", json_object_new_string(root->ns?(char*)root->ns->prefix:""));
	
	
	json_object * jns_defs = json_object_new_array();
	json_object_object_add(jroot, "ns_defs", jns_defs);

	xmlNsPtr ns_def = root->nsDef;
	printf("== ns defines ==\n");
	while(ns_def) {
		json_object * jns = json_object_new_object();
		json_object_object_add(jns, "prefix", json_object_new_string((char *)ns_def->prefix));
		json_object_object_add(jns, "href", json_object_new_string((char *)ns_def->href));
		json_object_array_add(jns_defs, jns);
		
		printf("%s : %s\n", ns_def->prefix, ns_def->href);
		ns_def = ns_def->next;
	}
	
	
	
	void add_xml_child_to_json(json_object *jparent, xmlNodePtr child);
	add_xml_child_to_json(jroot, root->children);
	
	//~ fprintf(stderr, "json_str: %s\n", 
		//~ json_object_to_json_string_ext(jroot, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE)
	//~ );
	
	json_object_to_file_ext("xml-result.json", jroot, 
		JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE);
	return 0;
}

void add_xml_child_to_json(json_object *jparent, xmlNodePtr child)
{
	if(NULL == child) return;
	json_object *jchildren = json_object_new_array();
	json_object_object_add(jparent, "children", jchildren);
	
	while(child) {
		json_object *jchild = json_object_new_object();
		
		json_object_object_add(jchild, "name", json_object_new_string((char*)child->name));
		json_object_object_add(jchild, "type", json_object_new_int(child->type));
		json_object_object_add(jchild, "type_str", json_object_new_string(xml_type_to_string(child->type)));
		json_object_object_add(jchild, "ns", json_object_new_string(child->ns?(char*)child->ns->prefix:""));
		json_object_array_add(jchildren, jchild);
		
		if(child->children && child->children->type == XML_TEXT_NODE) {
			xmlNodePtr text_node = child->children;
			json_object_object_add(jchild, "content", 
				json_object_new_string(text_node->content?(char*)text_node->content:""));
			
			if(child->type == XML_TEXT_NODE) {
				printf("text node: name=%s, content=%s\n", 
					(char *)child->name,
					(char *)text_node->content);
			}
		}else {
			if(child->children) add_xml_child_to_json(jchild, child->children);
		}
		
		child = child->next;
	}
	return;
}

