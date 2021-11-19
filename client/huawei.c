#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "logger.h"
#include "sms.h"

typedef int (*sms_urc_command_t)(
        struct sms * /*sms*/,
        const struct section * /*sections*/);

static int huawei_on_NWTIME(struct sms *sms, const struct section *s)
{
    struct tm tm;
    char sign;
    time_t t;
    char *p;
    int ret;
    int dst;
    int tz;

    (void)sms;

    if (!(p = sms_get_value(s->from->ptr))) {
        return -1;
    }

    memset(&tm, 0, sizeof(tm));
    ret = sscanf(p, "%d/%d/%d,%d:%d:%d%c%d,%d",
            &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
            &tm.tm_hour, &tm.tm_min, &tm.tm_sec,
            &sign, &tz, &dst);

    if (ret != 9) {
        return -1;
    }

    if (tm.tm_year < 38) { // TODO(yiyuanzhong): Y2K
        tm.tm_year += 100;
    }

    tm.tm_mon -= 1;
    t = mktime(&tm);
    if (sign == '+') {
        t += tz * 900;
    } else if (sign == '-') {
        t -= tz * 900;
    } else {
        return -1;
    }

    t -= dst * 3600;
    localtime_r(&t, &tm);
    LOGI("NWTIME: [%s]: %ld %04d-%02d-%02d %02d:%02d:%02d", p, t,
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec);

    return 0;
}

static int huawei_on_HCSQ(struct sms *sms, const struct section *s)
{
    char *p;

    (void)sms;

    if (!(p = sms_get_value(s->from->ptr))) {
        return -1;
    }

    LOGT("HCSQ: [%s]", p);
    return 0;
}

static int huawei_on_RSSI(struct sms *sms, const struct section *s)
{
    char *p;

    (void)sms;

    if (!(p = sms_get_value(s->from->ptr))) {
        return -1;
    }

    LOGT("^RSSI: [%s]", p);
    return 0;
}

static ssize_t huawei_on_CMGL(
        struct sms *sms,
        const struct section *sections,
        size_t csection,
        size_t ack)
{
    const struct section *s;
    const struct line *l;
    char buffer[1024];
    size_t length;
    int index;
    int stat;
    char *p;
    int ret;
    int n;

    (void)csection;

    if (ack == 0) {
        return 1; /* Possible */
    }

    s = sections + ack - 1;
    if (strcmp(s->name, "+CMGL")) {
        return 1; /* URC */
    }

    if ((s->to - s->from) % 2) {
        return -1;
    }

    for (l = s->from, n = 0; l < s->to; l += 2, ++n) {
        if (memcmp(l->ptr, "+CMGL:", 6)) {
            return -1;
        }

        if (!(p = sms_get_value(l->ptr))) {
            return -1;
        }

        LOGI("CMGL: [%s]", p);

        if (sscanf(p, "%d,%d,,%lu", &index, &stat, &length) != 3) {
            return -1;
        }

        if (stat != 0 && stat != 1) { /* Received unread/read */
            continue;
        }

        LOGI("CMGL: [%s]", l[1].ptr);
        if (length == strlen(l[1].ptr) / 2) {
            buffer[0] = '0';
            buffer[1] = '0';
            ret = snprintf(buffer + 2, sizeof(buffer) - 2, "%s", l[1].ptr);
            if (ret < 0 || (size_t)ret >= sizeof(buffer) - 2) {
                return -1;
            }

            ret = sms_inbox_push(sms, index, buffer, n);

        } else {
            ret = sms_inbox_push(sms, index, l[1].ptr, n);
        }

        if (ret) {
            return -1;
        }
    }

    return 2;
}

static ssize_t huawei_on_CMGR(
        struct sms *sms,
        const struct section *sections,
        size_t csection,
        size_t ack)
{
    const struct section *s;
    const struct line *l;
    char buffer[1024];
    size_t length;
    int stat;
    int ret;
    char *p;

    (void)sms;
    (void)csection;

    if (ack == 0) {
        return -1; /* Not possible */
    }

    s = sections + ack - 1;
    if (strcmp(s->name, "+CMGR")) {
        return 1;
    }

    if (!(p = sms_get_value(s->from->ptr))) {
        return -1;
    }

    LOGI("CMGR: [%s]", p);

    l = s->from + 1;
    if (l == s->to) {
        return -1;
    }

    if (sscanf(p, "%d,,%lu", &stat, &length) != 2) {
        return -1;
    }

    if (stat != 0 && stat != 1) { /* Received unread/read */
        return 2;
    }

    LOGI("CMGR: [%s]", l->ptr);

    if (length == strlen(l->ptr) / 2) {
        buffer[0] = '0';
        buffer[1] = '0';
        ret = snprintf(buffer + 2, sizeof(buffer) - 2, "%s", l->ptr);
        if (ret < 0 || (size_t)ret >= sizeof(buffer) - 2) {
            return -1;
        }

        ret = sms_inbox_commit(sms, buffer);

    } else {
        ret = sms_inbox_commit(sms, l->ptr);
    }

    if (ret) {
        return -1;
    }

    for (++l; l < s->to; ++l) {
        LOGW("Unexpected CMGR: [%s]", l->ptr);
    }

    return 2;
}

