#include "sms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/uio.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

#include <flinter/msleep.h>
#include <flinter/utility.h>

#include "inbox.h"
#include "json.h"
#include "logger.h"

#ifndef NDEBUG
static void HEX(const char *what, const void *buffer, size_t length)
{
    const char H[] = "0123456789ABCDEF";
    const unsigned char *p = buffer;
    size_t i;

    printf("%s: ", what);
    for (i = 0; i < length; ++i) {
        printf("%c%c", H[p[i] / 16], H[p[i] % 16]);
    }
    printf("\n");
    fflush(stdout);
}
#else
#define HEX(w,b,l)
#endif

extern volatile sig_atomic_t g_quit;

struct node {
    struct node *next;
    char command[256];
    sms_command_t callback;
};

struct sms {
    struct termios original;
    int64_t delay;
    int fd;

    int64_t cooling;
    int64_t requesting;
    sms_command_t callback;

    char buffer[65536];
    size_t total;
    int64_t last;
    struct timespec when;
    struct inbox *inbox;

    /* For calls */
    int calling;
    int ringing;
    char call_number[64];
    int call_number_type;
    int call_number_validity;
    struct timespec ring_started;
    struct timespec call_started;

    /* For command queue */
    struct node *head;
    struct node *tail;
};

static int sms_do_send(
        struct sms *sms,
        const char *command,
        sms_command_t callback)
{
    char buffer[1600];
    ssize_t ret;
    ssize_t len;

    len = snprintf(buffer, sizeof(buffer), "AT%s\r", command);
    if (len < 0 || (size_t)len >= sizeof(buffer)) {
        return -1;
    }

    ret = write(sms->fd, buffer, (size_t)len);
    if (ret != len) {
        return -1;
    }

    sms->requesting = get_monotonic_timestamp();
    LOGT("SEND [%s]", command);
    sms->callback = callback;
    return 0;
}

int sms_send(struct sms *sms, const char *command, sms_command_t callback)
{
    struct node *n;

    if (!sms->requesting && get_monotonic_timestamp() >= sms->cooling) {
        return sms_do_send(sms, command, callback);
    }

    LOGT("QUEUE [%s]", command);
    if (strlen(command) >= sizeof(n->command)) {
        return -1;
    }

    n = (struct node *)malloc(sizeof(*n));
    if (!n) {
        return -1;
    }

    n->next = NULL;
    n->callback = callback;
    strcpy(n->command, command);

    if (sms->tail) {
        sms->tail->next = n;
        sms->tail = n;
    } else {
        sms->head = n;
        sms->tail = n;
    }

    return 0;
}

static void sms_break_lines(
        char *buffer,
        size_t total,
        struct line *lines,
        size_t *cline)
{
    struct line *pline;
    size_t max;
    char *last;
    char *end;
    char *p;

    max = *cline;
    *cline = 0;
    last = buffer;
    end = buffer + total;
    for (p = buffer; p < end; ++p) {
        if (*p == '\r' && p + 1 < end && *(p + 1) == '\n') {
            pline = lines + *cline;
            pline->from = (size_t)(last - buffer);
            pline->to = (size_t)(p - buffer) + 2;
            pline->id = *cline;
            pline->ptr = last;
            last = p + 2;
            *p++ = '\0';
            ++(*cline);

            if (*cline == max) {
                break;
            }

        } else if (!last) {
            last = p;
        }
    }

#ifndef NDEBUG
    for (size_t i = 0; i < *cline; ++i) {
        LOGD("[%lu:%lu:%lu]: [%s]",
               i, lines[i].from, lines[i].to, lines[i].ptr);
    }
#endif
}

