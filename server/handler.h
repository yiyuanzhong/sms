#ifndef SMS_SERVER_HANDLER_H
#define SMS_SERVER_HANDLER_H

#include <string>

extern int handle(const std::string &payload,
                  std::string *response,
                  void *processor);

#endif // SMS_SERVER_HANDLER_H
