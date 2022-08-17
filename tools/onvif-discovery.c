/*
 * onvif-discovery.c
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

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>	// ==> struct sockaddr_ll

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <json-c/json.h>

#define ONVIF_MULTICAST_IP    "239.255.255.250"
#define ONVIF_MULTICAST_PORT  3702

struct _xmlns
{
	char *key;
	char *value;
};

static const struct _xmlns soap_env_xmlns = {
	"SOAP-ENV", "http://schemas.xmlsoap.org/soap/envelope/"
};
static const struct _xmlns soap_wsa_xmlns = {
	"wsa", "http://schemas.xmlsoap.org/ws/2004/08/addressing"
	
};
static const struct _xmlns soap_wsdd_xmlns = {
	"wsdd", "http://schemas.xmlsoap.org/ws/2005/04/discovery"
};
static const struct _xmlns soap_enc_xmlns = {
	"SOAP-ENC", "http://www.w3.org/2003/05/soap-encoding"
};

static const struct _xmlns onvif_probe_matches_xmlns[] = {
	soap_env_xmlns,
	soap_wsa_xmlns,
	soap_wsdd_xmlns,
	soap_enc_xmlns
};




static ssize_t list_net_interfaces()
{
	int rc = 0;
	struct ifaddrs *ifaddrs = NULL;
	char host[NI_MAXHOST] = "";
	
	if(-1 == getifaddrs(&ifaddrs)) { 
		perror("getifaddrs"); 
		return -1; 
	}
	
	ssize_t num_interfaces = 0;
	
	for(struct ifaddrs *ifa = ifaddrs; NULL != ifa; ifa = ifa->ifa_next)
	{
		if(NULL == ifa->ifa_addr) continue;
	
		int family = ifa->ifa_addr->sa_family;
		struct sockaddr_ll *sll = NULL;
		
		printf("%s: \n", ifa->ifa_name);
		switch(family)
		{
		case AF_PACKET: // mac addr
			
			sll = (struct sockaddr_ll *)ifa->ifa_addr;
			printf(
				"  family=AF_PACKET, index=%d, hatype: %hu, pkttype: %hu, halen=%hu\n"
				"  macaddr=",
				sll->sll_ifindex, sll->sll_hatype, sll->sll_pkttype, sll->sll_halen);
			for(int i = 0; i < sll->sll_halen; ++i) {
				printf("%.2x", sll->sll_addr[i]);
			}
			printf("\n");
			
			if(ifa->ifa_data) {
				// struct rtnl_link_stats *link_stats = ifa->ifa_data;
				// dump bytes send/recv
			}
			break;
			
		case AF_INET:
		case AF_INET6: 
			rc = getnameinfo(ifa->ifa_addr, 
				(family==AF_INET)?sizeof(struct sockaddr_in):sizeof(struct sockaddr_in6),
				host, sizeof(host),
				NULL, 0, NI_NUMERICHOST);
			if(rc) {
				fprintf(stderr, "[ERROR]: getnameinfo() failed: %s\n",
					gai_strerror(rc));
				continue;
			}
			printf("  addr=%s\n", host);
			break;
		default:
			fprintf(stderr, "unknown family: %d\n", family);
			continue;
		}
		++num_interfaces;
	}
	
	return num_interfaces;
}

static char s_msg_envolop[] = 
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
"<SOAP-ENV:Envelope xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\">"
"<SOAP-ENV:Header>"
"	<Action xmlns=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\">"
	"http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe"
"	</Action>"
"	<MessageID xmlns=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\">"
	"urn:uuid:be3decdf-af24-4556-837a-68fb34a2fd79"
"	</MessageID>"
"	<To xmlns=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\">"
	"urn:schemas-xmlsoap-org:ws:2005:04:discovery"
"	</To>"
"</SOAP-ENV:Header>"
"<SOAP-ENV:Body>"
"<Probe xmlns=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\">"
"	<Types>"
	"http://www.onvif.org/ver10/network/wsdl:NetworkVideoTransmitter http://www.onvif.org/ver10/device/wsdl:Device"
"	</Types>"
"	<Scopes></Scopes>"
"</Probe>"
"</SOAP-ENV:Body>"
"</SOAP-ENV:Envelope>\n";
static int s_msg_envolop_length = sizeof(s_msg_envolop) - 1;

static int check_fault_code(xmlDocPtr doc)
{
	printf("== %s() ...\n", __FUNCTION__);
	int rc = 0;
	
	static const char * fault_code_path = "//SOAP-ENV:Envelope/SOAP-ENV:Body/SOAP-ENV:Fault/faultcode";
	xmlXPathContextPtr xpath_ctx = NULL;
	xpath_ctx = xmlXPathNewContext(doc);
	if(NULL == xpath_ctx) {
		fprintf(stderr, "xmlXPathNewContext(doc) failed\n");
		return -1;
	}
	xmlXPathRegisterNs(xpath_ctx, (xmlChar *)soap_env_xmlns.key, (xmlChar *)soap_env_xmlns.value);
	xmlXPathObject *fault_code = xmlXPathEval((xmlChar *)fault_code_path, xpath_ctx);
	if(NULL == fault_code) {
		xmlXPathFreeContext(xpath_ctx);
		return 0;
	}
	
	if(fault_code) {
		xmlNodeSetPtr nodes = fault_code->nodesetval;
		if(nodes) {
			for(int i = 0; i < nodes->nodeNr; ++i) {
				xmlNodePtr node = nodes->nodeTab[i];
				if(node->type == XML_NAMESPACE_DECL) { // todo: dump namespace
					
				}else if(node->type == XML_ELEMENT_NODE) {
					xmlChar * content = xmlNodeGetContent(node);
					fprintf(stderr, "fault code: %s\n", content);
					xmlFree(content);
				}else {
					printf("node: %s, type: %d\n", node->name, node->type);
				}
			}
		}
		xmlXPathFreeObject(fault_code);
		rc = 1;
	}
	xmlXPathFreeContext(xpath_ctx);
	
	return rc;
}

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

struct onvif_probe_matches
{
	// header
	char message_id[64]; /* //SOAP-ENV:Header/wsa:MessageId */
	char relates_to[64]; /* //SOAP-ENV:Header/wsa:RelatesTo */
	char to;
	char *action;
	
	// body
	int num_matches;
	struct onvif_match *matches;
};

