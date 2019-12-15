#ifndef SMS_SERVER_SERVER_H
#define SMS_SERVER_SERVER_H

#include <stddef.h>

extern int server_process(
        const std::string &payload,
        std::string *response);

#endif // SMS_SERVER_SERVER_H
