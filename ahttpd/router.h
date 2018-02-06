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


struct ahttpd_route {
    enum ahttpd_method method;
    uint8_t *url;
    enum ahttpd_status (*handler)(struct ahttpd_request *);

    /* Opaque data pointer that will be injected into request->data */
    void *data;
    struct ahttpd_route *next;
};


struct ahttpd_route *ahttpd_route_new(
        enum http_method method,
        uint8_t *url,
        enum ahttpd_status (*handler)(struct ahttpd_request *));

void ahttpd_route_free(struct ahttpd_route *route);

esp_err_t ahttpd_init_routes(struct ahttpd_route *routes);

struct ahttpd_route *ahttpd_get_routes(void);

void ahttpd_router_404_handler(
        enum ahttpd_status (*handler)(struct ahttpd_request *));

enum ahttpd_status ahttpd_router(struct ahttpd_request *request);


#endif /* AHTTPD_ROUTER_H_ */
