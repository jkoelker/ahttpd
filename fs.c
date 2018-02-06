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

#ifdef CONFIG_AHTTPD_ENABLE_ESPFS

#include <esp_err.h>
#include <esp_log.h>

#include <stdbool.h>
#include <string.h>

#include "espfs/espfs.h"
#include "espfs/webpages-espfs.h"

#include "ahttpd/ahttpd.h"
#include "ahttpd/router.h"

#define CHUNK_SIZE 1024


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
    return AHTTPD_DONE

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
    esp_err_t err;
    char buf[CHUNK_SIZE];

    EspFsFile *file = (EspFsFile *)request->data;

    err = ahttpd_fs_init();
    ESP_ERROR_CHECK(err);

    if (err != ESP_OK) {
        return AHTTPD_NOT_FOUND;
    }

    if (file == NULL) {
        bool gzipped;
        struct ahttpd_header *accept;

        file = espFsOpen(request->url);

        if (file == NULL) {
            uint8_t buf[AHTTPD_MAX_URL_SIZE];
            snprintf(buf, sizeof(buf), "%s/index.html", request->url);
            file = espFsOpen(buf);
        }

        if (file == NULL) {
            return AHTTPD_NOT_FOUND;
        }

        gzipped = (espFsFlags(file) & FLAG_GZIP) == FLAG_GZIP;
        if (gzipped) {
            accept = ahttpd_find_header(requst, "Accept-Encoding");
            if (accept == NULL || strcasestr(accept->value, "gzip") == NULL) {
                espFsClose(file);
                return ahttpd_501(request);
            }
        }

        request->data = file;

        ahttpd_start_response(request, 200);
        ahttpd_send_header(request, "Content-Type", "");
        ahttpd_send_header(request, "Cache-Control",
                           "max-age=3600, must-revalidate");

        if (gzipped) {
            ahttpd_send_header(request, "Content-Encoding", "gzip");
        }

        ahttpd_end_headers(request);
        return AHTTPD_MORE;
    }

    if (espFsRead(file, buf, CHUNK_SIZE) > 0) {
        ahttpd_send(request, buf, len);
        return AHTTPD_MORE;
    }

    return AHTTPD_DONE;
}


void ahttpd_fs_501_handler(
        enum ahttpd_status (*handler)(struct ahttpd_request *)) {
    _501 = handler;
}


#endif /* CONFIG_AHTTPD_ENABLE_ESPFS */
