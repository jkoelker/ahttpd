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

#include <esp_err.h>
#include <esp_log.h>

#include <string.h>
#include <stdint.h>

#include "ahttpd/ahttpd.h"
#include "ahttpd/router.h"


static const char* TAG = "ahttpd-router";

static struct ahttpd_route *_routes = NULL;
static enum ahttpd_status (*_404)(struct ahttpd_request *) = NULL;


static enum ahttpd_status ahttpd_404(struct ahttpd_request *request) {
    if (_404 != NULL) {
        request->handler = _404;
        return _404(request);
    }

    uint8_t body[] = "Not Found";
    ahttpd_start_response(request, 404);
    ahttpd_send_header(request, "Server", "AHTTPD/1.0");
    ahttpd_end_headers(request);
    ahttpd_send(request, body, strlen((char *)body));
    return AHTTPD_DONE;
}


static void ahttpd_free_routes(struct ahttpd_route *routes) {
    struct ahttpd_route *r;

    while ((r = routes) != NULL) {
        routes = routes->next;
        ahttpd_route_free(r);
    }
}


void ahttpd_route_free(struct ahttpd_route *route) {
        free(route->url);
        free(route);
}


struct ahttpd_route *ahttpd_route_new(
        enum ahttpd_method method,
        char *url,
        enum ahttpd_status (*handler)(struct ahttpd_request *),
        void *data) {
    struct ahttpd_route *r;
    r = calloc(1, sizeof(*r));

    if (r == NULL) {
        ESP_LOGE(TAG, "Error creating route: Out of memory");
        return NULL;
    }

    size_t url_len = strlen(url) + 1;
    r->url = calloc(1, url_len);

    if (r->url == NULL) {
        ESP_LOGE(TAG, "Error creating route url: Out of memory");
        free(r);
        return NULL;
    }

    r->method = method;
    snprintf(r->url, url_len, "%s", url);
    r->handler = handler;
    r->data = data;

    return r;
}


static struct ahttpd_route *ahttpd_copy_route(struct ahttpd_route *route) {
    return ahttpd_route_new(route->method, route->url, route->handler,
                            route->data);
}


esp_err_t ahttpd_router_init(struct ahttpd_route *routes) {
    struct ahttpd_route *r;

    if (routes == NULL) {
        return ESP_OK;
    }

    if (_routes != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    _routes = ahttpd_copy_route(routes);
    r = _routes;
    routes = routes->next;

    while (routes != NULL) {
        r->next = ahttpd_copy_route(routes);

        if (r->next == NULL) {
            ahttpd_free_routes(_routes);
            return ESP_ERR_NO_MEM;
        }

        r = r->next;
        routes = routes->next;
    }

    return ESP_OK;
}


struct ahttpd_route *ahttpd_get_routes(void) {
    return _routes;
}


void ahttpd_router_404_handler(
        enum ahttpd_status (*handler)(struct ahttpd_request *)) {
    _404 = handler;
}


enum ahttpd_status ahttpd_router(struct ahttpd_request *request) {
    enum ahttpd_status status = AHTTPD_NOT_FOUND;
    struct ahttpd_route *route = _routes;

    route = _routes;
    if (route == NULL) {
        return ahttpd_404(request);
    }

    while (route != NULL) {
        if (route->method != AHTTPD_ANY && route->method != request->method) {
            route = route->next;
            continue;
        }

        size_t url_len = strlen((char *)route->url);

        if (strcmp((char *)route->url, (char *)request->url) == 0 ||
                (route->url[url_len - 1] == '*' &&
                    strncmp((char *)route->url, (char *)request->url,
                             url_len -1) == 0)) {
            void *data = request->data;
            request->data = route->data;
            status = route->handler(request);

            if (status != AHTTPD_NOT_FOUND) {
                /* NOTE(jkoelker) Since this route handled the request, set it
                                  as the handler to avoid this lookup next
                                  time */
                request->handler = route->handler;
                return status;
            }

            request->data = data;
        }

        route = route->next;
    }

    return ahttpd_404(request);
}


static enum ahttpd_status _ahttpd_redirect(struct ahttpd_request *request,
                                           uint16_t code, char *url) {
    /* AHTTPD_MAX_URL_SIZE + strlen("Moved to ") + 1 */
    char body[AHTTPD_MAX_URL_SIZE + 10];

    if (url == NULL) {
        return AHTTPD_NOT_FOUND;
    }

    snprintf(body, sizeof(body), "Moved to %s", url);
    ahttpd_start_response(request, code);
    ahttpd_send_header(request, "Location", url);
    ahttpd_end_headers(request);
    ahttpd_send(request, body, strlen((char *)body));
    return AHTTPD_DONE;
}


enum ahttpd_status ahttpd_redirect(struct ahttpd_request *request) {
    char *url = (char *)request->data;
    return _ahttpd_redirect(request, 302, url);
}
