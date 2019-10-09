#ifndef SMS_SERVER_SMTP_H
#define SMS_SERVER_SMTP_H

#include <string>

struct Curl_easy;
struct curl_slist;

class SMTP {
public:
    SMTP();
    ~SMTP();

    bool Send(const std::string &to,
              const std::string &receiver,
              const std::string &body,
              const std::string &content_type,
              time_t when = -1);

protected:
    void Disconnect();
    bool Connect(const std::string &to);

    static std::string date(time_t when, long tz);
    size_t read(char *buffer, size_t size, size_t nitems);
    static size_t ReadFunction(
            char *buffer, size_t size, size_t nitems, void *userdata);

private:
    struct Curl_easy *_curl;
    struct curl_slist *_tlist;
    struct curl_slist *_dlist;

    const std::string *_email;
    size_t _uploaded;

}; // class SMTP

#endif // SMS_SERVER_SMTP_H