static void sms_break_sections(
        struct line *lines,
        size_t cline,
        struct section *sections,
        size_t *csection)
{
    struct section *psection;
    size_t max;
    size_t i;
    size_t j;
    char *p;

    max = *csection;
    *csection = 0;
    for (i = 0; i < cline; ++i) {
        if (!*lines[i].ptr) {
            if (i + 1 == cline) {
                break;
            }

            ++i;
            for (j = i + 1; j < cline; ++j) {
                if (!*lines[j].ptr) {
                    break;
                }
            }

            psection = sections + (*csection)++;
            psection->from = lines + i;
            psection->to = lines + j;
            snprintf(psection->name, sizeof(psection->name), "%s", lines[i].ptr);
            p = strchr(psection->name, ':');
            if (p) {
                *p = '\0';
            }

            i = j - 1;
            if (*csection == max) {
                break;
            }
        }
    }

#ifndef NDEBUG
    for (size_t i = 0; i < *csection; ++i) {
        psection = sections + i;
        LOGD("Section: %lu - %lu: %s",
               psection->from->id,
               (psection->to - 1)->id,
               psection->name);
    }
#endif
}

static size_t sms_on_command(
        struct sms *sms,
        const struct section *sections,
        size_t csection)
{
    static const int64_t kCooling = 100000000LL; /* 100ms */
    const struct section *s;
    size_t i;

    for (i = 0; i < csection; ++i) {
        s = sections + i;
        if (strcmp(s->name, "OK"        ) == 0 ||
            strcmp(s->name, "ERROR"     ) == 0 ||
            strcmp(s->name, "+CME ERROR") == 0 ||
            strcmp(s->name, "+CMS ERROR") == 0 ){

            sms->cooling = get_monotonic_timestamp() + kCooling;
            sms->requesting = 0;
            LOGT("ACK");
            break;
        }
    }

    return i;
}

static void sms_increase_when(struct sms *sms)
{
    /* Make sure every time we call handlers, we provide a different `when` */
    ++sms->when.tv_nsec;
    sms->when.tv_sec += sms->when.tv_nsec / 1000000000;
    sms->when.tv_nsec %= 1000000000;
}

static int sms_process(struct sms *sms)
{
    struct section sections[256];
    struct line lines[4096];
    sms_command_t callback;
    char buffer[65536];
    size_t csection;
    size_t skipf;
    size_t skipt;
    size_t cline;
    ssize_t ret;
    size_t i;

    memcpy(buffer, sms->buffer, sms->total);
    HEX("Process", buffer, sms->total);

    cline = sizeof(lines) / sizeof(*lines);
    sms_break_lines(buffer, sms->total, lines, &cline);

    csection = sizeof(sections) / sizeof(*sections);
    sms_break_sections(lines, cline, sections, &csection);

    if (!csection) {
        return -1;
    }

    skipf = 0;
    skipt = 0;
    if (sms->requesting) {
        i = sms_on_command(sms, sections, csection);
        if (i < csection) {
            callback = sms->callback;
            sms->callback = NULL;

            ret = 1;
            if (callback) {
                ret = callback(sms, sections, csection, i);
                if (ret < 0) {
                    return -1;
                }

                sms_increase_when(sms);
            }

            skipf = i + 1 - (size_t)ret;
            skipt = i + 1;
        }
    }

    for (i = 0; i < csection; ++i) {
        if (i >= skipf && i < skipt) {
            continue;
        }

        if (sms_on_urc(sms, sections + i)) {
            return -1;
        }

        sms_increase_when(sms);
    }

    sms->total -= (sections[csection - 1].to - 1)->to;
    if (sms->total) {
        return 1;
    }

    return 0;
}

static int sms_do_handshake_drain_stale_output(struct sms *sms)
{
    struct timespec tv;
    char buffer[2048];
    sigset_t empty;
    size_t total;
    ssize_t ret;
    fd_set rset;
    int n;

    memset(&tv, 0, sizeof(tv));
    if (sigemptyset(&empty)) {
        return -1;
    }

    total = 0;
    for (;;) {
        tv.tv_sec = 0;
        tv.tv_nsec = 500000000L;
        FD_ZERO(&rset);
        FD_SET(sms->fd, &rset);
        n = pselect(sms->fd + 1, &rset, NULL, NULL, &tv, &empty);
        if (n < 0) {
            if (errno == EINTR) {
                if (g_quit) {
                    return -1;
                }

                continue;
            }

            return -1;
        } else if (n == 0) {
            break;
        }

        ret = read(sms->fd, buffer, sizeof(buffer));
        if (ret < 0) {
            if (errno != EAGAIN) {
                return -1;
            }
        } else if (ret == 0) {
            return -1;
        }

        total += (size_t)ret;
    }

    LOGT("Handshake: drained %lu bytes", total);
    return 0;
}