#include <stdarg.h>

#define AUTO_FREE(type) AUTO_FREE_##type type
#define AUTO_FREE_IMPL(type, free_func) void auto_free_##type(void * p_ptr) { \
		type ptr = *(type *)p_ptr; \
		if(ptr) {  free_func(ptr); *(void **)ptr = NULL; } \
	}

#define AUTO_FREE_xmlDocPtr __attribute__((cleanup(auto_free_xmlDocPtr)))
AUTO_FREE_IMPL(xmlDocPtr, xmlFreeDoc)

typedef json_object *json_object_ptr; 
#define AUTO_FREE_json_object_ptr __attribute__((cleanup(auto_free_json_object_ptr)))
AUTO_FREE_IMPL(json_object_ptr, json_object_put)

#define AUTO_FREE_xmlXPathContextPtr __attribute__((cleanup(auto_free_xmlXPathContextPtr)))
AUTO_FREE_IMPL(xmlXPathContextPtr, xmlXPathFreeContext)

#define AUTO_FREE_xmlXPathObjectPtr __attribute__((cleanup(auto_free_xmlXPathObjectPtr)))
AUTO_FREE_IMPL(xmlXPathObjectPtr, xmlXPathFreeObject)


//~ int onvif_probe_matches_parse(struct onvif_probe_matches *probe_matches, xmlDoc *doc)
//~ {
	//~ static const xmlChar *message_id_path = (const xmlChar *)"wsa:MessageId";
	//~ static const xmlChar *relates_to_path = (const xmlChar *)"wsa:RelatesTo";
	
	//~ printf("==== %s ====\n", __FUNCTION__);

	//~ AUTO_FREE(xmlXPathContextPtr) xpath_ctx = xmlXPathNewContext(doc);
	//~ if(NULL == xpath_ctx) return -1;
	
	//~ AUTO_FREE(xmlXPathObjectPtr) header_tags = xmlXPathEval((xmlChar *)"//SOAP-ENV:Header", xpath_ctx);
	
	//~ assert(header_tags && header_tags->type == XPATH_NODESET && NULL != header_tags->nodesetval);
	//~ // parse headers;
	//~ int num_header_tags = header_tags->nodesetval->nodeNr;
	//~ assert(num_header_tags == 1);
	
	//~ xmlNodePtr header = header_tags->nodesetval->nodeTab[0];
	//~ assert(header->type == XML_ELEMENT_NODE);
	
	//~ printf("tag: %s\n", header->name);
	
	//~ xmlNodePtr current = header->children;
	//~ printf("name: %s, type: %d, content: %s\n", current->name, current->type, current->content);
	
	//~ return 0;
	
//~ }


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


