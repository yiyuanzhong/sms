#include "http.h"

#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include "logger.h"

struct http {
    CURL *curl;
    struct curl_slist *slist;
    char *content_type;
    char response[1024];
    size_t length;
}; /* struct http */

static size_t writer(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    struct http *h;
    size_t total;

    total = size * nmemb;
    h = (struct http *)userdata;
    if (total + h->length >= sizeof(h->response)) { /* Trailing zero reserved */
        return 0;
    }

    if (total) {
        memcpy(h->response + h->length, ptr, total);
        h->length += total;
        h->response[h->length] = 0;
    }

    return total;
}

int http_initialize(void)
{
    if (curl_global_init(CURL_GLOBAL_ALL)) {
        return -1;
    }

    return 0;
}

void http_shutdown(void)
{
    curl_global_cleanup();
}

struct http *http_open(
        const char *url,
        const char *hostname,
        const char *content_type,
        const char *cainfo)
{
    char buffer[1024];
    struct http *h;
    int ret;

    h = (struct http *)malloc(sizeof(*h));
    if (!h) {
        return NULL;
    }

    memset(h, 0, sizeof(*h));
    h->curl = curl_easy_init();
    if (!h->curl) {
        http_close(h);
        return NULL;
    }

    if (curl_easy_setopt(h->curl, CURLOPT_CONNECTTIMEOUT, 5L    ) ||
        curl_easy_setopt(h->curl, CURLOPT_TIMEOUT,        15L   ) ||
        curl_easy_setopt(h->curl, CURLOPT_URL,            url   ) ||
        curl_easy_setopt(h->curl, CURLOPT_WRITEFUNCTION,  writer) ||
        curl_easy_setopt(h->curl, CURLOPT_WRITEDATA,      h     ) ||
        curl_easy_setopt(h->curl, CURLOPT_SSL_VERIFYHOST, 0L    ) ||
        curl_easy_setopt(h->curl, CURLOPT_SSL_VERIFYPEER, 0L    ) ||
        curl_easy_setopt(h->curl, CURLOPT_FOLLOWLOCATION, 1L    ) ||
        curl_easy_setopt(h->curl, CURLOPT_MAXREDIRS,      5L    ) ||
        curl_easy_setopt(h->curl, CURLOPT_POST,           1L    ) ){

        http_close(h);
        return NULL;
    }

    if (cainfo) {
        if (curl_easy_setopt(h->curl, CURLOPT_CAINFO,         cainfo) ||
            curl_easy_setopt(h->curl, CURLOPT_SSL_VERIFYHOST, 2L    ) ||
            curl_easy_setopt(h->curl, CURLOPT_SSL_VERIFYPEER, 1L    ) ){

            http_close(h);
            return NULL;
        }
    }

    if (hostname) {
        ret = snprintf(buffer, sizeof(buffer), "Host: %s", hostname);
        if (ret < 0 || (size_t)ret >= sizeof(buffer)) {
            http_close(h);
            return NULL;
        }

        h->slist = curl_slist_append(h->slist, buffer);
    }

    if (content_type) {
        ret = snprintf(buffer, sizeof(buffer), "Content-Type: %s", content_type);
        if (ret < 0 || (size_t)ret >= sizeof(buffer)) {
            http_close(h);
            return NULL;
        }

        h->slist = curl_slist_append(h->slist, buffer);
    }

    h->slist = curl_slist_append(h->slist, "Expect:");
    if (curl_easy_setopt(h->curl, CURLOPT_HTTPHEADER, h->slist)) {
        http_close(h);
        return NULL;
    }

    h->content_type = strdup(content_type);
    if (!h->content_type) {
        http_close(h);
        return NULL;
    }

    return h;
}

void http_close(struct http *h)
{
    if (!h) {
        return;
    }

    if (h->slist) {
        curl_slist_free_all(h->slist);
    }

    if (h->curl) {
        curl_easy_cleanup(h->curl);
    }

    free(h->content_type);
    free(h);
}

int http_perform(
        struct http *h,
        const char *request,
        int *status,
        char *response,
        size_t resplen)
{
    size_t length;
    CURLcode ret;
    long code;
    char *raw;

    if (!h || !request) {
        return -1;
    }

    length = strlen(request);
    if (curl_easy_setopt(h->curl, CURLOPT_POSTFIELDS,          request) ||
        curl_easy_setopt(h->curl, CURLOPT_POSTFIELDSIZE_LARGE, length ) ){

        return -1;
    }

    h->length = 0;
    ret = curl_easy_perform(h->curl);
    if (ret != CURLE_OK) {
        LOGW("Http: curl_easy_perform() = %d", ret);
        return -1;
    }

    ret = curl_easy_getinfo(h->curl, CURLINFO_RESPONSE_CODE, &code);
    if (ret != CURLE_OK) {
        return -1;
    }

    ret = curl_easy_getinfo(h->curl, CURLINFO_CONTENT_TYPE, &raw);
    if (ret != CURLE_OK) {
        return -1;
    }

    if (code < 100 || code > 999) {
        return -1;
    }

    *status = (int)code;
    if (strcmp(raw, h->content_type)) {
        return -1;
    }

    if (h->length >= resplen) {
        return -1;
    }

    memcpy(response, h->response, h->length + 1);
    return 0;
}