/* Send handshaking command without changing any internal status */
static int sms_do_handshake_send(struct sms *sms, const char *handshake)
{
    char buffer[256];
    ssize_t ret;
    ssize_t len;

    len = snprintf(buffer, sizeof(buffer), "AT%s\r", handshake);
    if (len < 0 || (size_t)len >= sizeof(buffer)) {
        return -1;
    }

    ret = write(sms->fd, buffer, (size_t)len);
    if (ret != len) {
        return -1;
    }

    return 0;
}

static int sms_do_handshake_wait_for_ok(struct sms *sms)
{
    struct timespec tv;
    char buffer[256];
    sigset_t empty;
    ssize_t ret;
    fd_set rset;
    size_t pos;
    int n;

    memset(&tv, 0, sizeof(tv));
    if (sigemptyset(&empty)) {
        return -1;
    }

    pos = 0;
    for (;;) {
        tv.tv_sec = 0;
        tv.tv_nsec = 500000000L;
        FD_ZERO(&rset);
        FD_SET(sms->fd, &rset);
        n = pselect(sms->fd + 1, &rset, NULL, NULL, &tv, &empty);
        if (n < 0) {
            if (errno == EINTR) {
                if (g_quit) {
                    return -1;
                }

                continue;
            }

            return -1;
        } else if (n == 0) {
            break;
        }

        if (pos == sizeof(buffer)) {
            return -1;
        }

        ret = read(sms->fd, buffer + pos, sizeof(buffer) - pos);
        if (ret < 0) {
            if (errno != EAGAIN) {
                return -1;
            }
        } else if (ret == 0) {
            return -1;
        }

        pos += (size_t)ret;
    }

    HEX("Handshake", buffer, pos);
    if (pos < 4) {
        return -1;
    }

    if (memcmp(buffer + pos - 4, "OK\r\n", 4)) {
        return -1;
    }

    return 0;
}

static int sms_do_handshake(struct sms *sms, const char *handshake)
{
    LOGT("Handshake: drain output...");
    if (sms_do_handshake_drain_stale_output(sms)) {
        return -1;
    }

    LOGT("Handshake: send command...");
    if (sms_do_handshake_send(sms, handshake)) {
        return -1;
    }

    LOGT("Handshake: wait for OK...");
    if (sms_do_handshake_wait_for_ok(sms)) {
        return -1;
    }

    return 0;
}

static int sms_handshake(struct sms *sms, const char *handshake)
{
    int i;

    for (i = 0; i < 10; ++i) {
        if (sms_do_handshake(sms, handshake) == 0) {
            LOGI("HANDSHAKE");
            return 0;
        }

        if (i < 9) {
            msleep(1000);
        }
    }

    return -1;
}

struct sms *sms_open(const char *path, int baudrate, struct http *h)
{
    static const int kTries = 30;
    struct termios original;
    struct termios t;
    struct sms *sms;
    int64_t delay;
    speed_t baud;
    int fd;
    int i;

    switch (baudrate) {
    case 9600  : baud = B9600  ; break;
    case 38400 : baud = B38400 ; break;
    case 57600 : baud = B57600 ; break;
    case 115200: baud = B115200; break;
    default    : errno = EINVAL; return NULL;
    }

    /* How much nanoseconds to transmit one byte, multiply with 3 */
    delay = 1000000000LL / (baudrate / 8) * 3;
    delay = delay >= 1000000LL ? delay : 1000000LL;

    memset(&t, 0, sizeof(t));
    t.c_cflag = CS8 | CREAD | CLOCAL | CRTSCTS;
    if (cfsetispeed(&t, baud) || cfsetospeed(&t, baud)) {
        return NULL;
    }

