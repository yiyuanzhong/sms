#ifndef JSON_H
#define JSON_H

#include <stddef.h>

struct sms_deliver;
struct sms_submit;

extern int json_encode_sms_deliver(
        const char *smsc,
        const struct sms_deliver *pdu,
        void *buffer,
        size_t length);

extern int json_encode_sms_submit(
        const char *smsc,
        const struct sms_submit *pdu,
        void *buffer,
        size_t length);

#endif /* JSON_H */
