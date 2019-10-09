#ifndef SMS_HTTP_H
#define SMS_HTTP_H

#include <stddef.h>

struct http;

extern int http_initialize(void);
extern void http_close(struct http *http);
extern void http_shutdown(void);

extern struct http *http_open(
        const char *url,
        const char *hostname,
        const char *content_type,
        const char *cainfo);

extern int http_perform(
        struct http *http,
        const char *request,
        int *status,
        char *response,
        size_t length);

#endif /* SMS_HTTP_H */
