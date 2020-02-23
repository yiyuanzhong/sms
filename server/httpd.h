#ifndef SMS_SERVER_HTTPD_H
#define SMS_SERVER_HTTPD_H

#include <stddef.h>

struct MHD_Connection;
struct MHD_Daemon;

class Cleaner;

class HTTPD {
public:
    explicit HTTPD(Cleaner *cleaner);
    ~HTTPD();

    bool Start();
    void Stop();

    int Handler(
            struct MHD_Connection *connection,
            const char *url,
            const char *method,
            const char *version,
            const char *upload_data,
            size_t *upload_data_size,
            void **con_cls);

    void Completed(
            struct MHD_Connection *connection,
            void **con_cls,
            int toe);

protected:
    class Request;

    int StandardResponse(
            struct MHD_Connection *connection,
            const char *url,
            int status);

    int Error(struct MHD_Connection *connection) const;

private:
    Cleaner *const _cleaner;
    struct MHD_Daemon *_daemon;

}; // class HTTPD

#endif // SMS_SERVER_HTTPD_H
