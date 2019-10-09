#include "charset.h"

#include <iconv.h>

ssize_t charset(const void *input,
                size_t inlen,
                void *output,
                size_t outlen,
                const char *from,
                const char *to)
{
    size_t olen;
    iconv_t cd;
    size_t ret;
    char *optr;
    char *iptr;

    if (inlen == 0) {
        return 0;
    }

    cd = iconv_open(to, from);
    if (cd == (iconv_t)-1) {
        return -1;
    }

    olen = outlen;
    iptr = (char *)input;
    optr = (char *)output;
    ret = iconv(cd, &iptr, &inlen, &optr, &outlen);
    iconv_close(cd);

    if (ret == (size_t)-1) {
        return -1;
    }

    return (ssize_t)(olen - outlen);
}
