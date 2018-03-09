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

#include <lwip/err.h>
#include <lwip/inet.h>
#include <lwip/ip_addr.h>
#include <lwip/sockets.h>
#include <lwip/tcp.h>

#include <inttypes.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "ahttpd/ahttpd.h"
#include "http-parser/http_parser.h"


static const char* TAG = "ahttpd";


struct unsent_buf {
    void *buf;
    size_t len;
    struct unsent_buf *next;
};


struct ahttpd_state {
    struct ahttpd *httpd;
    struct tcp_pcb *pcb;
    http_parser *parser;
    uint8_t retry_count;

    struct ahttpd_request *request;
    enum ahttpd_status status;

    struct unsent_buf *unsent;
};


static void ahttpd_close(struct tcp_pcb *tpcb, struct ahttpd_state *state);


static int on_url(http_parser* parser, const char *at, size_t length) {
    struct ahttpd_state *state = (struct ahttpd_state *)parser->data;

    if (state == NULL) {
        ESP_LOGE(TAG, "on_url got NULL state.");
        return 1;
    }

    if (state->request->url == NULL) {
        if (length >= AHTTPD_MAX_URL_SIZE) {
            ESP_LOGE(TAG, "url > max length (%d): %.*s .",
                     AHTTPD_MAX_URL_SIZE, (int)length, at);
            return 1;
        }

        state->request->url = calloc(length + 1, sizeof(char));
    } else {
        size_t len = strlen((char *)state->request->url) + length + 1;
        if (len >= AHTTPD_MAX_URL_SIZE) {
            ESP_LOGE(TAG, "url > max length (%d): %.*s .",
                     AHTTPD_MAX_URL_SIZE, (int)length, at);
            return 1;
        }

        state->request->url = realloc(state->request->url, len);
    }

    strncat((char *)state->request->url, at, length);
    state->request->method = parser->method;

    return 0;
}


static int on_header_field(http_parser* parser, const char *at,
                           size_t length) {
    struct ahttpd_state *state = (struct ahttpd_state *)parser->data;
    struct ahttpd_header *headers;

    if (state == NULL) {
        ESP_LOGE(TAG, "on_header_field got NULL state.");
        return 1;
    }

    if (state->request->headers == NULL) {
        state->request->headers = calloc(1, sizeof(struct ahttpd_header));
    }

    if (state->request->headers->value != NULL) {
        struct ahttpd_header *header = calloc(1, sizeof(*header));
        header->next = state->request->headers;
        state->request->headers = header;
    }

    headers = state->request->headers;

    if (headers->name == NULL) {
        if (length >= AHTTPD_MAX_HEADER_NAME_SIZE) {
            ESP_LOGE(TAG, "header name > max length (%d): %.*s .",
                     AHTTPD_MAX_HEADER_NAME_SIZE, (int)length, at);
            return 1;
        }

        headers->name = calloc(length + 1, sizeof(uint8_t));
    } else {
        size_t len = strlen((char *)headers->name) + length + 1;
        if (len >= AHTTPD_MAX_HEADER_NAME_SIZE) {
            ESP_LOGE(TAG, "header name > max length (%d): %.*s .",
                     AHTTPD_MAX_HEADER_NAME_SIZE, (int)length, at);
            return 1;
        }

        headers->name = realloc(headers->name, len);
    }

    strncat((char *)headers->name, at, length);

    return 0;
}


static int on_header_value(http_parser* parser, const char *at,
                           size_t length) {
    struct ahttpd_state *state = (struct ahttpd_state *)parser->data;
    struct ahttpd_header *headers;

    if (state == NULL) {
        ESP_LOGE(TAG, "on_header_value got NULL state.");
        return 1;
    }

    if (state->request->headers == NULL) {
        ESP_LOGE(TAG, "on_header_value called before on_header_field.");
        return 1;
    }

    headers = state->request->headers;

    if (headers->value == NULL) {
        if (length >= AHTTPD_MAX_HEADER_VALUE_SIZE) {
            ESP_LOGE(TAG, "header value > max length (%d): %.*s .",
                     AHTTPD_MAX_HEADER_VALUE_SIZE, (int)length, at);
            return 1;
        }

        headers->value = calloc(length + 1, sizeof(uint8_t));
    } else {
        size_t len = (strlen((char *)headers->value) +
                        length + 1);
        if (len >= AHTTPD_MAX_HEADER_VALUE_SIZE) {
            ESP_LOGE(TAG, "header value > max length (%d): %.*s .",
                     AHTTPD_MAX_HEADER_VALUE_SIZE, (int)length, at);
            return 1;
        }

        headers->value = realloc(headers->value, len);
    }

    strncat((char *)headers->value, at, length);

    return 0;
}