    for (i = 0;;) {
        fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd >= 0) {
            break;
        }

        LOGW("Device cannot be opened: %d: %s", errno, strerror(errno));
        if (++i >= kTries) {
            return NULL;
        }

        msleep(1000);
    }

    if (tcgetattr(fd, &original)) {
        close(fd);
        return NULL;
    }

    if (tcsetattr(fd, TCSANOW, &t)) {
        close(fd);
        return NULL;
    }

    sms = (struct sms *)malloc(sizeof(*sms));
    if (!sms) {
        tcsetattr(fd, TCSANOW, &original);
        close(fd);
        return NULL;
    }

    memset(sms, 0, sizeof(*sms));
    memcpy(&sms->original, &original, sizeof(original));

    sms->inbox = inbox_initialize(sms, h);
    if (!sms->inbox) {
        free(sms);
        tcsetattr(fd, TCSANOW, &original);
        close(fd);
        return NULL;
    }

    sms->delay = delay;
    sms->fd = fd;
    return sms;
}

void sms_close(struct sms *sms)
{
    struct node *n;

    if (!sms) {
        return;
    }

    while ((n = sms->head)) {
        sms->head = sms->head->next;
        free(n);
    }

    inbox_shutdown(sms->inbox);
    tcsetattr(sms->fd, TCSADRAIN, &sms->original);
    close(sms->fd);
    free(sms);
}

int sms_run(struct sms *sms, const char *handshake)
{
    static const int64_t kRequesting = 10000000000LL; /* 10s   */
    static const int64_t kHeartbeat  = 1000000000LL;  /* 1s    */
    static const int64_t kTick       = 100000000LL;   /* 100ms */

    struct timespec tv;
    int64_t heartbeat;
    struct node *n;
    sigset_t empty;
    int64_t delay;
    int64_t diff;
    fd_set rset;
    ssize_t ret;
    int64_t now;
    int more;

    if (sigemptyset(&empty)) {
        return -1;
    }

    if (sms_handshake(sms, handshake)) {
        return -1;
    }

    if (sms_read_all_sms(sms)) {
        return -1;
    }

    heartbeat = get_monotonic_timestamp() + kHeartbeat;
    memset(&tv, 0, sizeof(tv));
    sms->last = -1;

    while (!g_quit) {
        now = get_monotonic_timestamp();

        delay = kTick;
        if (sms->last >= 0) {
            diff = sms->last - now;
            if (diff <= 0) {
                do {
                    more = sms_process(sms);
                    if (more < 0) {
                        LOGE("sms_process() returned %d", more);
                        return -1;
                    }
                } while (more);
                fflush(stdout);
                sms->last = -1;

            } else if (diff < delay) {
                delay = diff;
            }
        }

        if (sms->requesting) {
            if (now >= sms->requesting + kRequesting) {
                LOGE("Request timed out");
                return -1;
            }
        }

        if (sms->head) {
            if (!sms->requesting && now >= sms->cooling) {
                if (sms_do_send(sms, sms->head->command, sms->head->callback)) {
                    LOGE("Dequeuing sending item failed");
                    return -1;
                }

                n = sms->head;
                sms->head = sms->head->next;
                free(n);
                if (!sms->head) {
                    sms->tail = NULL;
                }
            }
        }

        if (now > heartbeat) {
            heartbeat = now + kHeartbeat;
            inbox_health_check(sms->inbox);
        }

        FD_ZERO(&rset);
        FD_SET(sms->fd, &rset);
        tv.tv_sec = delay / 1000000000LL;
        tv.tv_nsec = delay % 1000000000LL;
        ret = pselect(sms->fd + 1, &rset, NULL, NULL, &tv, &empty);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }

            LOGE("pselect(): %d: %s", errno, strerror(errno));
            return -1;

        } else if (ret == 0) {
            continue;
        }

        if (!FD_ISSET(sms->fd, &rset)) {
            continue;
        }

        if (sms->total == sizeof(sms->buffer)) {
            LOGE("Buffer overflow");
            return -1;
        }

        ret = read(sms->fd,
                   sms->buffer + sms->total,
                   sizeof(sms->buffer) - sms->total);

        if (ret < 0) {
            if (errno == EAGAIN) {
                continue;
            }
            LOGE("read(): %d: %s", errno, strerror(errno));
            return -1;

        } else if (ret == 0) {
            LOGE("read(): peer hang");
            return -1;
        }

        clock_gettime(CLOCK_REALTIME, &sms->when);
        sms->total += (size_t)ret;

        /* Don't rely on `now`, let it be precise */
        sms->last = get_monotonic_timestamp() + sms->delay;
    }

    return 0;
}