static int huawei_on_CMTI(struct sms *sms, const struct section *s)
{
    int index;
    char *p;

    if (!(p = sms_get_value(s->from->ptr))) {
        return -1;
    }

    if (sscanf(p, "\"SM\",%d", &index) != 1) {
        return -1;
    }

    LOGI("CMTI: [%s]", p);

    if (sms_inbox_prepare(sms, index)) {
        return 0;
    }

    return sms_read_sms(sms, index);
}

static int huawei_on_CRING(struct sms *sms, const struct section *s)
{
    char *p;
    int ret;

    if (!(p = sms_get_value(s->from->ptr))) {
        return -1;
    }

    if (strcmp(p, "VOICE")) {
        return 0;
    }

    ret = sms_call_start(sms);
    if (ret < 0) {
        return -1;
    }

    return 0;
}

static int huawei_on_CLIP(struct sms *sms, const struct section *s)
{
    char number[64];
    int validity;
    int type;
    char *p;

    if (!(p = sms_get_value(s->from->ptr))) {
        return -1;
    }

    if (p[0] != '\"') {
        return -1;
    }

    if (p[1] == '\"') {
        number[0] = '\0';
        if (sscanf(p + 2, ",%d,,,,%d", &type, &validity) != 2) {
            return -1;
        }
    } else {
        if (sscanf(p, "\"%63[^\"]\",%d,,,,%d", number, &type, &validity) != 3) {
            return -1;
        }
    }

    return sms_call_set_caller(sms, number, type, validity);
}

static int huawei_on_CEND(struct sms *sms, const struct section *s)
{
    char *p;

    int call_x;
    int duration;
    int end_status;
    int cc_cause;

    if (!(p = sms_get_value(s->from->ptr))) {
        return -1;
    }

    if (sscanf(p, "%d,%d,%d,%d", &call_x, &duration, &end_status, &cc_cause) != 4) {
        if (sscanf(p, "%d,%d,%d", &call_x, &duration, &end_status) != 3) {
            return -1;
        }

        cc_cause = 0;
    }

    return sms_call_end(sms, call_x, duration, end_status, cc_cause);
}

static const struct sms_urc_commands {
    const char *name;
    sms_urc_command_t command;
} g_urc_commands[] = {
    { "^MODE",      NULL                },
    { "^HWNAT",     NULL                },
    { "+CRING",     huawei_on_CRING     },
    { "+CLIP",      huawei_on_CLIP      },
    { "^CEND",      huawei_on_CEND      },
    { "^RSSI",      huawei_on_RSSI      },
    { "+CMTI",      huawei_on_CMTI      },
    { "^NWTIME",    huawei_on_NWTIME    },
    { "^HCSQ",      huawei_on_HCSQ      },
};

int sms_on_urc(struct sms *sms, const struct section *s)
{
    const struct sms_urc_commands *handler;
    const struct sms_urc_commands *p;
    const struct line *l;

    handler = NULL;
    for (p = g_urc_commands;
         p < g_urc_commands + sizeof(g_urc_commands) / sizeof(*g_urc_commands);
         ++p) {

        if (strcmp(p->name, s->name) == 0) {
            handler = p;
            break;
        }
    }

    if (!handler) {
        LOGT("Unhandled URC:");
        for (l = s->from; l != s->to; ++l) {
            LOGT("%s", l->ptr);
        }
        return 0;

    } else if (!handler->command) {
        return 0;
    }

    return handler->command(sms, s);
}

int sms_read_all_sms(struct sms *sms)
{
    char buffer[32];
    sprintf(buffer, "+CMGL=4");
    return sms_send(sms, buffer, huawei_on_CMGL);
}

int sms_read_sms(struct sms *sms, int index)
{
    char buffer[32];
    sprintf(buffer, "+CMGR=%d", index);
    return sms_send(sms, buffer, huawei_on_CMGR);
}

int sms_delete_sms(struct sms *sms, int index)
{
    char buffer[32];
    sprintf(buffer, "+CMGD=%d", index);
    return sms_send(sms, buffer, NULL);
}
