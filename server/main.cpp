/* Copyright 2014 yiyuanzhong@gmail.com (Yiyuan Zhong)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <string.h>

#include <ClearSilver/ClearSilver.h>
#include <curl/curl.h>

#include <flinter/fastcgi/dispatcher.h>
#include <flinter/types/tree.h>
#include <flinter/babysitter.h>
#include <flinter/cmdline.h>
#include <flinter/daemon.h>
#include <flinter/logger.h>
#include <flinter/msleep.h>
#include <flinter/openssl.h>
#include <flinter/signals.h>
#include <flinter/utility.h>

#include "sms/server/configure.h"
#include "sms/server/database.h"
#include "sms/server/httpd.h"
#include "sms/server/processor.h"

static volatile sig_atomic_t g_quit = 0;
static void on_signal_quit(int signum)
{
    g_quit = signum;
}

static int initialize_signals(void)
{
    return signals_block_all()
        || signals_default_all()
        || signals_set_handler(SIGHUP , on_signal_quit)
        || signals_set_handler(SIGINT , on_signal_quit)
        || signals_set_handler(SIGQUIT, on_signal_quit)
        || signals_set_handler(SIGTERM, on_signal_quit)
        || signals_ignore(SIGPIPE)
        || signals_unblock_all_except(SIGHUP, SIGINT, SIGQUIT, SIGTERM, 0);
}

static int main_loop(Processor *processor)
{
    sigset_t empty;
    if (sigemptyset(&empty)) {
        return -1;
    }

    struct timespec tv;
    memset(&tv, 0, sizeof(tv));
    while (!g_quit) {
        tv.tv_sec = 1;
        tv.tv_nsec = 0;
        int ret = pselect(0, nullptr, nullptr, nullptr, &tv, &empty);
        if (ret < 0) {
            if (errno != EINTR) {
                return -1;
            }
            continue;
        }

        if (!processor->Cleanup()) {
            return -1;
        }
    }

    return 0;
}

static int callback(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (initialize_signals()) {
        return EXIT_FAILURE;
    }

    flinter::OpenSSLInitializer openssl_initializer;

    if (configure_load("../etc/server.conf")) {
        fprintf(stderr, "Failed to load configure.\n");
        return EXIT_FAILURE;
    }

    const flinter::Tree &log = (*g_configure)["log"];
    if (isatty(STDIN_FILENO)) {
        flinter::Logger::SetFilter(
                log["level"].as<int>(flinter::Logger::kLevelDebug));

    } else {
        flinter::Logger::SetColorful(log["colorful"].as<bool>(false));
        flinter::Logger::ProcessAttach(log["file"].value(),
                log["level"].as<int>(flinter::Logger::kLevelTrace));
    }

    // Initialize libcurl.
    if (curl_global_init(CURL_GLOBAL_ALL)) {
        CLOG.Error("Failed to initialize libcurl.");
        return EXIT_FAILURE;
    }

    if (!Database::Initialize()) {
        CLOG.Error("Failed to initialize Database.");
        return EXIT_FAILURE;
    }

    // Initialize ClearSilver.
    // Nothing to do.

    LOG(INFO) << "INITIALIZE";

    Processor processor;
    if (!processor.Initialize()) {
        return EXIT_FAILURE;
    }

    HTTPD httpd(&processor);
    if (!httpd.Start()) {
        return EXIT_FAILURE;
    }

    LOG(INFO) << "RUNNING";
    int ret = main_loop(&processor);
    LOG(INFO) << "SHUTDOWN";

    httpd.Stop();

    processor.Shutdown();

#if HAVE_NERR_SHUTDOWN
    nerr_shutdown();
#endif

    Database::Cleanup();

    configure_destroy();

    LOG(INFO) << "QUIT";
    flinter::Logger::ProcessDetach();
    return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
    babysitter_configure_t b;
    memset(&b, 0, sizeof(b));
    daemon_configure_t d;
    memset(&d, 0, sizeof(d));
    d.callback = callback;
    return daemon_main(&d, argc, argv);
}