static void call_handler(struct ahttpd_state *state) {
    if (state->status == AHTTPD_MORE && state->request->handler != NULL) {
        state->status = state->request->handler(state->request);
    }
}


static int on_headers_complete(http_parser* parser) {
    struct ahttpd_state *state = (struct ahttpd_state *)parser->data;

    if (state == NULL) {
        ESP_LOGE(TAG, "on_headers_complete got NULL state.");
        return 1;
    }

    if (state->request->url == NULL) {
        ESP_LOGE(TAG, "Headers complete without URL! Closing connection.");
        ahttpd_close(state->pcb, state);
        return 1;
    }

    ESP_LOGD(TAG, "New request for url %s", state->request->url);
    call_handler(state);

    if (state->status == AHTTPD_DONE) {
        return 1;  /* Don't expect a body if we are done */
    }

    return 0;
}


static int on_body(http_parser* parser, const char *at, size_t length) {
    struct ahttpd_state *state = (struct ahttpd_state *)parser->data;
    state->request->body = (const uint8_t *) at;
    state->request->body_len = length;

    call_handler(state);

    return 0;
}


static int on_message_complete(http_parser* parser) {
    struct ahttpd_state *state = (struct ahttpd_state *)parser->data;

    state->request->body = NULL;

    call_handler(state);

    return 0;
}


static err_t ahttpd_state_alloc(struct tcp_pcb *newpcb,
                                struct ahttpd *httpd,
                                struct ahttpd_state **state) {
    struct ahttpd_state *s;

    s = calloc(1, sizeof(*s));
    if (s == NULL) {
        ESP_LOGE(TAG, "Error creating state for connection: Out of memory");
        return ERR_MEM;
    }

    s->parser = calloc(1, sizeof(*(s->parser)));
    if (s->parser == NULL) {
        ESP_LOGE(TAG, "Error creating parser for connection: Out of memory");
        free(s);
        return ERR_MEM;
    }

    s->request = calloc(1, sizeof(*(s->request)));
    if (s->request == NULL) {
        ESP_LOGE(TAG, "Error creating request for connection: Out of memory");
        free(s->parser);
        free(s);
        return ERR_MEM;
    }

    s->request->handler = httpd->router;
    s->request->free_data = 0;
    s->request->_state = s;
    http_parser_init(s->parser, HTTP_REQUEST);
    s->parser->data = s;
    s->retry_count = 0;
    s->httpd = httpd;
    s->pcb = newpcb;

    *state = s;
    return ESP_OK;
}


static void ahttpd_state_free(struct ahttpd_state *state) {
    const char *url;
    if (state->request->url != NULL) {
        url = state->request->url;
    } else {
        url = "<NULL>";
    }

    ESP_LOGD(TAG, "Freeing state for request url: %s", url);

    if (state->request->headers != NULL) {
        struct ahttpd_header *header;
        while ((header = state->request->headers) != NULL) {
            state->request->headers = state->request->headers->next;
            free(header->name);
            free(header->value);
            free(header);
        }
    }

    if (state->unsent != NULL) {
        struct unsent_buf *b;
        while ((b = state->unsent) != NULL) {
            state->unsent = state->unsent->next;
            free(b->buf);
            free(b);
        }
    }

    free(state->parser);

    if (state->request->free_data) {
        free(state->request->data);
    }

    free(state->request->url);
    free(state->request);
    free(state);
}


static void ahttpd_close(struct tcp_pcb *tpcb, struct ahttpd_state *state) {
    err_t err;

    ESP_LOGD(TAG, "Closing connection.");
    tcp_arg(tpcb, NULL);
    tcp_sent(tpcb, NULL);
    tcp_recv(tpcb, NULL);
    tcp_err(tpcb, NULL);
    tcp_poll(tpcb, NULL, 0);

    ahttpd_state_free(state);

    err = tcp_close(tpcb);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "Error closing connection: %s", lwip_strerr(err));
    }
}


