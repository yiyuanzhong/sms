#include <stdint.h>

#include <fstream>
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
#include "sms/server/pdu.h"
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

    bool ProcessPdu(
            const flinter::Tree &tree,
            std::ostringstream &m,
            bool has_smsc);

    bool ProcessCall(
            const flinter::Tree &t,
            std::ostringstream &m);

    bool ProcessCallOld(
            const flinter::Tree &t,
            std::ostringstream &m);

    bool ProcessSms(
            const flinter::Tree &t,
            std::ostringstream &m);

    int FindDevice(const std::string &token,
            std::string *receiver,
            std::string *to,
            bool *has_smsc) const;

    static std::shared_ptr<const pdu::ConcatenatedShortMessages>
    GetConcatenatedShortMessages(const pdu::PDU &pdu);

    static void Print(
            int64_t timestamp,
            const pdu::Submit *pdu,
            std::ostringstream &m);

    static void Print(
            int64_t timestamp,
            const pdu::Deliver *pdu,
            std::ostringstream &m);

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
        std::string *to,
        bool *has_smsc) const
{
    const flinter::Tree &c = (*g_configure)["device"];
    for (flinter::Tree::const_iterator p = c.begin(); p != c.end(); ++p) {
        const flinter::Tree &i = *p;
        if (i["token"].compare(token) == 0) {
            *receiver = i["receiver"];
            *to = i["to"];
            *has_smsc = !i.Has("has_smsc") || !!i["has_smsc"].as<int>();
            return flinter::convert<int>(i.key());
        }
    }

    return -1;
}

