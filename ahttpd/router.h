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

#ifndef AHTTPD_ROUTER_H_
#define AHTTPD_ROUTER_H_

#include <esp_err.h>
#include <stdint.h>

#include <ahttpd/ahttpd.h>


#define AHTTPD_ADD_ROUTE(routes, route) \
    do { \
        struct ahttpd_route *__r; \
        (route)->next = NULL; \
        if (routes) { \
            __r = (routes); \
            while (__r->next) { __r = __r->next; } \
            __r->next = (route); \
        } else { \
            (routes) = (route); \
        } \
    } while (0)


#define AHTTPD_ROUTE(routes, method, url, handler, data) \
    AHTTPD_ADD_ROUTE(routes, &((struct ahttpd_route) { \
        (method), (url), (handler), (data), NULL })); \


#define AHTTPD_REDIRECT(routes, url, dest) \
    do { \
        size_t __dest_len = strlen(dest) + 1; \
        char *__dest = calloc(__dest_len, sizeof(*__dest)); \
        snprintf(__dest, __dest_len, "%s", dest); \
        AHTTPD_ROUTE(routes, AHTTPD_ANY, url, (&ahttpd_redirect), __dest); \
    } while (0)


struct ahttpd_route {
    enum ahttpd_method method;
    char *url;
    enum ahttpd_status (*handler)(struct ahttpd_request *);

    /* Opaque data pointer that will be injected into request->data */
    void *data;
    struct ahttpd_route *next;
};


struct ahttpd_route *ahttpd_route_new(
        enum ahttpd_method method,
        char *url,
        enum ahttpd_status (*handler)(struct ahttpd_request *),
        void *data);

void ahttpd_route_free(struct ahttpd_route *route);

esp_err_t ahttpd_router_init(struct ahttpd_route *routes);

struct ahttpd_route *ahttpd_get_routes(void);

void ahttpd_router_404_handler(
        enum ahttpd_status (*handler)(struct ahttpd_request *));

enum ahttpd_status ahttpd_router(struct ahttpd_request *request);

/* Redirects to the url in request->data */
enum ahttpd_status ahttpd_redirect(struct ahttpd_request *request);


#endif /* AHTTPD_ROUTER_H_ */
