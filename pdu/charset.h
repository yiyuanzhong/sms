#ifndef CHARSET_H
#define CHARSET_H

#include <stddef.h>

#include <sys/types.h>

extern ssize_t charset(
        const void *input,
        size_t inlen,
        void *output,
        size_t outlen,
        const char *from,
        const char *to);

#endif /* CHARSET_H */
