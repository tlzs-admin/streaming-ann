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

typedef char * string;
#define json_get_value(jobj, type, key)	({									\
		type value = (type)0;												\
		if (jobj) {															\
			json_object * jvalue = NULL;									\
			json_bool ok = FALSE;											\
			ok = json_object_object_get_ex(jobj, #key, &jvalue);			\
			if(ok && jvalue) value = (type)json_object_get_##type(jvalue);	\
		}																	\
		value;																\
	})

#define json_get_value_default(jobj, type, key, defval)	({					\
		type value = (type)defval;											\
		json_object * jvalue = NULL;										\
		json_bool ok = FALSE;												\
		ok = json_object_object_get_ex(jobj, #key, &jvalue);				\
		if(ok && jvalue) value = (type)json_object_get_##type(jvalue);		\
		value;																\
	})
	
struct onvif_match
{
	struct {
		char address[64]; 
	} endpoint_reference;
	char * types;
	char * scopes;
	char * service_addrs;
	char * metadata_version;
};
void onvif_match_cleanup(struct onvif_match * match)
{
	if(match->types) free(match->types);
	if(match->scopes) free(match->scopes);
	if(match->service_addrs) free(match->service_addrs);
	if(match->metadata_version) free(match->metadata_version);
	memset(match, 0, sizeof(*match));
}

struct onvif_probe_matches
{
	// header
	char message_id[64]; /* //SOAP-ENV:Header/wsa:MessageId */
	char relates_to[64]; /* //SOAP-ENV:Header/wsa:RelatesTo */
	char *to;
	char *action;
	
	// body
	int num_matches;
	struct onvif_match *matches;
};
void onvif_probe_matches_cleanup(struct onvif_probe_matches * probe_matches)
{
	if(probe_matches->to) free(probe_matches->to);
	if(probe_matches->action) free(probe_matches->action);
	
	if(probe_matches->num_matches > 0 && probe_matches->matches) {
		for(int i = 0; i < probe_matches->num_matches; ++i) {
			onvif_match_cleanup(&probe_matches->matches[i]);
		}
	}
	if(probe_matches->matches) free(probe_matches->matches);
	memset(probe_matches, 0, sizeof(*probe_matches));
	
	return;
}

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

void onvif_probe_matches_dump(const struct onvif_probe_matches * probe_matches);
int onvif_probe_matches_parse(struct onvif_probe_matches *probe_matches, json_object *jresult);
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
	
	json_object_to_file_ext("xml-result.json", jroot, 
		JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE);
	
	struct onvif_probe_matches matches;
	memset(&matches, 0, sizeof(matches));
		
	onvif_probe_matches_parse(&matches, jroot);
	onvif_probe_matches_dump(&matches);
	
	onvif_probe_matches_cleanup(&matches);
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

static int onvif_probe_matches_parse_header(
	struct onvif_probe_matches * probe_matches, 
	json_object *jheader)
{
	const char *message_id = NULL;
	const char *relates_to = NULL;
	const char *to = NULL;
	const char *action = NULL;
	
	json_object *jchildren = NULL;
	json_bool ok = json_object_object_get_ex(jheader, "children", &jchildren);
	if(!ok || NULL == jchildren) return -1;
	
	int num_children = json_object_array_length(jchildren);
	for(int i = 0; i < num_children; ++i) {
		json_object *jchild = json_object_array_get_idx(jchildren, i);
		if(NULL == jchild) continue;
		
		const char *name = json_get_value(jchild, string, name);
		if(NULL == name) continue;
		
		if(strcasecmp(name, "MessageID") == 0) message_id = json_get_value(jchild, string, content);
		else if(strcasecmp(name, "RelatesTo") == 0) relates_to = json_get_value(jchild, string, content);
		else if(strcasecmp(name, "To") == 0) to = json_get_value(jchild, string, content);
		else if(strcasecmp(name, "Action") == 0) action = json_get_value(jchild, string, content);
	}
	
	if(message_id) {
		char *uuid = strstr(message_id, "uuid:");
		if(uuid) {
			uuid += sizeof("uuid:") - 1;
			strncpy(probe_matches->message_id, uuid, sizeof(probe_matches->message_id));
		}
	}
	
	if(relates_to) {
		char *uuid = strstr(message_id, "uuid:");
		if(uuid) {
			uuid += sizeof("uuid:") - 1;
			strncpy(probe_matches->relates_to, uuid, sizeof(probe_matches->relates_to));
		}
	}
	
	if(to) probe_matches->to = strdup(to);
	if(action) probe_matches->action = strdup(action);
	
	return 0;
}

