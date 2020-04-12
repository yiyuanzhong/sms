#include "sms/server/splitter.h"

#include <flinter/types/tree.h>
#include <flinter/logger.h>

#include "sms/server/configure.h"

template <class T>
inline bool is_mms(const T &t)
{
    return !!t->TPUserDataHeader.GetApplicationPortAddressingScheme();
}

bool Splitter::FindDevice(int device, bool *has_smsc)
{
    const flinter::Tree &c = (*g_configure)["device"];
    std::ostringstream s;
    s << device;
    std::string str = s.str();

    if (!c.Has(str)) {
        return false;
    }

    const flinter::Tree &i = c[str];
    *has_smsc = !i.Has("has_smsc") || !!i["has_smsc"].as<int>();
    return true;
}

bool Splitter::Add(const db::PDU &db)
{
    bool has_smsc;
    if (!FindDevice(db.device, &has_smsc)) {
        return false;
    }

    const bool sending = (db.type == "Outgoing");
    pdu::PDU pdu(db.pdu, sending, has_smsc);
    if (pdu.result() != pdu::Result::OK) {
        return false;
    }

    if (pdu.type() == pdu::Type::Deliver) {
        Deliver d;
        d._pdu = pdu.deliver();
        if (is_mms(d._pdu)) {
            return true;
        }

        d._db = db;
        d._c = d._pdu->TPUserDataHeader.GetConcatenatedShortMessages();
        if (!(d._c)) {
            Finish({d}, {});
            return true;
        }

        _delivers[d._c->ReferenceNumber].push_back(d);
        return true;

    } else if (pdu.type() == pdu::Type::Submit) {
        Submit s;
        s._pdu = pdu.submit();
        if (is_mms(s._pdu)) {
            return true;
        }

        s._db = db;
        s._c = s._pdu->TPUserDataHeader.GetConcatenatedShortMessages();
        if (!s._c) {
            Finish({s}, {});
            return true;
        }

        _submits[s._c->ReferenceNumber].push_back(s);
        return true;
    }

    return false;
}

void Splitter::Split()
{
    for (std::unordered_map<uint16_t, std::list<Deliver>>::iterator
         p = _delivers.begin(); p != _delivers.end(); ++p) {

        Split(&p->second);
    }
}

void Splitter::Split(std::list<Deliver> *input)
{
    constexpr time_t kMaximumSending = 300; // 5 minutes

    std::list<Deliver> duplicates;
    std::list<std::list<Deliver>> output;
    for (std::list<Deliver>::iterator p = input->begin(); p != input->end();) {
        auto t = p++;
        bool found = false;
        for (auto &&q : output) {
            const time_t diff = t->_pdu->TPServiceCentreTimeStamp
                              - q.front()._pdu->TPServiceCentreTimeStamp;

            if (t->_pdu->TPOriginatingAddress == q.front()._pdu->TPOriginatingAddress &&
                diff >= -kMaximumSending && diff <= kMaximumSending                   ){

                q.splice(q.end(), *input, t);
                found = true;
                break;
            }
        }

        if (!found) {
            output.push_back({});
            output.back().splice(output.back().end(), *input, t);
        }
    }

    for (auto &&one : output) {
        one.sort([] (const Deliver &a, const Deliver &b) -> bool {
            return a._c->Sequence < b._c->Sequence ||
                   (a._c->Sequence == b._c->Sequence &&
                    a._db.timestamp < b._db.timestamp);
        });

        for (auto p = one.begin();;) {
            auto q = p++;
            if (p == one.end()) {
                break;
            }

            if (q->_c->Sequence == p->_c->Sequence         ||
                q->_pdu->TPUserData != p->_pdu->TPUserData ){

                continue;
            }

            duplicates.splice(duplicates.end(), one, p);
            p = q;
        }

        if (one.size() != one.front()._c->Maximum) {
            input->splice(input->end(), one);
            continue;
        }

        uint16_t expected = 1;
        for (std::list<Deliver>::const_iterator
             q = one.begin(); q != one.end(); ++q, ++expected) {

            if (q->_c->Sequence != expected) {
                input->splice(input->end(), one);
                break;
            }
        }

        Finish(one, duplicates);
    }
}

void Splitter::Finish(
        const std::list<Deliver> &delivers,
        const std::list<Deliver> &duplicates)
{
    auto p = delivers.begin();
    const int device = p->_db.device;
    const std::string type = p->_db.type;
    const std::string peer = p->_pdu->TPOriginatingAddress;

    Done done;
    done._pdus.push_back(p->_db);
    int64_t received = p->_db.timestamp;
    std::string body = p->_pdu->TPUserData;
    time_t sent = p->_pdu->TPServiceCentreTimeStamp;

    for (++p; p != delivers.end(); ++p) {
        done._pdus.push_back(p->_db);
        body.append(p->_pdu->TPUserData);
        received = std::max(received, p->_db.timestamp);
        sent = std::min(sent, p->_pdu->TPServiceCentreTimeStamp);
    }

    for (auto &&d : duplicates) {
        done._duplicates.push_back(d._db);
    }

    done._sms.device   = device;
    done._sms.type     = type;
    done._sms.sent     = sent * 1000000000LL;
    done._sms.received = received;
    done._sms.peer     = peer;
    done._sms.body     = body;

    _deliver.push_back(done);
}

void Splitter::Finish(
        const std::list<Submit> &submits,
        const std::list<Submit> &duplicates)
{
    auto p = submits.begin();
    const int device = p->_db.device;
    const std::string type = p->_db.type;
    const std::string peer = p->_pdu->TPDestinationAddress;

    Done done;
    done._pdus.push_back(p->_db);
    int64_t received = p->_db.timestamp;
    std::string body = p->_pdu->TPUserData;

    for (++p; p != submits.end(); ++p) {
        done._pdus.push_back(p->_db);
        body.append(p->_pdu->TPUserData);
        received = std::max(received, p->_db.timestamp);
    }

    for (auto &&d : duplicates) {
        done._duplicates.push_back(d._db);
    }

    done._sms.device   = device;
    done._sms.type     = type;
    done._sms.received = received;
    done._sms.peer     = peer;
    done._sms.body     = body;

    _deliver.push_back(done);
}
