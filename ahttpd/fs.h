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

#ifndef AHTTPD_FS_H_
#define AHTTPD_FS_H_

#ifdef CONFIG_AHTTPD_ENABLE_ESPFS

#include <ahttpd/ahttpd.h>
#include <ahttpd/router.h>


#define AHTTPD_FS_URL(routes, url) \
    AHTTPD_ROUTE(routes, AHTTPD_GET, url, &(ahttpd_fs_handler), NULL)

#define AHTTPD_FS(routes) \
    AHTTPD_FS_URL(routes, "*")


enum ahttpd_status ahttpd_fs_handler(struct ahttpd_request *request);

void ahttpd_fs_501_handler(
        enum ahttpd_status (*handler)(struct ahttpd_request *));


#endif /* CONFIG_AHTTPD_ENABLE_ESPFS */

void ahttpd_fs_mimetype_handler(const char *(*handler)(const char *ext));

const char *ahttpd_fs_mimetype(const char *ext);

#endif /* AHTTPD_FS_H_ */
