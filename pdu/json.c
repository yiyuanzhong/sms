#include "json.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "charset.h"
#include "pdu.h"

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define UCS4 "UCS-4LE"
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define UCS4 "UCS-4BE"
#else
#error Unsupported endian
#endif


static int json_encode_string(
        const char *input,
        char *output,
        size_t outlen)
{
    uint32_t ucs[256];
    const uint32_t *p;
    ssize_t inlen;
    char *q;
    int ret;

    assert(outlen);
    inlen = charset(input, strlen(input), ucs, sizeof(ucs), "UTF-8", UCS4);
    if (inlen < 0) {
        return -1;
    }

    inlen /= (ssize_t)sizeof(*ucs);
    for (p = ucs, q = output; inlen; ++p, --inlen) {
        /* TODO(yiyuanzhong): too lazy to calculate buffer precisely */
        if (outlen < 10) {
            return -1;
        }

        switch (*p) {
        case 0x22: *q++ = '\\'; *q++ = '\"'; outlen -= 2; continue;
        case 0x5C: *q++ = '\\'; *q++ = '\\'; outlen -= 2; continue;
        case 0x2F: *q++ = '\\'; *q++ = '/' ; outlen -= 2; continue;
        case 0x08: *q++ = '\\'; *q++ = 'b' ; outlen -= 2; continue;
        case 0x0C: *q++ = '\\'; *q++ = 'f' ; outlen -= 2; continue;
        case 0x0A: *q++ = '\\'; *q++ = 'n' ; outlen -= 2; continue;
        case 0x0D: *q++ = '\\'; *q++ = 'r' ; outlen -= 2; continue;
        case 0x09: *q++ = '\\'; *q++ = 't' ; outlen -= 2; continue;
        default:
            break;
        };

        if (*p >= 0x20 && *p <= 0x7E) {
            *q++ = (char)*p;
            --outlen;

        } else {
            ret = snprintf(q, outlen, "\\u%04X", *p);
            if (ret < 0 || (size_t)ret >= outlen) {
                return -1;
            }

            outlen -= (size_t)ret;
            q += ret;
        }
    }

    *q++ = '\0';
    return 0;
}

static int json_set_string(
        const char *name,
        const char *s,
        char **p,
        size_t *length)
{
    char value[1024];
    char key[64];
    int ret;

    if (json_encode_string(name, key, sizeof(key))) {
        return -1;
    }

    if (json_encode_string(s, value, sizeof(value))) {
        return -1;
    }

    ret = snprintf(*p, *length, "\"%s\":\"%s\",", key, value);
    if (ret < 0 || (size_t)ret >= *length) {
        return -1;
    }

    *length -= (size_t)ret;
    *p += ret;
    return 0;
}

static int json_set_time(
        const char *name,
        time_t t,
        char **p,
        size_t *length)
{
    char key[64];
    int ret;

    if (json_encode_string(name, key, sizeof(key))) {
        return -1;
    }

    ret = snprintf(*p, *length, "\"%s\":%ld,", key, t);
    if (ret < 0 || (size_t)ret >= *length) {
        return -1;
    }

    *length -= (size_t)ret;
    *p += ret;
    return 0;
}

//static int json_set_number(
//        const char *name,
//        uint8_t n,
//        char **p,
//        size_t *length)
//{
//    char key[64];
//    int ret;
//
//    if (json_encode_string(name, key, sizeof(key))) {
//        return -1;
//    }
//
//    ret = snprintf(*p, *length, "\"%s\":%u,", key, n);
//    if (ret < 0 || (size_t)ret >= *length) {
//        return -1;
//    }
//
//    *length -= (size_t)ret;
//    *p += ret;
//    return 0;
//}

static int json_do_encode_sms_deliver(
        const char *smsc,
        const struct sms_deliver *pdu,
        char **p,
        size_t *length)
{
    if (smsc) {
        if (json_set_string("RP-OA", smsc, p, length)) {
            return -1;
        }
    }

    if (json_set_string("TP-OA",   pdu->TPOriginatingAddress,     p, length) ||
        json_set_time  ("TP-SCTS", pdu->TPServiceCentreTimeStamp, p, length) ||
        json_set_string("TP-UD",   pdu->TPUserData,               p, length) ){

        return -1;
    }

    return 0;
}

int json_encode_sms_deliver(
        const char *smsc,
        const struct sms_deliver *pdu,
        void *buffer,
        size_t length)
{
    char *p;

    if (length < 3) {
        return -1;
    }

    p = (char *)buffer;
    *p++ = '{';
    --length;

    if (json_do_encode_sms_deliver(smsc, pdu, &p, &length)) {
        return -1;
    }

    *(p - 1) = '}';
    *p = '\0';
    return 0;
}

int json_encode_sms_submit(
        const char *smsc,
        const struct sms_submit *pdu,
        void *buffer,
        size_t length)
{
    (void)smsc;
    (void)pdu;
    (void)buffer;
    (void)length;
    return -1;
}