void Server::Run()
{
    _uploaded = get_wall_clock_timestamp();

    std::ofstream of("/tmp/upload.txt", std::ios::out);
    of << request_body();
    of.close();

    flinter::Tree r;
    if (!r.ParseFromJsonString(request_body())) {
        throw flinter::HttpException(400);
    }

    const std::string &token = r["token"];
    std::string receiver;
    std::string to;
    bool has_smsc;

    _device = FindDevice(token, &receiver, &to, &has_smsc);
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
    if (pdu.children_size()) {
        if (!ProcessPdu(pdu, m, has_smsc)) {
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

bool Server::ProcessCallOld(const flinter::Tree &t, std::ostringstream &m)
{
    bool v;

    const int64_t timestamp = flinter::convert<int64_t>(t["timestamp"], 0, &v) * 1000000;
    if (!v) {
        return false;
    }

    const int64_t duration = flinter::convert<int64_t>(t["duration"], 0, &v) * 1000000000;
    if (!v) {
        return false;
    }

    const std::string &type = t["type"];
    if (type.empty()) {
        return false;
    }

    const std::string &peer = t["from"];
    if (peer.empty()) {
        return false;
    }

    m << "<tr>\n"
      << "<td>" << FormatDateTime(timestamp) << "</td>\n"
      << "<td>" << flinter::EscapeHtml(peer) << "</td>\n"
      << "<td>" << flinter::EscapeHtml(type) << "</td>\n"
      << "<td>" << FormatDuration(duration) << "</td>\n"
      << "</tr>\n";

    return _db->InsertCall(
            _device, timestamp, _uploaded, peer, duration, type, std::string());
}

bool Server::ProcessCall(const flinter::Tree &t, std::ostringstream &m)
{
    bool v;

    if (t.Has("from")) {
        return ProcessCallOld(t, m);
    }

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

    const std::string &type = "Incoming";

    const int64_t sent = flinter::convert<int64_t>(t["sent"], 0, &v) * 1000000;
    if (!v) {
        return false;
    }

    const int64_t received = flinter::convert<int64_t>(t["received"], 0, &v) * 1000000;
    if (!v) {
        return false;
    }

    const std::string &peer = t["from"];
    if (peer.empty()) {
        return false;
    }

    const std::string &subject = t["subject"];
    const std::string &body = t["body"];
    if (body.empty()) {
        return false;
    }

    m << "<tr>\n"
      << "<td>" << FormatDate(sent) << "</td>\n"
      << "<td>" << FormatTime(sent) << "</td>\n"
      << "<td>" << FormatTime(received) << "</td>\n"
      << "<td>" << flinter::EscapeHtml(peer) << "</td>\n"
      << "</tr>\n"
      << "<tr><th colspan=\"4\">" << flinter::EscapeHtml(body) << "</th>\n"
      << "</tr>\n";

    return _db->InsertSMS(
            _device, type, sent, received, peer, subject, body);
}

bool Server::ProcessPdu(
        const flinter::Tree &tree,
        std::ostringstream &m,
        bool has_smsc)
{
    std::list<std::pair<int64_t, pdu::PDU>> pdus;
    for (flinter::Tree::const_iterator p = tree.begin(); p != tree.end(); ++p) {
        bool v;
        const flinter::Tree &t = *p;
        const int64_t timestamp = flinter::convert<int64_t>(t["timestamp"], 0, &v);
        if (!v) {
            return false;
        }

        const std::string &type = t["type"];
        if (type.empty()) {
            return false;
        }

        bool sending;
        if (type == "Incoming") {
            sending = false;
        } else if (type == "Outgoing") {
            sending = true;
        } else {
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

        if (!_db->InsertPDU(_device, timestamp, _uploaded, type, hex)) {
            return false;
        }

        pdu::PDU s(hex, sending, has_smsc);
        switch (s.result()) {
        case pdu::Result::Failed:
            m << "<tr>\n"
              << "<td>" << FormatDate(timestamp) << "</td>\n"
              << "<td>" << FormatTime(timestamp) << "</td>\n"
              << "<td>" << flinter::EscapeHtml(type) << "</td>\n"
              << "<td>" << flinter::EscapeHtml("<error>") << "</td>\n"
              << "</tr>\n";
            break;

        case pdu::Result::OK:
            pdus.push_back(std::make_pair(timestamp, s));
            break;

        case pdu::Result::NotImplemented:
            m << "<tr>\n"
              << "<td>" << FormatDate(timestamp) << "</td>\n"
              << "<td>" << FormatTime(timestamp) << "</td>\n"
              << "<td>" << flinter::EscapeHtml(type) << "</td>\n"
              << "<td>" << flinter::EscapeHtml("<unsupported>") << "</td>\n"
              << "</tr>\n";
            break;
        }
    }

    pdus.sort([] (const std::pair<int64_t, pdu::PDU> &a,
                  const std::pair<int64_t, pdu::PDU> &b) -> bool {

        auto at = a.second.type();
        auto bt = b.second.type();
        if (at != bt) {
            return at < bt;
        }

        auto ac = GetConcatenatedShortMessages(a.second);
        auto bc = GetConcatenatedShortMessages(b.second);
        if (!ac && !bc) {
            return a.first < b.first;
        } else if (!ac) {
            return true;
        } else if (!bc) {
            return false;
        } else if (ac->ReferenceNumber == bc->ReferenceNumber) {
            return ac->Sequence < bc->Sequence;
        } else {
            return ac->ReferenceNumber < bc->ReferenceNumber;
        }
    });

    for (std::list<std::pair<int64_t, pdu::PDU>>::const_iterator
         p = pdus.begin(); p != pdus.end(); ++p) {

        switch (p->second.type()) {
        case pdu::Type::Deliver:
            Print(p->first, p->second.deliver().get(), m);
            break;

        case pdu::Type::Submit:
            Print(p->first, p->second.submit().get(), m);
            break;
        }
    }

    return true;
}

std::shared_ptr<const pdu::ConcatenatedShortMessages>
Server::GetConcatenatedShortMessages(const pdu::PDU &pdu)
{
    switch (pdu.type()) {
    case pdu::Type::Deliver:
        return pdu.deliver()->TPUserDataHeader.GetConcatenatedShortMessages();

    case pdu::Type::Submit:
        return pdu.submit()->TPUserDataHeader.GetConcatenatedShortMessages();

    default:
        return nullptr;
    }
}

void Server::Print(
        int64_t timestamp,
        const pdu::Deliver *pdu,
        std::ostringstream &m)
{
    const int64_t ts = static_cast<int64_t>(
            pdu->TPServiceCentreTimeStamp) * 1000000000;

    m << "<tr>\n"
      << "<td>" << FormatDate(ts) << "</td>\n"
      << "<td>" << FormatTime(ts) << "</td>\n"
      << "<td>" << FormatTime(timestamp) << "</td>\n"
      << "<td>" << flinter::EscapeHtml(pdu->TPOriginatingAddress) << "</td>\n"
      << "</tr>\n"
      << "<tr><th colspan=\"4\">"
      << flinter::EscapeHtml(pdu->TPUserData)
      << "</th>\n"
      << "</tr>\n";
}

void Server::Print(
        int64_t timestamp,
        const pdu::Submit *pdu,
        std::ostringstream &m)
{
    m << "<tr>\n"
      << "<td>" << FormatDate(timestamp) << "</td>\n"
      << "<td>" << FormatTime(timestamp) << "</td>\n"
      << "<td>" << flinter::EscapeHtml("Outgoing") << "</td>\n"
      << "<td>" << flinter::EscapeHtml(pdu->TPDestinationAddress) << "</td>\n"
      << "</tr>\n"
      << "<tr><th colspan=\"4\">"
      << flinter::EscapeHtml(pdu->TPUserData)
      << "</th>\n"
      << "</tr>\n";
}

CGI_DISPATCH(Server, "/");
