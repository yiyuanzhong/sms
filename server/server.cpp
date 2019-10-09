#include <stdint.h>

#include <sstream>

#include <flinter/fastcgi/cgi.h>
#include <flinter/fastcgi/common_filters.h>
#include <flinter/fastcgi/dispatcher.h>
#include <flinter/fastcgi/http_exception.h>

#include <flinter/types/tree.h>

#include <flinter/convert.h>
#include <flinter/encode.h>
#include <flinter/utility.h>

#include "sms/server/configure.h"
#include "sms/server/database.h"
#include "sms/server/smtp.h"

class Server : public flinter::CGI {
public:
    Server();
    virtual ~Server();
    virtual void Run();

protected:
    static std::string FormatTime(int64_t t);
    static std::string FormatDate(int64_t t);
    static std::string FormatDateTime(int64_t t);
    static std::string FormatDuration(int64_t t);

    bool ProcessCall(const flinter::Tree &t, std::ostringstream &m);
    bool ProcessPdu(const flinter::Tree &t, std::ostringstream &m);
    bool ProcessSms(const flinter::Tree &t, std::ostringstream &m);

    int FindDevice(const std::string &token,
            std::string *receiver,
            std::string *to) const;

private:
    int _device;
    int64_t _uploaded;
    std::unique_ptr<Database> _db;

}; // class Server

std::string Server::FormatDuration(int64_t t)
{
    const time_t s = t / 1000000000;
    char buffer[64];

    if (s < 60) {
        sprintf(buffer, "%lds", s);
    } else {
        sprintf(buffer, "%ldm%lds\n", s / 60, s % 60);
    }

    return buffer;
}

std::string Server::FormatTime(int64_t t)
{
    const time_t s = t / 1000000000;
    char buffer[64];
    struct tm tm;

    localtime_r(&s, &tm);
    sprintf(buffer, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buffer;
}

std::string Server::FormatDate(int64_t t)
{
    const time_t s = t / 1000000000;
    char buffer[64];
    struct tm tm;

    localtime_r(&s, &tm);
    sprintf(buffer, "%04d-%02d-%02d",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);

    return buffer;
}

std::string Server::FormatDateTime(int64_t t)
{
    const time_t s = t / 1000000000;
    char buffer[64];
    struct tm tm;

    localtime_r(&s, &tm);
    sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec);

    return buffer;
}

Server::Server() : _device(-1), _uploaded(-1), _db(new Database)
{
    AppendFilter(new flinter::NoCacheFilter);
    AppendFilter(new flinter::AllowedMethodsFilter("POST"));
    AppendFilter(new flinter::AllowedContentTypeFilter("application/json"));
}

Server::~Server()
{
    // Intended left blank
}

int Server::FindDevice(
        const std::string &token,
        std::string *receiver,
        std::string *to) const
{
    const flinter::Tree &c = (*g_configure)["device"];
    for (flinter::Tree::const_iterator p = c.begin(); p != c.end(); ++p) {
        const flinter::Tree &i = *p;
        if (i["token"].compare(token) == 0) {
            *receiver = i["receiver"];
            *to = i["to"];
            return flinter::convert<int>(i.key());
        }
    }

    return -1;
}

