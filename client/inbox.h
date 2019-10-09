#ifndef SMS_INBOX_H
#define SMS_INBOX_H

#include <time.h>

struct http;
struct inbox;
struct json_call;
struct sms;

extern void inbox_shutdown(struct inbox *inbox);
extern int inbox_health_check(struct inbox *inbox);
extern int inbox_commit(struct inbox *inbox, const char *what);
extern struct inbox *inbox_initialize(struct sms *sms, struct http *h);
extern int inbox_call(struct inbox *inbox, const struct json_call *call);

extern int inbox_prepare(
        struct inbox *inbox,
        int index,
        const struct timespec *when);

#endif /* SMS_INBOX_H */