static err_t ahttpd_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p,
                        err_t err) {
    struct ahttpd_state *state = (struct ahttpd_state *)arg;

    if (p == NULL) {
        ESP_LOGD(TAG, "Remote host closed connection.");
        ahttpd_close(tpcb, state);
        return ERR_OK;
    }

    if (err != ERR_OK || state == NULL) {
        if (state == NULL) {
            ESP_LOGE(TAG, "State is NULL.");
        } else {
            ESP_LOGE(TAG, "Other error: %s.", lwip_strerr(err));
        }

        tcp_recved(tpcb, p->tot_len);
        pbuf_free(p);
        ahttpd_close(tpcb, state);
        return ERR_OK;
    }


    const http_parser_settings settings = {
        .on_url = &on_url,
        .on_headers_complete = &on_headers_complete,
        .on_header_field = &on_header_field,
        .on_header_value = &on_header_value,
        .on_body = &on_body,
        .on_message_complete = &on_message_complete
    };

    struct pbuf *q;
    size_t plen;

    for (q = p; q != NULL; q = q->next) {
        plen = http_parser_execute(state->parser,
                                   &settings,
                                   q->payload,
                                   q->len);
        /* TODO support websocket / upgrade */
        if (state->parser->upgrade) {
            ESP_LOGE(TAG, "Websocket Not supported: dropping connection.");
            tcp_recved(tpcb, p->tot_len);
            pbuf_free(p);
            ahttpd_close(tpcb, state);
            return ERR_OK;
        }

        if (plen != q->len) {
            ESP_LOGE(TAG, "plen(%d) != q->len(%d)", plen, q->len);
            ESP_LOGE(TAG, "HTTP parsing error, dropping connection.");
            tcp_recved(tpcb, p->tot_len);
            pbuf_free(p);
            ahttpd_close(tpcb, state);
            return ERR_OK;
        }

    }

    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    return ERR_OK;
}


static void ahttpd_err(void *arg, err_t err) {
    struct ahttpd_state *state = (struct ahttpd_state *)arg;
    ESP_LOGD(TAG, "Error: %s", lwip_strerr(err));
    ahttpd_state_free(state);
}


static esp_err_t ahttpd_unsent(struct ahttpd_state *state, void *buf,
                               size_t length) {
    struct unsent_buf *unsent = calloc(1, sizeof(*unsent));

    if (unsent == NULL) {
        ESP_LOGE(TAG, "Failed to alloc unsent pointer!");
        return ESP_ERR_NO_MEM;
    }

    unsent->buf = malloc(length);

    if (unsent->buf == NULL) {
        ESP_LOGE(TAG, "Failed to alloc unsent pointer!");
        free(unsent);
        return ESP_ERR_NO_MEM;
    }

    memcpy(unsent->buf, buf, length);
    unsent->len = length;

    if (state->unsent != NULL) {
        struct unsent_buf *b;
        while ((b = state->unsent) != NULL) {}
        b->next = unsent;
    } else {
        state->unsent = unsent;
    }

    return ESP_OK;
}


static size_t _ahttpd_write(struct tcp_pcb *pcb, void *buf, size_t length) {
    uint16_t len;
    uint16_t max_len;
    err_t err;

    max_len = tcp_sndbuf(pcb);
    if (length > max_len) {
        len = max_len;
    } else {
        len = length;
    }

    err = tcp_write(pcb, buf, len, 0);

    while (err == ERR_MEM && len > 0) {
        if (tcp_sndbuf(pcb) == 0 ||
                tcp_sndqueuelen(pcb) >= TCP_SND_QUEUELEN(pcb)) {
            len = 0;
        } else {
            len = len / 2;
        }

        err = tcp_write(pcb, buf, len, 0);
    }

    if (err == ERR_OK) {
        tcp_output(pcb);
    }

    return len;
}


static void ahttpd_write(struct ahttpd_state *state, struct tcp_pcb *pcb,
                         void *buf, size_t length) {
    uint16_t len;
    esp_err_t esp_err;

    if (state->unsent != NULL) {
        esp_err = ahttpd_unsent(state, buf, length);
        ESP_ERROR_CHECK(esp_err);

        if (esp_err != ESP_OK) {
            ahttpd_close(pcb, state);
        }

        return;
    }

    len = _ahttpd_write(pcb, buf, length);
    state->retry_count = 0;

    if (len != length) {
        esp_err = ahttpd_unsent(state, buf + len, length - len);
        ESP_ERROR_CHECK(esp_err);
        if (esp_err != ESP_OK) {
            ahttpd_close(pcb, state);
        }
    }
}


