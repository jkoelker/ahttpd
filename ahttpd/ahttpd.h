/*
 Copyright (c) 2018 Jason Kölker

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 */

#ifndef AHTTPD_HTTPD_H_
#define AHTTPD_HTTPD_H_

#include <stdint.h>
#include <stdio.h>
#include <lwip/tcp.h>
#include <lwip/ip_addr.h>

#include "http-parser/http_parser.h"

#ifndef AHTTPD_MAX_URL_SIZE
#define AHTTPD_MAX_URL_SIZE 256
#endif

#ifndef AHTTPD_MAX_HEADER_NAME_SIZE
#define AHTTPD_MAX_HEADER_NAME_SIZE 128
#endif

#ifndef AHTTPD_MAX_HEADER_VALUE_SIZE
#define AHTTPD_MAX_HEADER_VALUE_SIZE 256
#endif


enum ahttpd_method {
#define XX(num, name, string) AHTTPD_##name = num,
    HTTP_METHOD_MAP(XX)
#undef XX
    AHTTPD_ANY,
};


enum ahttpd_status {
    AHTTPD_MORE,
    AHTTPD_DONE,
    AHTTPD_NOT_FOUND,
};


struct ahttpd_request {
    enum ahttpd_method method;
    uint8_t *url;
    struct ahttpd_header *headers;
    const uint8_t *body;
    size_t body_len;

    enum ahttpd_status (*handler)(struct ahttpd_request *);

    /* data pointer for application use */
    void *data;

    /* internal state pointer */
    void *_state;
};


struct ahttpd_header {
    char *name;
    char *value;
    struct ahttpd_header *next;
};


struct ahttpd {
    struct tcp_pcb *_pcb;
    uint8_t *_bind_str;

    enum ahttpd_status (*router)(struct ahttpd_request *);
};


struct ahttpd_options {
    const ip_addr_t *ip_addr;
    uint16_t port;

    enum ahttpd_status (*router)(struct ahttpd_request *);
};


#define AHTTPD_OPTIONS_DEFAULT() { \
    .ip_addr = IP_ADDR_ANY, \
    .port = 80, \
    .router = NULL \
}


esp_err_t ahttpd_start(const struct ahttpd_options *options,
                       struct ahttpd **out_httpd);

esp_err_t ahttpd_stop(struct ahttpd *httpd);

void ahttpd_start_response(struct ahttpd_request *request, uint16_t code);

void ahttpd_send_header(struct ahttpd_request *request, const char *name,
                        const char *value);

void ahttpd_send_headers(struct ahttpd_request *request,
                         struct ahttpd_header *headers);

void ahttpd_end_headers(struct ahttpd_request *request);

void ahttpd_send(struct ahttpd_request *request, void *buf, size_t length);

void ahttpd_send_file(struct ahttpd_request *request, const char *pathname);

struct ahttpd_header *ahttpd_find_header(struct ahttpd_request *request,
                                         char *name);

ip_addr_t *ahttpd_remote_ip(struct ahttpd_request *request);

#endif /* AHTTPD_HTTPD_H_ */
