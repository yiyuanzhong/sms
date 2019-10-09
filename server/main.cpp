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

#include <ClearSilver/ClearSilver.h>
#include <curl/curl.h>
#include <fcgiapp.h>
#include <fcgios.h>

#include <flinter/fastcgi/dispatcher.h>
#include <flinter/cmdline.h>
#include <flinter/openssl.h>
#include <flinter/utility.h>

#include "sms/server/configure.h"

int main(int argc, char *argv[])
{
    argv = cmdline_setup(argc, argv);
    if (!argv) {
        fprintf(stderr, "Failed to setup cmdline library.\n");
        return EXIT_FAILURE;
    }

    // Initialize timezone before going multi-threaded.
    tzset();

    // Initialize weak (but fast) PRG rand(3).
    randomize();

    flinter::OpenSSLInitializer openssl_initializer;

    // Initialize libcurl.
    if (curl_global_init(CURL_GLOBAL_ALL)) {
        fprintf(stderr, "Failed to initialize libcurl.\n");
        return EXIT_FAILURE;
    }

    // Initialize FastCGI.
    if (FCGX_Init()) {
        fprintf(stderr, "Failed to initialize FastCGI.\n");
        return EXIT_FAILURE;
    }

    // Initialize ClearSilver.
    // Nothing to do.

    if (configure_load("../etc/server.conf")) {
        fprintf(stderr, "Failed to load configure.\n");
        return EXIT_FAILURE;
    }

    return flinter::Dispatcher::main(argc, argv);
}
