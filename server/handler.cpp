#include "sms/server/handler.h"

#include <stdint.h>

#include <fstream>
#include <list>
#include <sstream>

#include <flinter/types/tree.h>

#include <flinter/convert.h>
#include <flinter/encode.h>
#include <flinter/logger.h>
#include <flinter/utility.h>

#include "sms/server/configure.h"
#include "sms/server/database.h"
#include "sms/server/db.h"
#include "sms/server/pdu.h"
#include "sms/server/processor.h"
#include "sms/server/smtp.h"

class Handler {
public:
    explicit Handler(Processor *processor);
    int Run(const std::string &payload, std::string *response);

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
    Processor *const _processor;
    std::unique_ptr<Database> _db;

}; // class Handler

std::string Handler::FormatDuration(int64_t t)
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

std::string Handler::FormatTime(int64_t t)
{
    const time_t s = t / 1000000000;
    char buffer[64];
    struct tm tm;

    localtime_r(&s, &tm);
    sprintf(buffer, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buffer;
}

std::string Handler::FormatDate(int64_t t)
{
    const time_t s = t / 1000000000;
    char buffer[64];
    struct tm tm;

    localtime_r(&s, &tm);
    sprintf(buffer, "%04d-%02d-%02d",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);

    return buffer;
}

std::string Handler::FormatDateTime(int64_t t)
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

Handler::Handler(Processor *processor)
        : _device(-1)
        , _uploaded(-1)
        , _processor(processor)
        , _db(new Database)
{
    // Intended left blank
}

int Handler::FindDevice(
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

int Handler::Run(const std::string &payload, std::string *response)
{
    _uploaded = get_wall_clock_timestamp();

    std::ofstream of("/tmp/upload.txt", std::ios::out);
    of << payload;
    of.close();

    flinter::Tree r;
    if (!r.ParseFromJsonString(payload)) {
        return 400;
    }

    const std::string &token = r["token"];
    std::string receiver;
    std::string to;
    bool has_smsc;

    _device = FindDevice(token, &receiver, &to, &has_smsc);
    if (_device < 0) {
        return 403;
    }

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

    bool good = true;
    size_t items = 0;
    const flinter::Tree &call = r["call"];
    for (flinter::Tree::const_iterator p = call.begin(); p != call.end(); ++p, ++items) {
        good &= ProcessCall(*p, m);
    }

    const flinter::Tree &pdu = r["pdu"];
    if (pdu.children_size()) {
        good &= ProcessPdu(pdu, m, has_smsc);
        items += pdu.children_size();
    }

    const flinter::Tree &sms = r["sms"];
    for (flinter::Tree::const_iterator p = sms.begin(); p != sms.end(); ++p, ++items) {
        good &= ProcessSms(*p, m);
    }

    if (!good) {
        return 400;
    }

    if (items) {
        m << "</table>\n"
          << "</body>\n"
          << "</html>\n";

        SMTP smtp;
        if (!smtp.Send(to, receiver, m.str(),
                "text/html; charset=UTF-8", _uploaded / 1000000000)) {

            CLOG.Warn("Failed to send email, still continue...");
        }
    }

    response->assign("{\"ret\":0}");
    return 202;
}

bool Handler::ProcessCallOld(const flinter::Tree &t, std::ostringstream &m)
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

    db::Call record;
    record.device    = _device;
    record.timestamp = timestamp;
    record.uploaded  = _uploaded;
    record.peer      = peer;
    record.duration  = duration;
    record.type      = type;

    return _db->InsertCall(record) >= 0;
}

bool Handler::ProcessCall(const flinter::Tree &t, std::ostringstream &m)
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

    db::Call record;
    record.device    = _device;
    record.timestamp = timestamp;
    record.uploaded  = _uploaded;
    record.peer      = peer;
    record.duration  = duration;
    record.type      = type;
    record.raw       = raw;

    return _db->InsertCall(record) >= 0;
}

bool Handler::ProcessSms(const flinter::Tree &t, std::ostringstream &m)
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

    db::SMS record;
    record.device   = _device;
    record.type     = type;
    record.sent     = sent;
    record.received = received;
    record.peer     = peer;
    record.subject  = subject;
    record.body     = body;

    return _db->InsertSMS(record) >= 0;
}

bool Handler::ProcessPdu(
        const flinter::Tree &tree,
        std::ostringstream &m,
        bool has_smsc)
{
    bool good = true;
    std::list<std::pair<int64_t, pdu::PDU>> pdus;
    for (flinter::Tree::const_iterator p = tree.begin(); p != tree.end(); ++p) {
        bool v;
        const flinter::Tree &t = *p;
        const int64_t timestamp = flinter::convert<int64_t>(t["timestamp"], 0, &v);
        if (!v) {
            good = false;
            continue;
        }

        const std::string &type = t["type"];
        if (type.empty()) {
            good = false;
            continue;
        }

        bool sending;
        if (type == "Incoming") {
            sending = false;
        } else if (type == "Outgoing") {
            sending = true;
        } else {
            good = false;
            continue;
        }

        const std::string &pdu = t["pdu"];
        if (pdu.empty()) {
            good = false;
            continue;
        }

        std::string hex;
        if (flinter::DecodeHex(pdu, &hex)) {
            good = false;
            continue;
        }

        db::PDU record;
        record.device    = _device;
        record.timestamp = timestamp;
        record.uploaded  = _uploaded;
        record.type      = type;
        record.pdu       = hex;

        record.id = _db->InsertPDU(record);
        if (record.id < 0) {
            good = false;
            continue;
        } else if (record.id == 0) {
            continue;
        }

        _processor->Received(record);
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

    return good;
}

std::shared_ptr<const pdu::ConcatenatedShortMessages>
Handler::GetConcatenatedShortMessages(const pdu::PDU &pdu)
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

void Handler::Print(
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

void Handler::Print(
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

int handle(const std::string &payload,
           std::string *response,
           void *processor)
{
    Processor *const c = reinterpret_cast<Processor *>(processor);

    Handler server(c);
    return server.Run(payload, response);
}
