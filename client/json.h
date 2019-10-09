#ifndef SMS_JSON_H
#define SMS_JSON_H

#include <stddef.h>
#include <time.h>

struct json_sms {
    struct timespec when;
    const char *type;
    const char *pdu;
}; /* struct json_sms */

struct json_call {
    struct timespec ring_start;
    struct timespec call_start;
    struct timespec call_end;
    const char *type;
    const char *peer;
    const char *raw;
}; /* struct json_call */

/* Life span not taken */
extern void json_set_token(
        const char *token);

extern int json_decode(
        const char *result,
        int *ret);

extern int json_encode(
        const struct json_sms *sms,
        size_t sms_count,
        const struct json_call *calls,
        size_t call_count,
        void *buffer,
        size_t length);

#endif /* SMS_JSON_H */
