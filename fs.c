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

#include <stddef.h>
#include <string.h>

#include "ahttpd/fs.h"

#ifdef CONFIG_AHTTPD_ENABLE_ESPFS

#include <esp_err.h>
#include <esp_log.h>

#include <stdbool.h>

#include "espfs/espfsformat.h"
#include "espfs/espfs.h"
#include "espfs/webpages-espfs.h"

#include "ahttpd/ahttpd.h"
#include "ahttpd/router.h"

#ifndef CHUNK_SIZE
#define CHUNK_SIZE AHTTPD_ESPFS_CHUNK_SIZE
#endif


static const char* TAG = "ahttpd-fs";
static bool FS_INITED = false;


static enum ahttpd_status (*_501)(struct ahttpd_request *) = NULL;


static enum ahttpd_status ahttpd_501(struct ahttpd_request *request) {
    if (_501 != NULL) {
        request->handler = _501;
        return _501(request);
    }

    uint8_t body[] = "Gzip not supported by client";
    ahttpd_start_response(request, 501);
    ahttpd_send_header(request, "Server", "AHTTPD/1.0");
    ahttpd_end_headers(request);
    ahttpd_send(request, body, strlen((char *)body));
    return AHTTPD_DONE;
}


static esp_err_t ahttpd_fs_init(void) {
    EspFsInitResult res;

    if (FS_INITED) {
        return ESP_OK;
    }

    res = espFsInit(webpages_espfs_start);

    switch (res) {
        case ESPFS_INIT_RESULT_OK:
            FS_INITED = true;
            return ESP_OK;

        case ESPFS_INIT_RESULT_NO_IMAGE:
            ESP_LOGE(TAG, "Failed to start espfs, image magic not found.");
            return ESP_ERR_NOT_FOUND;

        case ESPFS_INIT_RESULT_BAD_ALIGN:
            ESP_LOGE(TAG, "Failed to start espfs, bad alignment.");
            return ESP_ERR_INVALID_STATE;

        default:
            break;
    }

    ESP_LOGE(TAG, "Failed to start espfs, unknown result.");
    return ESP_ERR_INVALID_STATE;
}



struct _file {
    char *path;
    EspFsFile *file;
};


/* NOTE(jkoelker) This function is basically a port of cgiEspFsHook
                  from libesphttpd covered under the following:
  ----------------------------------------------------------------------------
  "THE BEER-WARE LICENSE" (Revision 42):
  Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain
  this notice you can do whatever you want with this stuff. If we meet some day,
  and you think this stuff is worth it, you can buy me a beer in return.
  ----------------------------------------------------------------------------
 */
enum ahttpd_status ahttpd_fs_handler(struct ahttpd_request *request) {
    struct _file *f = (struct _file *)request->data;

    if (!FS_INITED) {
        esp_err_t err = ahttpd_fs_init();
        ESP_ERROR_CHECK(err);

        if (err != ESP_OK) {
            return AHTTPD_NOT_FOUND;
        }
    }

    if (f == NULL) {
        bool gzipped;
        struct ahttpd_header *accept;
        const char *mimetype = NULL;

        EspFsFile *file = espFsOpen((char *)(request->url));

        if (file == NULL) {
            return AHTTPD_NOT_FOUND;
        }

        gzipped = (espFsFlags(file) & FLAG_GZIP) == FLAG_GZIP;
        if (gzipped) {
            accept = ahttpd_find_header(request, "Accept-Encoding");
            if (accept == NULL || strstr(accept->value, "gzip") == NULL) {
                espFsClose(file);
                return ahttpd_501(request);
            }
        }

        size_t url_len = strlen((char *)request->url);
        if (mimetype == NULL) {
            char *ext = (char *)request->url + url_len - 1;
            while (ext != (char *)request->url && *(ext - 1) != '.') {
                ext--;
            }

            mimetype = ahttpd_fs_mimetype(ext);

            if (mimetype == NULL) {
                mimetype = "application/octet-stream";
            }
        }

        f = calloc(1, sizeof(*f));
        if (f == NULL) {
            ESP_LOGE(TAG, "OOM while creating file struct for path %s",
                     request->url);
            return AHTTPD_DONE;
        }

        f->path = calloc(url_len + 1, sizeof(*(f->path)));
        if (f->path == NULL) {
            ESP_LOGE(TAG, "OOM while creating file url for path %s",
                     request->url);
            free(f);
            return AHTTPD_DONE;
        }

        snprintf(f->path, url_len + 1, "%s", request->url);
        f->file = file;
        request->data = f;

        ahttpd_start_response(request, 200);
        ahttpd_send_header(request, "Content-Type", mimetype);
        ahttpd_send_header(request, "Cache-Control",
                           "max-age=3600, must-revalidate");

        if (gzipped) {
            ahttpd_send_header(request, "Content-Encoding", "gzip");
        }

        ahttpd_end_headers(request);
        return AHTTPD_MORE;
    }

    char buf[CHUNK_SIZE];

    int len = espFsRead(f->file, buf, CHUNK_SIZE);
    if (len > 0) {
        ESP_LOGD(TAG, "Sending %d from file %s", len, f->path);
        ahttpd_send(request, buf, len);
        return AHTTPD_MORE;
    }

    espFsClose(f->file);
    free(f->path);
    free(f);
    return AHTTPD_DONE;
}


void ahttpd_fs_501_handler(
        enum ahttpd_status (*handler)(struct ahttpd_request *)) {
    _501 = handler;
}


#endif /* CONFIG_AHTTPD_ENABLE_ESPFS */


struct mime_map {
    const char *ext;
    const char *mime;
};


static const char *(*_mimetype)(const char *ext) = NULL;
static const struct mime_map mimetypes[] = {
    {"html", "text/html"},
    {"htm", "text/html"},
    {"css", "text/css"},
    {"js", "text/javascript"},
    {"txt", "text/plain"},
    {"jpg", "image/jpeg"},
    {"jpeg", "image/jpeg"},
    {"png", "image/png"},
    {"svg", "image/svg+xml"},
    {"xml", "text/xml"},
    {"json", "application/json"},
    {"eot", "application/vnd.ms-fontobject"},
    {"ttf", "application/font-sfnt"},
    {"woff", "application/font-woff"},
    {"woff2", "application/font-woff2"},
};


void ahttpd_fs_mimetype_handler(const char *(*handler)(const char *ext)) {
    _mimetype = handler;
}


const char *ahttpd_fs_mimetype(const char *ext) {
    const char *mimetype;

    if (_mimetype != NULL) {
        mimetype = _mimetype(ext);

        if (mimetype != NULL) {
            return mimetype;
        }
    }

    for (size_t i = 0; i < (sizeof(mimetypes) / sizeof(*mimetypes)); i++) {
        if (strcasecmp(mimetypes[i].ext, ext) == 0) {
            return mimetypes[i].mime;
        }
    }

    return NULL;
}
