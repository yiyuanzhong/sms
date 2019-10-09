#include "configure.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <flinter/cmdline.h>

static int configure_read_line(FILE *fp, char *buffer, int buflen)
{
    size_t length;

    if (!fgets(buffer, buflen, fp)) {
        if (feof(fp)) {
            return -1;
        }

        return -2;
    }

    length = strlen(buffer);
    if (length && buffer[length - 1] == '\n') {
        buffer[--length] = '\0';
    }

    if (length && buffer[length - 1] == '\r') {
        buffer[--length] = '\0';
    }

    return 0;
 }

static char *configure_trim(char *s)
{
    size_t len;
    char *b;

    for (b = s; *b && (*b == ' ' || *b == '\t'); ++b);

    for (len = strlen(b); len; --len) {
        if (b[len - 1] != ' ' && b[len - 1] != '\t') {
            break;
        }

        b[len - 1] = '\0';
    }

    return b;
}

static int configure_read(FILE *fp, struct configure *c)
{
    char buffer[256];
    unsigned long n;
    char *value;
    char *key;
    char *p;
    int ret;

    memset(c, 0, sizeof(*c));

    for (;;) {
        ret = configure_read_line(fp, buffer, sizeof(buffer));
        if (ret == -2) {
            return -1;
        } else if (ret == -1) {
            break;
        }

        if (buffer[0] == '\0' || buffer[0] == '#') {
            continue;
        }

        p = strchr(buffer, '=');
        if (!p) {
            return -1;
        }

        *p = '\0';
        key = configure_trim(buffer);
        value = configure_trim(p + 1);

        if (strcmp(key, "handshake") == 0) {
            if (!(c->handshake = strdup(value))) {
                return -1;
            }

        } else if (strcmp(key, "device") == 0) {
            if (!(c->device = strdup(value))) {
                return -1;
            }

        } else if (strcmp(key, "token") == 0) {
            if (!(c->token = strdup(value))) {
                return -1;
            }

        } else if (strcmp(key, "hostname") == 0) {
            if (!(c->hostname = strdup(value))) {
                return -1;
            }

        } else if (strcmp(key, "url") == 0) {
            if (!(c->url = strdup(value))) {
                return -1;
            }

        } else if (strcmp(key, "cainfo") == 0) {
            if (!(c->cainfo = strdup(value))) {
                return -1;
            }

        } else if (strcmp(key, "baudrate") == 0) {
            n = strtoul(value, &value, 10);
            if (n > INT_MAX || *value) {
                return -1;
            }

            c->baudrate = (int)n;
        }
    }

    /* hostname and cainfo can be omitted */

    if (!c->baudrate                    ||
        !c->handshake || !*c->handshake ||
        !c->device    || !*c->device    ||
        !c->url       || !*c->url       ||
        !c->token     || !*c->token     ){

        return -1;
    }

    return 0;
}

struct configure *configure_create(const char *path)
{
    struct configure *c;
    char *filename;
    FILE *fp;

    if (!path || !*path) {
        return NULL;
    }

    filename = cmdline_get_absolute_path(path, 0);
    if (!filename) {
        return NULL;
    }

    fp = fopen(filename, "r");
    if (!fp) {
        free(filename);
        return NULL;
    }

    free(filename);
    c = (struct configure *)malloc(sizeof(*c));
    if (!c) {
        fclose(fp);
        return NULL;
    }

    memset(c, 0, sizeof(*c));
    if (configure_read(fp, c)) {
        configure_free(c);
        fclose(fp);
        return NULL;
    }

    fclose(fp);
    return c;
}

void configure_free(struct configure *c)
{
    if (!c) {
        return;
    }

    free(c->handshake);
    free(c->hostname);
    free(c->device);
    free(c->cainfo);
    free(c->token);
    free(c->url);
    free(c);
}
