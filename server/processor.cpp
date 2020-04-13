#include "sms/server/processor.h"

#include <flinter/types/tree.h>
#include <flinter/encode.h>
#include <flinter/logger.h>

#include "sms/server/configure.h"
#include "sms/server/database.h"
#include "sms/server/smtp.h"
#include "sms/server/splitter.h"

Processor::Processor() : _splitter(new Splitter)
{
    // Intended left blank
}

Processor::~Processor()
{
    delete _splitter;
}

bool Processor::Initialize()
{
    CLOG.Trace("Processor: Initializing");
    InitializeDevices();
    CLOG.Trace("Processor: loaded %lu devices", _devices.size());

    Database db;
    std::list<db::PDU> all;
    if (!db.Select(&all)) {
        return false;
    }

    db.Disconnect();
    CLOG.Trace("Processor: loaded %lu PDUs", all.size());

    for (auto &&a : all) {
        _splitter->Add(a);
    }

    if (!all.empty()) {
        Split(std::chrono::steady_clock::now());
    }

    CLOG.Trace("Processor: initializing done");
    return true;
}

void Processor::InitializeDevices()
{
    const flinter::Tree &conf = (*g_configure)["device"];
    for (auto &&c : conf) {
        Device d;
        d._to = c["to"];
        d._receiver = c["receiver"];
        const int did = c.key_as<int>();
        _devices.insert(std::make_pair(did, d));
        CLOG.Trace("Processor: device %d -> %s <%s>",
                did, d._to.c_str(), d._receiver.c_str());
    }
}

void Processor::Split(const std::chrono::steady_clock::time_point &when)
{
    CLOG.Trace("Processor: split");

    _splitter->Split();
    _splitter->Process([this, &when](
            const db::SMS &sms,
            const std::list<db::PDU> &pdus,
            const std::list<db::PDU> &duplicated) -> bool {

        CLOG.Trace("Processor: assembled SMS for device %d "
                "out of %lu PDUs and %lu duplicated",
                sms.device, pdus.size(), duplicated.size());

        Database db;
        if (!db.InsertSMS(sms, pdus, duplicated)) {
            return false;
        }

        Finish(when, sms);
        return true;
    });
}

int Processor::Received(std::unique_ptr<db::PDU> r)
{
    const auto now = std::chrono::steady_clock::now();

    Database db;
    int ret = db.InsertPDU(*r);
    if (ret <= 0) {
        return ret;
    }

    r->id = ret;
    db.Disconnect();
    CLOG.Info("Processor: inserted PDU for device %d with id %d",
            r->device, r->id);

    Task task;
    task._when = now;
    task._pdu = std::move(r);

    std::lock_guard<std::mutex> locker(_mutex);
    _tasks.push_back(std::move(task));
    return ret;
}

int Processor::Received(std::unique_ptr<db::SMS> r)
{
    const auto now = std::chrono::steady_clock::now();

    Database db;
    int ret = db.InsertSMS(*r);
    if (ret <= 0) {
        return ret;
    }

    r->id = ret;
    db.Disconnect();
    CLOG.Info("Processor: inserted SMS for device %d with id %d",
            r->device, r->id);

    Task task;
    task._when = now;
    task._sms = std::move(r);

    std::lock_guard<std::mutex> locker(_mutex);
    _tasks.push_back(std::move(task));
    return ret;
}

int Processor::Received(std::unique_ptr<db::Call> r)
{
    const auto now = std::chrono::steady_clock::now();

    Database db;
    int ret = db.InsertCall(*r);
    if (ret <= 0) {
        return ret;
    }

    r->id = ret;
    db.Disconnect();
    CLOG.Info("Processor: inserted call for device %d with id %d",
            r->device, r->id);

    Task task;
    task._when = now;
    task._call = std::move(r);

    std::lock_guard<std::mutex> locker(_mutex);
    _tasks.push_back(std::move(task));
    return ret;
}

bool Processor::Shutdown()
{
    Flush(true);
    return true;
}

bool Processor::Cleanup()
{
    const auto now = std::chrono::steady_clock::now();

    std::unique_lock<std::mutex> locker(_mutex);
    std::list<Task> tasks;
    tasks.splice(tasks.end(), _tasks);
    locker.unlock();

    bool pdu = false;
    for (auto &&task : tasks) {
        if (task._call) {
            Finish(task._when, *task._call);

        } else if (task._sms) {
            Finish(task._when, *task._sms);

        } else if (task._pdu) {
            _splitter->Add(*task._pdu);
            pdu = true;

        } else {
            abort();
        }
    }

    if (pdu) {
        Split(now);
    }

    Flush(false);
    return true;
}

std::string Processor::FormatDuration(int64_t t)
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

