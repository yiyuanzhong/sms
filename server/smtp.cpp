#include "sms/server/smtp.h"

#include <string.h>

#define CURL_STRICTER
#include <curl/curl.h>

#include <flinter/types/tree.h>
#include <flinter/types/uuid.h>
#include <flinter/encode.h>
#include <flinter/logger.h>

#include "sms/server/configure.h"

SMTP::SMTP() : _curl(nullptr)
             , _tlist(nullptr)
             , _dlist(nullptr)
             , _email(nullptr)
             , _uploaded(0)
{
    // Intended left blank
}

SMTP::~SMTP()
{
    Disconnect();
}

bool SMTP::Connect(const std::string &to)
{
    if (_curl) {
        return true;
    }

    const flinter::Tree &c = (*g_configure)["smtp"];
    const std::string &username = c["username"];
    const std::string &password = c["password"];
    const std::string &resolve  = c["resolve"];
    const std::string &cainfo   = c["cainfo"];
    const std::string &from     = c["from"];
    const std::string &url      = c["url"];
    constexpr long kConnectTimeout = 5;
    constexpr long kTimeout = 5;

    curl_slist *tlist = curl_slist_append(nullptr, to.c_str());
    if (!tlist) {
        return false;
    }

    curl_slist *dlist = nullptr;
    if (!resolve.empty()) {
        dlist = curl_slist_append(nullptr, resolve.c_str());
        if (!dlist) {
            curl_slist_free_all(tlist);
            return false;
        }
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        curl_slist_free_all(dlist);
        curl_slist_free_all(tlist);
        return false;
    }

    if (curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectTimeout) ||
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, kTimeout)               ||
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str())                ||
        curl_easy_setopt(curl, CURLOPT_USERNAME, username.c_str())      ||
        curl_easy_setopt(curl, CURLOPT_PASSWORD, password.c_str())      ||
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, ReadFunction)      ||
        curl_easy_setopt(curl, CURLOPT_READDATA, this)                  ||
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L)                      ||
#ifndef NDEBUG
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L)                     ||
#endif
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from.c_str())         ||
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, tlist)                ){

        curl_slist_free_all(dlist);
        curl_slist_free_all(tlist);
        curl_easy_cleanup(curl);
        return false;
    }

    if (dlist && curl_easy_setopt(curl, CURLOPT_RESOLVE, dlist)) {
        curl_slist_free_all(dlist);
        curl_slist_free_all(tlist);
        curl_easy_cleanup(curl);
        return false;
    }

    if (!cainfo.empty()) {
        if (curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL)     ||
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L)          ||
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L)          ||
            curl_easy_setopt(curl, CURLOPT_CAINFO, cainfo.c_str())      ){

            curl_slist_free_all(dlist);
            curl_slist_free_all(tlist);
            curl_easy_cleanup(curl);
            return false;
        }
    }

    _dlist = dlist;
    _tlist = tlist;
    _curl = curl;
    return true;
}

void SMTP::Disconnect()
{
    if (!_curl) {
        return;
    }

    curl_slist_free_all(_dlist);
    _dlist = nullptr;

    curl_slist_free_all(_tlist);
    _tlist = nullptr;

    curl_easy_cleanup(_curl);
    _curl = nullptr;
}

std::string SMTP::date(time_t when, long tz)
{
    static const char *W[] = {
            "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

    static const char *M[] = {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    char buffer[64];
    struct tm tm;
    const time_t t = when - tz;
    const long atz = tz < 0 ? -tz : tz;
    gmtime_r(&t, &tm);

    snprintf(buffer, sizeof(buffer), "%s, %d %s %d %02d:%02d:%02d %c%02ld%02ld",
            W[tm.tm_wday], tm.tm_mday, M[tm.tm_mon], tm.tm_year + 1900,
            tm.tm_hour, tm.tm_min, tm.tm_sec,
            tz < 0 ? '+' : '-',
            atz / 3600, atz % 3600 / 60);

    return buffer;
}

bool SMTP::Send(
        const std::string &to,
        const std::string &receiver,
        const std::string &body,
        const std::string &content_type,
        time_t when)
{
    if (when < 0) {
        when = time(nullptr);
    }

    std::string base64;
    if (flinter::EncodeBase64(body, &base64)) {
        return false;
    }

    const flinter::Tree &c = (*g_configure)["smtp"];
    const std::string &from = c["from"];
    const std::string &domain = from.substr(from.find('@') + 1);

    std::ostringstream s;
    s << "MIME-Version: 1.0\r\n";
    s << "Message-ID: <" << flinter::Uuid::CreateRandom().str() << "@" << domain << ">\r\n";
    s << "Date: " << date(when, timezone) << "\r\n";
    s << "From: " << c["sender"] << " <" << from << ">\r\n";
    s << "To: " << receiver << " <" << to << ">\r\n";
    s << "Subject: " << c["subject"] << "\r\n";
    s << "Content-Type: " << content_type << "\r\n";
    s << "Content-Transfer-Encoding: base64\r\n";
    s << "\r\n";
    s << base64;

    const std::string &email = s.str();

    if (c["disabled"].as<int>()) {
        printf("=== SMTP ===\n%s\n=== SMTP ===\n", email.c_str());

    } else {
        if (!Connect(to)) {
            return false;
        }

        _uploaded = 0;
        _email = &email;
        CURLcode ret = curl_easy_perform(_curl);
        _email = nullptr;

        Disconnect();
        if (ret) {
            CLOG.Warn("curl_easy_perform() = %d", ret);
        }
    }

    return true;
}

size_t SMTP::ReadFunction(
        char *buffer, size_t size, size_t nitems, void *userdata)
{
    SMTP *const smtp = reinterpret_cast<SMTP *>(userdata);
    return smtp->read(buffer, size, nitems);
}

size_t SMTP::read(char *buffer, size_t size, size_t nitems)
{
    const size_t max = size * nitems;
    const size_t total = _email->length();
    const size_t remaining = total - _uploaded;
    const size_t now = std::min(max, remaining);

    if (now) {
        memcpy(buffer, _email->data() + _uploaded, now);
        _uploaded += now;
    }

    return now;
}