void Server::Run()
{
    _uploaded = get_wall_clock_timestamp();

    flinter::Tree r;
    if (!r.ParseFromJsonString(request_body())) {
        throw flinter::HttpException(400);
    }

    const std::string &token = r["token"];
    std::string receiver;
    std::string to;

    _device = FindDevice(token, &receiver, &to);
    if (_device < 0) {
        throw flinter::HttpException(403);
    }

    bool good = false;

    std::ostringstream m;
    m << "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" "
         "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n"
      << "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n"
      << "<head>\n"
      << "<style>\n"
      << "table { border: 1px solid black; border_collapse: collapse }\n"
      << "th { border: 1px solid black; border_collapse: collapse }\n"
      << "td { border: 1px solid black; border_collapse: collapse }\n"
      << "</style>\n"
      << "</head>\n"
      << "<body>\n"
      << "<table>\n";

    const flinter::Tree &call = r["call"];
    for (flinter::Tree::const_iterator p = call.begin(); p != call.end(); ++p) {
        if (!ProcessCall(*p, m)) {
            throw flinter::HttpException(400);
        }

        good = true;
    }

    const flinter::Tree &pdu = r["pdu"];
    for (flinter::Tree::const_iterator p = pdu.begin(); p != pdu.end(); ++p) {
        if (!ProcessPdu(*p, m)) {
            throw flinter::HttpException(400);
        }

        good = true;
    }

    const flinter::Tree &sms = r["sms"];
    for (flinter::Tree::const_iterator p = sms.begin(); p != sms.end(); ++p) {
        if (!ProcessSms(*p, m)) {
            throw flinter::HttpException(400);
        }

        good = true;
    }

    if (!good) {
        throw flinter::HttpException(400);
    }

    m << "</table>\n"
      << "</body>\n"
      << "</html>\n";

    SMTP smtp;
    if (!smtp.Send(to, receiver, m.str(),
            "text/html; charset=UTF-8", _uploaded / 1000000000)) {

        fprintf(stderr, "Failed to send email, still continue...\n");
    }

    SetStatusCode(202);
    SetContentType("application/json");
    BODY << "{\"ret\":0}";
}

bool Server::ProcessCall(const flinter::Tree &t, std::ostringstream &m)
{
    bool v;

    const int64_t timestamp = flinter::convert<int64_t>(t["timestamp"], 0, &v);
    if (!v) {
        return false;
    }

    const int64_t duration = flinter::convert<int64_t>(t["duration"], 0, &v);
    if (!v) {
        return false;
    }

    const std::string &type = t["type"];
    if (type.empty()) {
        return false;
    }

    const std::string &peer = t["peer"];
    if (peer.empty()) {
        return false;
    }

    const std::string &raw = t["raw"];

    m << "<tr>\n"
      << "<td>" << FormatDateTime(timestamp) << "</td>\n"
      << "<td>" << flinter::EscapeHtml(peer) << "</td>\n"
      << "<td>" << flinter::EscapeHtml(type) << "</td>\n"
      << "<td>" << FormatDuration(duration) << "</td>\n"
      << "</tr>\n";

    return _db->InsertCall(
            _device, timestamp, _uploaded, peer, duration, type, raw);
}

bool Server::ProcessSms(const flinter::Tree &t, std::ostringstream &m)
{
    bool v;

    const std::string &type = t["type"];
    if (type.empty()) {
        return false;
    }

    const int64_t sent = flinter::convert<int64_t>(t["sent"], 0, &v);
    if (!v) {
        return false;
    }

    const int64_t received = flinter::convert<int64_t>(t["received"], 0, &v);
    if (!v) {
        return false;
    }

    const std::string &peer = t["peer"];
    if (peer.empty()) {
        return false;
    }

    const std::string &subject = t["subject"];
    const std::string &body = t["body"];
    if (body.empty()) {
        return false;
    }

    m << "<tr>\n"
      << "<td>" << FormatDate(received) << "</td>\n"
      << "<td>" << FormatTime(sent) << "</td>\n"
      << "<td>" << FormatTime(received) << "</td>\n"
      << "<td>" << flinter::EscapeHtml(peer) << "</td>\n"
      << "</tr>\n"
      << "<tr><th colspan=\"4\">" << flinter::EscapeHtml(body) << "</th>\n"
      << "</tr>\n";

    return _db->InsertSMS(
            _device, type, sent, received, peer, subject, body);
}

bool Server::ProcessPdu(const flinter::Tree &t, std::ostringstream &m)
{
    bool v;

    (void)m;

    const int64_t timestamp = flinter::convert<int64_t>(t["timestamp"], 0, &v);
    if (!v) {
        return false;
    }

    const std::string &type = t["type"];
    if (type.empty()) {
        return false;
    }

    const std::string &pdu = t["pdu"];
    if (pdu.empty()) {
        return false;
    }

    std::string hex;
    if (flinter::DecodeHex(pdu, &hex)) {
        return false;
    }

    return _db->InsertPDU(
            _device, timestamp, _uploaded, type, hex);
}

CGI_DISPATCH(Server, "/");
