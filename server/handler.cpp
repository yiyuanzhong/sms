#include "sms/server/handler.h"

#include <stdint.h>

#include <flinter/types/tree.h>

#include <flinter/convert.h>
#include <flinter/encode.h>
#include <flinter/utility.h>

#include "sms/server/configure.h"
#include "sms/server/processor.h"

class Handler {
public:
    explicit Handler(Processor *processor);
    int Run(const std::string &payload, std::string *response);

protected:
    bool ProcessPdu(const flinter::Tree &t);
    bool ProcessCall(const flinter::Tree &t);

    bool ProcessCallOld(const flinter::Tree &t);
    bool ProcessSmsOld(const flinter::Tree &t);

    int FindDevice(const std::string &token) const;

private:
    int _device;
    int64_t _uploaded;
    Processor *const _processor;

}; // class Handler

Handler::Handler(Processor *processor)
        : _device(-1)
        , _uploaded(-1)
        , _processor(processor)
{
    // Intended left blank
}

int Handler::FindDevice(const std::string &token) const
{
    const flinter::Tree &c = (*g_configure)["device"];
    for (flinter::Tree::const_iterator p = c.begin(); p != c.end(); ++p) {
        const flinter::Tree &i = *p;
        if (i["token"].compare(token) == 0) {
            return flinter::convert<int>(i.key());
        }
    }

    return -1;
}

int Handler::Run(const std::string &payload, std::string *response)
{
    _uploaded = get_wall_clock_timestamp();

    flinter::Tree r;
    if (!r.ParseFromJsonString(payload)) {
        return 400;
    }

    const std::string &token = r["token"];
    _device = FindDevice(token);
    if (_device < 0) {
        return 403;
    }

    bool good = true;
    const flinter::Tree &call = r["call"];
    for (flinter::Tree::const_iterator p = call.begin(); p != call.end(); ++p) {
        good &= ProcessCall(*p);
    }

    const flinter::Tree &pdu = r["pdu"];
    for (flinter::Tree::const_iterator p = pdu.begin(); p != pdu.end(); ++p) {
        good &= ProcessPdu(*p);
    }

    const flinter::Tree &sms = r["sms"];
    for (flinter::Tree::const_iterator p = sms.begin(); p != sms.end(); ++p) {
        good &= ProcessSmsOld(*p);
    }

    if (!good) {
        return 400;
    }

    response->assign("{\"ret\":0}");
    return 202;
}

bool Handler::ProcessCallOld(const flinter::Tree &t)
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

    std::unique_ptr<db::Call> r(new db::Call);
    r->device    = _device;
    r->timestamp = timestamp;
    r->uploaded  = _uploaded;
    r->peer      = peer;
    r->duration  = duration;
    r->type      = type;

    return _processor->Received(std::move(r)) >= 0;
}

bool Handler::ProcessCall(const flinter::Tree &t)
{
    bool v;

    if (t.Has("from")) {
        return ProcessCallOld(t);
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
    // peer can be empty if its identity was withheld by operator

    const std::string &raw = t["raw"];

    std::unique_ptr<db::Call> r(new db::Call);
    r->device    = _device;
    r->timestamp = timestamp;
    r->uploaded  = _uploaded;
    r->peer      = peer;
    r->duration  = duration;
    r->type      = type;
    r->raw       = raw;

    return _processor->Received(std::move(r)) >= 0;
}

bool Handler::ProcessSmsOld(const flinter::Tree &t)
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

    std::unique_ptr<db::SMS> r(new db::SMS);
    r->device   = _device;
    r->type     = type;
    r->sent     = sent;
    r->received = received;
    r->peer     = peer;
    r->subject  = subject;
    r->body     = body;

    return _processor->Received(std::move(r)) >= 0;
}

bool Handler::ProcessPdu(const flinter::Tree &t)
{
    bool v;

    const int64_t timestamp = flinter::convert<int64_t>(t["timestamp"], 0, &v);
    if (!v) {
        return false;
    }

    const std::string &type = t["type"];
    if (type.empty()) {
        return false;
    }

    if (type != "Incoming" && type != "Outgoing") {
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

    std::unique_ptr<db::PDU> r(new db::PDU);
    r->device    = _device;
    r->timestamp = timestamp;
    r->uploaded  = _uploaded;
    r->type      = type;
    r->pdu       = hex;

    return _processor->Received(std::move(r)) >= 0;
}

int handle(const std::string &payload,
           std::string *response,
           void *processor)
{
    Processor *const c = reinterpret_cast<Processor *>(processor);

    Handler server(c);
    return server.Run(payload, response);
}