static err_t ahttpd_poll(void *arg, struct tcp_pcb *pcb) {
    struct ahttpd_state *state = (struct ahttpd_state *)arg;

    if (state == NULL) {
        ESP_LOGE(TAG, "HTTP state is NULL: dropping connection.");
        ahttpd_close(pcb, state);
        return ERR_OK;
    }

    if (state->unsent != NULL) {
        struct unsent_buf *unsent;
        uint16_t len = _ahttpd_write(pcb, state->unsent->buf,
                                     state->unsent->len);
        state->retry_count = 0;

        if (len == state->unsent->len) {
            unsent = state->unsent;
            state->unsent = unsent->next;
            free(unsent->buf);
            free(unsent);
        } else {
            unsent = calloc(1, sizeof(*unsent));

            if (unsent == NULL) {
                ESP_LOGE(TAG, "Failed to alloc unsent pointer!");
                ahttpd_close(pcb, state);
                return ERR_OK;
            }

            unsent->buf = malloc(state->unsent->len - len);

            if (unsent->buf == NULL) {
                ESP_LOGE(TAG, "Failed to alloc unsent pointer!");
                free(unsent);
                ahttpd_close(pcb, state);
                return ERR_OK;
            }

            memcpy(unsent->buf, state->unsent->buf + len,
                   state->unsent->len - len);
            unsent->len = state->unsent->len - len;
            unsent->next = state->unsent->next;

            free(state->unsent->buf);
            free(state->unsent);

            state->unsent = unsent;
        }

        if (state->status == AHTTPD_DONE && state->unsent == NULL) {
            ahttpd_close(pcb, state);
        }

    } else {
        state->retry_count++;

        if (state->retry_count >= 4) {
            ESP_LOGD(TAG, "Send retries exceeded");
            ahttpd_close(pcb, state);
            return ERR_OK;
        }

        call_handler(state);
    }

    return ERR_OK;
}


static err_t ahttpd_sent(void *arg, struct tcp_pcb *pcb, uint16_t len) {
    struct ahttpd_state *state = (struct ahttpd_state *)arg;

    if (state == NULL) {
        return ERR_OK;
    }

    state->retry_count = 0;

    if (state->status == AHTTPD_DONE && state->unsent == NULL) {
        ahttpd_close(pcb, state);
    }

    return ERR_OK;
}

static err_t ahttpd_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    err_t state_err;
    struct ahttpd_state *state;
    struct ahttpd *httpd = (struct ahttpd *)arg;

    tcp_accepted(httpd->_pcb);
    tcp_setprio(newpcb, TCP_PRIO_MIN);

    state_err = ahttpd_state_alloc(newpcb, httpd, &state);
    if (state_err != ERR_OK) {
        return state_err;
    }

    tcp_arg(newpcb, state);
    tcp_recv(newpcb, ahttpd_recv);
    tcp_err(newpcb, ahttpd_err);
    tcp_poll(newpcb, ahttpd_poll, 4);
    tcp_sent(newpcb, ahttpd_sent);

    return ERR_OK;
}