char *sms_get_value(const char *s)
{
    char *p;

    p = (char *)strchr(s, ':');
    if (!p) {
        return NULL;
    }

    do {
        ++p;
    } while (*p && *p == ' ');

    return p;
}

int sms_inbox_prepare(struct sms *sms, int index)
{
    return inbox_prepare(sms->inbox, index, &sms->when);
}

int sms_inbox_commit(struct sms *sms, const char *what)
{
    return inbox_commit(sms->inbox, what);
}

int sms_inbox_push(struct sms *sms, int index, const char *what, int offset)
{
    struct timespec tv;

    tv = sms->when;
    tv.tv_nsec += offset;
    tv.tv_sec += tv.tv_nsec / 1000000000;
    tv.tv_nsec %= 1000000000;

    if (inbox_prepare(sms->inbox, index, &tv)) {
        return -1;
    }

    return inbox_commit(sms->inbox, what);
}

int sms_call_start(struct sms *sms)
{
    if (sms->calling) {
        return 0;
    }

    sms->call_number_validity = -1;
    sms->ring_started = sms->when;
    sms->call_number_type = -1;
    sms->call_number[0] = '\0';
    sms->ringing = 1;
    sms->calling = 1;
    return 0;
}

int sms_call_set_caller(
        struct sms *sms,
        const char *number,
        int type,
        int validity)
{
    if (!sms->calling) {
        return 0;
    }

    snprintf(sms->call_number, sizeof(sms->call_number), "%s", number);
    sms->call_number_validity = validity;
    sms->call_number_type = type;
    return 0;
}

int sms_call_end(
        struct sms *sms,
        int call_x,
        int duration,
        int end_status,
        int cc_cause)
{
    struct timespec ring;
    struct json_call c;
    char buffer[256];

    if (!sms->calling) {
        return 0;
    }

    memset(&c, 0, sizeof(c));
    c.ring_start = sms->ring_started;
    c.call_start = sms->when;
    c.call_end   = sms->when;
    c.type       = "Missed";
    c.peer       = sms->call_number;
    c.raw        = buffer;

    /* TODO(yiyuanzhong): formatting should be done in vendor implementation */
    snprintf(buffer, sizeof(buffer),
            "number=%s,type=%d,validity=%d,"
            "call_x=%d,duration=%d,end_status=%d,cc_cause=%d",
            sms->call_number,
            sms->call_number_type,
            sms->call_number_validity,
            call_x, duration, end_status, cc_cause);

    ring.tv_nsec =
            (sms->when.tv_sec - sms->ring_started.tv_sec) * 1000000000L +
            (sms->when.tv_nsec - sms->ring_started.tv_nsec);

    ring.tv_sec = ring.tv_nsec / 1000000000L;
    ring.tv_nsec = ring.tv_nsec % 1000000000L;

    LOGI("CALL From[%s] Since[%ld.%09ld] Duration[%ld.%09ld] "
         "Type=%d Validity=%d ID=%d Duration=%d Status=%d Cause=%d",
         sms->call_number,
         sms->ring_started.tv_sec, sms->ring_started.tv_nsec,
         ring.tv_sec, ring.tv_nsec,
         sms->call_number_type,
         sms->call_number_validity,
         call_x, duration, end_status, cc_cause);

    sms->calling = 0;
    sms->ringing = 0;

    return inbox_call(sms->inbox, &c);
}