static void add_xml_child_to_json(json_object *jparent, xmlNodePtr child)
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

static json_object * xml_to_json(xmlDocPtr doc)
{
	xmlNodePtr root = xmlDocGetRootElement(doc);
	if(NULL == root) return NULL;
	
	json_object * jroot = json_object_new_object();
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
	
	add_xml_child_to_json(jroot, root->children);
	return jroot;
}

int onvif_ws_discovery(const char *multicast_addr, unsigned int port)
{
	if(NULL == multicast_addr) multicast_addr = ONVIF_MULTICAST_IP;
	if(port == 0 || port > 65536) port = ONVIF_MULTICAST_PORT;
	
	printf("envolop: \n%s\n", s_msg_envolop);
	
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	assert(fd >= 0);
	
	// char msg_envolop[4096] = "";
	
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(multicast_addr);
	addr.sin_port = htons(port);
	
	if(NULL == s_msg_envolop) {
		
	}
	
	ssize_t cb_sent = sendto(fd, 
		s_msg_envolop, s_msg_envolop_length, 
		MSG_CONFIRM,
		(struct sockaddr *)&addr, sizeof(addr));
	printf("cb_sent: %ld\n", cb_sent);
	
	
	// make nonblock
	int rc = fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	assert(0 == rc);
	
	struct pollfd pfd[1] = {
		[0] = { .fd = fd, .events = POLLIN, }
	};
	char buf[4096] = "";
	
	
	const int timeout = 3000;
	int max_retries = 5;
	
	ssize_t cb_total = 0;
	while(max_retries-- > 0) {
		memset(buf, 0, sizeof(buf));
		int n = poll(pfd, 1, timeout);
		if(n <= 0) {
			if(n == 0) continue; // timeout
			rc = errno;
			perror("poll");
			return rc;
		}
		
		ssize_t cb_read = 0;
		
		struct sockaddr_storage ss[1];
		memset(ss, 0, sizeof(ss));
		socklen_t ss_len = sizeof(ss);
		cb_read = recvfrom(fd, 
			buf, sizeof(buf) - 1, 
			MSG_WAITALL, 
			(struct sockaddr *)&ss, &ss_len);
		
		if(cb_read <= 0) break;
		
		char host[NI_MAXHOST] = "";
		rc = getnameinfo((struct sockaddr *)ss, ss_len, 
			host, NI_MAXHOST, NULL, 0,
			NI_NUMERICHOST);
		if(0 == rc) {
			printf("== recv from: %s\n, cb_read = %ld\n", host, (long)cb_read);
		}
		
		// todo: parse xml result
		fprintf(stderr, "---- parse xml: \n%s\n----------------------\n", buf);
		
		xmlDoc *doc = xmlReadDoc((xmlChar *)buf, host, NULL, 0);
		if(NULL == doc) continue;
		
		rc = check_fault_code(doc);
		if(rc) {
			xmlFreeDoc(doc);
			continue;
		}
		
		AUTO_FREE(json_object_ptr) jresult = xml_to_json(doc);
		fprintf(stderr, "== ws-discovery response: \n%s\n",
			json_object_to_json_string_ext(jresult, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_NOSLASHESCAPE));
		
		//~ xmlXPathContextPtr xpath_ctx = NULL;
		//~ if(NULL == xpath_ctx) {
			//~ fprintf(stderr, "xmlXPathNewContext(doc) failed\n");
			//~ xmlFreeDoc(doc);
			//~ return -1;
		//~ }
		
		//~ // register onvif probe_matches ns
		//~ for(size_t ii = 0; ii < (sizeof(onvif_probe_matches_xmlns) / sizeof(onvif_probe_matches_xmlns[0])); ++ii)
		//~ {
			//~ xmlXPathRegisterNs(xpath_ctx, 
				//~ (xmlChar *)onvif_probe_matches_xmlns[ii].key, 
				//~ (xmlChar *)onvif_probe_matches_xmlns[ii].value);
		//~ }
		
		//~ struct onvif_probe_matches matches;
		//~ memset(&matches, 0, sizeof(matches));
		//~ onvif_probe_matches_parse(&matches, doc);
		
		//~ xmlXPathFreeContext(xpath_ctx);
		
		
		
		
		xmlFreeDoc(doc);
		
		cb_total += cb_read;
		//~ break;	
	}
	
	printf("result: \n%s\n", buf);
	return rc;
}



int main(int argc, char **argv)
{
	list_net_interfaces();
	onvif_ws_discovery(NULL, 0);
	
	return 0;
}