esp_err_t ahttpd_start(const struct ahttpd_options *options,
                       struct ahttpd **out_httpd) {
    err_t err;
    struct ahttpd *ctx;
    char ip_str[INET_ADDRSTRLEN];

    ctx = calloc(1, sizeof(*ctx));

    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* NOTE(jkoelker) INET_ADDRSTRLEN + strlen("[]:65536") + 1 */
    ctx->_bind_str = calloc(INET_ADDRSTRLEN + 10, sizeof(uint8_t));

    if (ctx->_bind_str == NULL) {
        free(ctx);
        return ESP_ERR_NO_MEM;
    }

    inet_ntop(AF_INET, options->ip_addr, ip_str, INET_ADDRSTRLEN);
    snprintf((char *)ctx->_bind_str, INET_ADDRSTRLEN + 10,
             "[%s]:%" PRIu16, ip_str, options->port);

    ESP_LOGD(TAG, "Creating initial PCB");
    ctx->_pcb = tcp_new();
    if (ctx->_pcb == NULL) {
        ESP_LOGE(TAG, "Could not create initial PCB");
        free(ctx->_bind_str);
        free(ctx);
        return ESP_ERR_NO_MEM;
    }


    ESP_LOGD(TAG, "Binding to %s", ctx->_bind_str);
    err = tcp_bind(ctx->_pcb, options->ip_addr, options->port);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "Could not bind to %s", ctx->_bind_str);
        free(ctx->_bind_str);
        free(ctx);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Transforming to listening PCB");
    ctx->_pcb = tcp_listen(ctx->_pcb);
    if (ctx->_pcb == NULL) {
        ESP_LOGE(TAG, "Could not transform to listening PCB");
        free(ctx->_bind_str);
        free(ctx);
        return ESP_ERR_NO_MEM;
    }

    ctx->router = options->router;

    tcp_arg(ctx->_pcb, ctx);
    tcp_accept(ctx->_pcb, ahttpd_accept);

    *out_httpd = ctx;
    return ESP_OK;
}


esp_err_t ahttpd_stop(struct ahttpd *httpd) {
    err_t err;

    ESP_LOGD(TAG, "Stopping server on %s", httpd->_bind_str);
    err = tcp_close(httpd->_pcb);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "Error stopping server on %s", httpd->_bind_str);
        return ESP_FAIL;
    }

    free(httpd->_bind_str);
    free(httpd);
    return ESP_OK;
}


void ahttpd_start_response(struct ahttpd_request *request, uint16_t code) {
    struct ahttpd_state *state = (struct ahttpd_state *)request->_state;
    char buf[32];

    if (state == NULL) {
        return;
    }

    snprintf(buf, sizeof(buf), "HTTP/1.1 %" PRIu16 " OK\r\n", code);
    ahttpd_write(state, state->pcb, buf, strlen(buf));
}


void ahttpd_send_header(struct ahttpd_request *request, const char *name,
                        const char *value) {
    struct ahttpd_state *state = (struct ahttpd_state *)request->_state;
    char buf[AHTTPD_MAX_HEADER_NAME_SIZE + AHTTPD_MAX_HEADER_VALUE_SIZE] = {0};

    if (state == NULL) {
        return;
    }

    snprintf(buf, sizeof(buf), "%s: %s\r\n", name, value);
    ahttpd_write(state, state->pcb, buf, strlen(buf));
}


void ahttpd_send_headers(struct ahttpd_request *request,
                        struct ahttpd_header *headers) {
    struct ahttpd_state *state = (struct ahttpd_state *)request->_state;
    struct ahttpd_header *header = headers;
    char buf[AHTTPD_MAX_HEADER_NAME_SIZE + AHTTPD_MAX_HEADER_VALUE_SIZE] = {0};

    if (state == NULL) {
        return;
    }

    while (header != NULL) {
        snprintf(buf, sizeof(buf), "%s: %s\r\n", header->name, header->value);
        ahttpd_write(state, state->pcb, buf, strlen(buf));
        header = header->next;
    }
}


void ahttpd_end_headers(struct ahttpd_request *request) {
    struct ahttpd_state *state = (struct ahttpd_state *)request->_state;

    if (state == NULL) {
        return;
    }

    ahttpd_write(state, state->pcb, "\r\n", 2);
}


void ahttpd_send(struct ahttpd_request *request, void *buf, size_t length) {
    struct ahttpd_state *state = (struct ahttpd_state *)request->_state;

    if (state == NULL) {
        return;
    }

    ahttpd_write(state, state->pcb, buf, length);
}


void ahttpd_send_file(struct ahttpd_request *request, const char *pathname) {
    /*
    struct ahttpd_state *state = (struct ahttpd_state *)request->_state;
    */
}


struct ahttpd_header *ahttpd_find_header(struct ahttpd_request *request,
                                         char *name) {
    struct ahttpd_header *header = request->headers;
    while (header != NULL) {
        if (strcasecmp(name, header->name) == 0) {
            return header;
        }

        header = header->next;
    }

    return NULL;
}


ip_addr_t *ahttpd_remote_ip(struct ahttpd_request *request) {
    struct ahttpd_state *state = (struct ahttpd_state *)request->_state;

    if (state == NULL) {
        return NULL;
    }

    return &(state->pcb->remote_ip);
}