std::string Processor::FormatTime(int64_t t)
{
    const time_t s = t / 1000000000;
    char buffer[64];
    struct tm tm;

    localtime_r(&s, &tm);
    sprintf(buffer, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buffer;
}

std::string Processor::FormatDate(int64_t t)
{
    const time_t s = t / 1000000000;
    char buffer[64];
    struct tm tm;

    localtime_r(&s, &tm);
    sprintf(buffer, "%04d-%02d-%02d",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);

    return buffer;
}

std::string Processor::FormatDateTime(int64_t t)
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

std::string Processor::Format(const db::SMS &sms)
{
    std::ostringstream s;
    s << "<tr>\n"
      << "<td>" << FormatDate(sms.sent)          << "</td>\n"
      << "<td>" << FormatTime(sms.sent)          << "</td>\n"
      << "<td>" << FormatTime(sms.received)      << "</td>\n"
      << "<td>" << flinter::EscapeHtml(sms.peer) << "</td>\n"
      << "</tr>\n"
      << "<tr>\n"
      << "<th colspan=\"4\">" << flinter::EscapeHtml(sms.body) << "</th>\n"
      << "</tr>\n";

    return s.str();
}

std::string Processor::Format(const db::Call &call)
{
    std::ostringstream s;
    s << "<tr>\n"
      << "<td>" << FormatDateTime(call.timestamp) << "</td>\n"
      << "<td>" << flinter::EscapeHtml(call.peer) << "</td>\n"
      << "<td>" << flinter::EscapeHtml(call.type) << "</td>\n"
      << "<td>" << FormatDuration(call.duration)  << "</td>\n"
      << "</tr>\n";

    return s.str();
}

std::string Processor::Format(
        const std::list<db::Call> &call,
        const std::list<db::SMS> &sms)
{
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

    for (auto &&p : call) {
        m << Format(p);
    }

    if (!call.empty() && !sms.empty()) {
        m << "<br /> \n";
    }

    for (auto &&p : sms) {
        m << Format(p);
    }

    m << "</table>\n"
      << "</body>\n"
      << "</html>\n";

    return m.str();
}

bool Processor::Send(
        const std::string &to,
        const std::string &receiver,
        const std::string &mail)
{
    SMTP smtp;
    return smtp.Send(to, receiver, mail, "text/html; charset=UTF-8");
}

void Processor::Finish(
        const std::chrono::steady_clock::time_point &when,
        const db::SMS &sms)
{
    constexpr auto kWait = std::chrono::seconds(5);

    const int did = sms.device;
    const auto p = _devices.find(did);
    if (p == _devices.end()) {
        abort();
        return;
    }

    Device &device = p->second;
    device._sms.push_back(sms);

    if (device._flush == std::chrono::steady_clock::time_point::min()) {
        device._flush = when + kWait;
    } else {
        device._flush = std::min(device._flush, when + kWait);
    }
}

void Processor::Finish(
        const std::chrono::steady_clock::time_point &when,
        const db::Call &call)
{
    const int did = call.device;
    const auto p = _devices.find(did);
    if (p == _devices.end()) {
        abort();
        return;
    }

    Device &device = p->second;
    device._call.push_back(call);

    if (device._flush == std::chrono::steady_clock::time_point::min()) {
        device._flush = when;
    } else {
        device._flush = std::min(device._flush, when);
    }
}

void Processor::Flush(bool force)
{
    constexpr size_t kMaximumCall = 50;
    constexpr size_t kMaximumSMS = 50;

    const auto now = std::chrono::steady_clock::now();
    for (auto &&p : _devices) {
        Device &device = p.second;
        if (device._call.empty() && device._sms.empty()) {
            continue;
        }

        if (!force) {
            if (now < device._flush) {
                continue;
            }
        }

        std::list<db::Call> call;
        if (device._call.size() < kMaximumCall) {
            call.splice(call.end(), device._call);
        } else {
            auto end = device._call.begin();
            std::advance(end, kMaximumCall);
            call.splice(call.end(), device._call, device._call.begin(), end);
        }

        std::list<db::SMS> sms;
        if (device._sms.size() < kMaximumSMS) {
            sms.splice(sms.end(), device._sms);
        } else {
            auto end = device._sms.begin();
            std::advance(end, kMaximumSMS);
            sms.splice(sms.end(), device._sms, device._sms.begin(), end);
        }

        const std::string &mail = Format(call, sms);
        CLOG.Trace("Processor: sending to %s <%s> with %lu calls and %lu SMS",
                device._to.c_str(), device._receiver.c_str(),
                call.size(), sms.size());

        if (Send(device._to, device._receiver, mail)) {
            CLOG.Info("Processor: sent to %s <%s> with %lu calls and %lu SMS",
                    device._to.c_str(), device._receiver.c_str(),
                    call.size(), sms.size());
        } else {
            CLOG.Warn("Processor: failed to send to %s <%s> "
                    "with %lu calls and %lu SMS",
                    device._to.c_str(), device._receiver.c_str(),
                    call.size(), sms.size());
        }
    }
}
