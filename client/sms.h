#ifndef SMS_SMS_H
#define SMS_SMS_H

#include <stddef.h>

#include <sys/types.h>

struct http;
struct sms;

struct line {
    char *ptr;
    size_t id;
    size_t to;
    size_t from;
};

struct section {
    char name[32];
    struct line *to;
    struct line *from;
};

typedef ssize_t (*sms_command_t)(
        struct sms * /*sms*/,
        const struct section * /*sections*/,
        size_t /*csection*/,
        size_t /*ack*/);

extern struct sms *sms_open(const char *device, int baudrate, struct http *h);
extern int sms_run(struct sms *sms, const char *handshake);
extern void sms_close(struct sms *sms);

/* Provide to vendor implementations */
extern char *sms_get_value(const char *s);
extern int sms_send(
        struct sms *sms,
        const char *command,
        sms_command_t callback);

extern int sms_inbox_prepare(struct sms *sms, int index);
extern int sms_inbox_commit(struct sms *sms, const char *what);
extern int sms_inbox_push(
        struct sms *sms,
        int index,
        const char *what,
        int offset);

extern int sms_call_start(
        struct sms *sms);

extern int sms_call_end(
        struct sms *sms,
        int call_x,
        int duration,
        int end_status,
        int cc_cause);

extern int sms_call_set_caller(
        struct sms *sms,
        const char *number,
        int type,
        int validity);

/* Provided by vendor implementations */
extern int sms_on_urc(
        struct sms *sms,
        const struct section *s);

extern int sms_read_all_sms(struct sms *sms);
extern int sms_read_sms(struct sms *sms, int index);
extern int sms_delete_sms(struct sms *sms, int index);

#endif /* SMS_SMS_H */