static int onvif_probe_matches_parse_body(
	struct onvif_probe_matches * probe_matches, 
	json_object *jbody)
{
	json_object *jprobe_matches = NULL;
	json_object *jchildren = NULL;
	json_bool ok = json_object_object_get_ex(jbody, "children", &jchildren);
	if(!ok || NULL == jchildren) return -1;
	
	int num_children = json_object_array_length(jchildren);
	if(num_children < 1) return -1;
	
	for(int i = 0; i < num_children; ++i) {
		json_object *jchild = json_object_array_get_idx(jchildren, i);
		if(NULL == jchild) continue;
		const char * name = json_get_value(jchild, string, name);
		if(NULL == name) continue;
		
		if(strcasecmp(name, "ProbeMatches") == 0) {
			jprobe_matches = jchild;
			break;
		}
	}
	
	
	if(NULL == jprobe_matches) return -1;
	
	ok = json_object_object_get_ex(jprobe_matches, "children", &jchildren);
	if(!ok || NULL == jchildren) return -1;
	int num_matches = json_object_array_length(jchildren);
	if(num_matches < 1) return -1;
	
	struct onvif_match *matches = calloc(num_matches, sizeof(*matches));
	for(int i = 0; i < num_matches; ++i) {
		json_object *jmatch = json_object_array_get_idx(jchildren, i);
		if(NULL == jmatch) continue;
		
		json_object * jfields = NULL;
		ok = json_object_object_get_ex(jmatch, "children", &jfields);
		if(!ok || NULL == jfields) continue;
		int num_fields = json_object_array_length(jfields);
		for(int ii = 0; ii < num_fields; ++ii) {
			json_object *jfield = json_object_array_get_idx(jfields, ii);
			if(NULL == jfield) continue;
			
			const char *name = json_get_value(jfield, string, name);
			if(NULL == name) continue;
			const char *content = json_get_value(jfield, string, content);
			if(NULL == content) continue;
			
			if(strcasecmp(name, "EndpointReference") == 0) { } // todo ...
			else if(strcasecmp(name, "Types") == 0) matches[i].types = strdup(content);
			else if(strcasecmp(name, "Scopes") == 0) matches[i].scopes = strdup(content);
			else if(strcasecmp(name, "XAddrs") == 0) matches[i].service_addrs = strdup(content);
			else if(strcasecmp(name, "MetadataVersion") == 0) matches[i].metadata_version = strdup(content);
		}	
	}
	probe_matches->num_matches = num_matches;
	probe_matches->matches = matches;

	return 0;
}

void onvif_probe_matches_dump(const struct onvif_probe_matches * probe_matches)
{
	printf("MessageID: %s\n", probe_matches->message_id);
	printf("RelatesTo: %s\n", probe_matches->relates_to);
	printf("To: %s\n", probe_matches->to);
	printf("Action: %s\n", probe_matches->action);
	
	printf("== num matches: %d\n", (int)probe_matches->num_matches);
	for(int i = 0; i < probe_matches->num_matches; ++i) {
		printf("  [%d]: \n", i);
		printf("    Types=%s\n", probe_matches->matches[i].types);
		printf("    Scopes=%s\n", probe_matches->matches[i].scopes);
		printf("    XAddrs=%s\n", probe_matches->matches[i].service_addrs);
		printf("    MetadataVersion=%s\n", probe_matches->matches[i].metadata_version);
	}
}

int onvif_probe_matches_parse(struct onvif_probe_matches *probe_matches, json_object *jresult)
{
	json_object *jheader = NULL;
	json_object *jbody = NULL;
	
	json_object *jchildren = NULL;
	json_bool ok = json_object_object_get_ex(jresult, "children", &jchildren);
	
	if(!ok || NULL == jchildren) return -1;
	
	int num_children = json_object_array_length(jchildren);
	for(int i = 0; i < num_children; ++i) {
		json_object *jchild = json_object_array_get_idx(jchildren, i);
		if(NULL == jchild) continue;
		
		const char *name = json_get_value(jchild, string, name);
		if(NULL == name) continue;
		
		if(strcasecmp(name, "Header") == 0) jheader = jchild;
		else if(strcasecmp(name, "Body") == 0) jbody = jchild;
	}
	
	if(NULL == jheader || NULL == jbody) return -1;
	
	
	int rc = onvif_probe_matches_parse_header(probe_matches, jheader);
	if(rc) return rc;
	
	rc = onvif_probe_matches_parse_body(probe_matches, jbody); 
	return rc;
}
