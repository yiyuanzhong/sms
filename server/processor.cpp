#include "sms/server/processor.h"

#include <flinter/types/tree.h>
#include <flinter/logger.h>

#include "sms/server/configure.h"
#include "sms/server/database.h"

static void split(std::list<Processor::Deliver> *input,
                  std::list<std::list<Processor::Deliver>> *output,
                  std::list<Processor::Deliver> *duplicates)
{
    constexpr int64_t kMaximumReception = 86400000000000LL; // 1 day
    constexpr time_t kMaximumSending = 10000000000LL; // 10 seconds

    output->clear();
    duplicates->clear();
    for (std::list<Processor::Deliver>::iterator
         p = input->begin(); p != input->end();) {

        CLOG.Debug("%u %u %ld %s", p->_c->ReferenceNumber, p->_c->Sequence, p->_pdu->TPServiceCentreTimeStamp, p->_pdu->TPUserData.c_str());
        auto t = p++;
        bool found = false;
        for (auto &&q : *output) {
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
            output->push_back({});
            output->back().splice(output->back().end(), *input, t);
        }
    }

    for (auto &&one : *output) {
        one.sort([] (const Processor::Deliver &a, const Processor::Deliver &b) -> bool {
            return a._c->Sequence < b._c->Sequence ||
                   (a._c->Sequence == b._c->Sequence &&
                    a._db.timestamp < b._db.timestamp);
        });

        for (auto p = one.begin();;) {
            auto q = p++;
            if (p == one.end()) {
                break;
            }

            if (q->_c->Sequence != p->_c->Sequence         ||
                q->_pdu->TPUserData != p->_pdu->TPUserData ){

                continue;
            }

            const int64_t diff = q->_db.timestamp - p->_db.timestamp;
            if (diff < -kMaximumReception || diff > kMaximumReception) {
                continue;
            }

            duplicates->splice(duplicates->end(), one, p);
            p = q;
        }

        if (one.size() != one.front()._c->Maximum) {
            CLOG.Debug("wowowow1 %u %u %lu", one.front()._c->ReferenceNumber, one.front()._c->Maximum, one.size());
            input->splice(input->end(), one);
            continue;
        }

        uint16_t expected = 1;
        for (std::list<Processor::Deliver>::const_iterator
             q = one.begin(); q != one.end(); ++q, ++expected) {

            if (q->_c->Sequence != expected) {
                CLOG.Debug("wowowow2 %u %u %lu", one.front()._c->ReferenceNumber, one.front()._c->Maximum, one.size());
                input->splice(input->end(), one);
                break;
            }
        }
    }

    for (std::list<std::list<Processor::Deliver>>::iterator
         p = output->begin(); p != output->end();) {

        if (p->empty()) {
            p = output->erase(p);
        } else {
            ++p;
        }
    }

    CLOG.Debug("after split: %lu", input->size());
}

template <>
bool Processor::Execute(
        const std::list<Processor::Deliver> &pdus,
        const std::list<Processor::Deliver> &duplicates) const
{
    auto p = pdus.begin();
    int64_t received = p->_db.timestamp;
    std::string body = p->_pdu->TPUserData;
    time_t sent = p->_pdu->TPServiceCentreTimeStamp;
    const std::string peer = p->_pdu->TPOriginatingAddress;

    std::list<db::PDU> dp;
    dp.push_back(p->_db);

    for (++p; p != pdus.end(); ++p) {
        dp.push_back(p->_db);
        body.append(p->_pdu->TPUserData);
        received = std::max(received, p->_db.timestamp);
        sent = std::min(sent, p->_pdu->TPServiceCentreTimeStamp);
    }

    for (auto &&d : duplicates) {
        dp.push_back(d._db);
    }

    Database db;
    return db.InsertArchive(
            dp,
            sent * 1000000000LL,
            received,
            peer,
            std::string(),
            body);
}

bool Processor::FindDevice(int device, bool *has_smsc) const
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

bool Processor::Add(const db::PDU &db)
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

    std::shared_ptr<const pdu::ConcatenatedShortMessages> c;
    switch (pdu.type()) {
    case pdu::Type::Deliver:
        if (pdu.deliver()->TPUserDataHeader.GetApplicationPortAddressingScheme()) {
            return false;
        }

        c = pdu.deliver()->TPUserDataHeader.GetConcatenatedShortMessages();
        if (!c) {
            return Execute<Deliver>({Deliver(db, pdu.deliver(), nullptr)}, {});
        }

        _deliver[c->ReferenceNumber].push_back(Deliver(db, pdu.deliver(), c));
        return true;

    case pdu::Type::Submit:
        if (pdu.submit()->TPUserDataHeader.GetApplicationPortAddressingScheme()) {
            return false;
        }

        c = pdu.submit()->TPUserDataHeader.GetConcatenatedShortMessages();
        if (!c) {
            return true;
        }

        _submit[c->ReferenceNumber].push_back(Submit(db, pdu.submit(), c));
        return true;
    }

    return false;
}

void Processor::Split(std::list<Deliver> *delivers)
{
    std::list<Deliver> duplicates;
    std::list<std::list<Deliver>> table;
    split(delivers, &table, &duplicates);

    for (auto &&row : table) {
        if (!Execute<Deliver>(row, duplicates)) {
            CLOG.Debug("wowowow3 %u", row.front()._c->ReferenceNumber);
            continue;
        }
    }
}

bool Processor::Initialize()
{
    _deliver.clear();
    _submit.clear();

    Database db;
    std::list<db::PDU> all;
    if (!db.Select(&all)) {
        return false;
    }

    db.Disconnect();
    for (auto &&p : all) {
        Add(p);
    }

    Split();
    DebugPrint();
    return true;
}

void Processor::Split()
{
    for (std::map<uint16_t, std::list<Deliver>>::iterator
         p = _deliver.begin(); p != _deliver.end();) {

        Split(&p->second);
        if (p->second.empty()) {
            p = _deliver.erase(p);
        } else {
            ++p;
        }
    }

    // TODO(yiyuanzhong): _submit
}

void Processor::DebugPrint() const
{
    CLOG.Debug("=== Deliver ===");
    for (std::map<uint16_t, std::list<Deliver>>::const_iterator
         p = _deliver.begin(); p != _deliver.end(); ++p) {

        CLOG.Debug("-> %u", p->first);
        for (std::list<Deliver>::const_iterator
             q = p->second.begin(); q != p->second.end(); ++q) {

            CLOG.Debug("---> %u %u", q->_c->Maximum, q->_c->Sequence);
        }
    }

    CLOG.Debug("=== Submit ===");
    for (std::map<uint16_t, std::list<Submit>>::const_iterator
         p = _submit.begin(); p != _submit.end(); ++p) {

        CLOG.Debug("-> %u", p->first);
        for (std::list<Submit>::const_iterator
             q = p->second.begin(); q != p->second.end(); ++q) {

            CLOG.Debug("---> %u %u", q->_c->Maximum, q->_c->Sequence);
        }
    }
}

bool Processor::Received(const db::PDU &db)
{
    if (!Add(db)) {
        return false;
    }

    Split();
    DebugPrint();
    return true;
}

bool Processor::Shutdown()
{
    return true; // Nothing to do
}

bool Processor::Cleanup()
{
    return true;
}
