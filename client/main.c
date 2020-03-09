#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#include <flinter/babysitter.h>
#include <flinter/cmdline.h>
#include <flinter/daemon.h>
#include <flinter/signals.h>

#include "configure.h"
#include "http.h"
#include "json.h"
#include "logger.h"
#include "sms.h"

volatile sig_atomic_t g_quit;

static void on_signal_quit(int signum)
{
    g_quit = signum;
}

static int initialize_signals(void)
{
    return signals_block_all()                                           ||
           signals_default_all()                                         ||
           signals_ignore(SIGPIPE)                                       ||
           signals_set_handler(SIGHUP , on_signal_quit)                  ||
           signals_set_handler(SIGINT , on_signal_quit)                  ||
           signals_set_handler(SIGQUIT, on_signal_quit)                  ||
           signals_set_handler(SIGTERM, on_signal_quit)                  ||
           signals_unblock_all_except(SIGHUP, SIGINT, SIGQUIT, SIGTERM, 0);
}

static int callback(int argc, char *argv[])
{
    struct configure *c;
    struct http *h;

    (void)argc;
    (void)argv;

    umask(S_IWGRP | S_IWOTH);

    if (logger_initialize("../log/sms.log")) {
        LOGW("Failed to open log file, keep going...");
    }

    LOGI("START");
    if (!(c = configure_create("../etc/sms.conf"))) {
        LOGE("Failed to open configure: %d: %s", errno, strerror(errno));
        logger_shutdown();
        return EXIT_FAILURE;
    }

    json_set_token(c->token);
    if (initialize_signals()) {
        LOGE("Failed to initialize signals: %d: %s", errno, strerror(errno));
        configure_free(c);
        logger_shutdown();
        return EXIT_FAILURE;
    }

    if (http_initialize()) {
        LOGE("Failed to initialize libcurl");
        configure_free(c);
        logger_shutdown();
        return EXIT_FAILURE;
    }

    h = http_open(c->url, c->hostname, "application/json", c->cainfo);
    if (!h) {
        LOGE("Failed to open http connection");
        configure_free(c);
        logger_shutdown();
        return EXIT_FAILURE;
    }

    struct sms *const sms = sms_open(c->device, c->baudrate, h);
    if (!sms) {
        LOGE("Failed to open device [%s] baudrate=%d: %d: %s",
                c->device, c->baudrate, errno, strerror(errno));

        http_close(h);
        configure_free(c);
        logger_shutdown();
        return EXIT_FAILURE;
    }

    LOGI("RUNNING");

    int ret = sms_run(sms, c->handshake);

    LOGI("QUIT");
    sms_close(sms);
    http_close(h);
    http_shutdown();
    configure_free(c);

    LOGI("SHUTDOWN");
    logger_shutdown();
    return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
    babysitter_configure_t c;
    daemon_configure_t d;

    memset(&c, 0, sizeof(c));
    c.coredump_delay = 5000;
    c.normal_delay = 1000;

    memset(&d, 0, sizeof(d));
    d.callback = callback;
    d.babysitter = &c;
    return daemon_main(&d, argc, argv);
}
