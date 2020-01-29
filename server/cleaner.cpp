#include "sms/server/cleaner.h"

#include <flinter/types/tree.h>

#include "sms/server/configure.h"
#include "sms/server/database.h"

static void split(std::list<Cleaner::Deliver> *input,
                  std::list<std::list<Cleaner::Deliver>> *output,
                  std::list<Cleaner::Deliver> *duplicates)
{
    constexpr int64_t kMaximum = 86400000000000LL; // 1 day

    output->clear();
    duplicates->clear();
    for (std::list<Cleaner::Deliver>::iterator
         p = input->begin(); p != input->end();) {

        auto t = p++;
        bool found = false;
        for (auto &&q : *output) {
            if (t->_pdu->TPServiceCentreTimeStamp == q.front()._pdu->TPServiceCentreTimeStamp &&
                t->_pdu->TPOriginatingAddress == q.front()._pdu->TPOriginatingAddress         ){

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
        one.sort([] (const Cleaner::Deliver &a, const Cleaner::Deliver &b) -> bool {
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

            int64_t diff = q->_db.timestamp - p->_db.timestamp;
            if (diff < -kMaximum || diff > kMaximum) {
                continue;
            }

            duplicates->splice(duplicates->end(), one, p);
            p = q;
        }

        if (one.size() != one.front()._c->Maximum) {
            printf("wowowow1 %u %u %lu\n", one.front()._c->ReferenceNumber, one.front()._c->Maximum, one.size());
            input->splice(input->end(), one);
            continue;
        }

        uint16_t expected = 1;
        for (std::list<Cleaner::Deliver>::const_iterator
             q = one.begin(); q != one.end(); ++q, ++expected) {

            if (q->_c->Sequence != expected) {
                printf("wowowow2 %u %u %lu\n", one.front()._c->ReferenceNumber, one.front()._c->Maximum, one.size());
                input->splice(input->end(), one);
                break;
            }
        }
    }

    for (std::list<std::list<Cleaner::Deliver>>::iterator
         p = output->begin(); p != output->end();) {

        if (p->empty()) {
            p = output->erase(p);
        } else {
            ++p;
        }
    }

    printf("after split: %lu\n", input->size());
}

static bool execute(
        const std::list<Cleaner::Deliver> &pdus,
        const std::list<Cleaner::Deliver> &duplicates)
{
    auto p = pdus.begin();
    int64_t received = p->_db.timestamp;
    std::string body = p->_pdu->TPUserData;
    time_t sent = p->_pdu->TPServiceCentreTimeStamp;
    const std::string peer = p->_pdu->TPOriginatingAddress;

    std::list<DatabasePDU> dp;
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

bool Cleaner::FindDevice(int device, bool *has_smsc) const
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

bool Cleaner::Initialize()
{
    _deliver.clear();
    _submit.clear();

    Database db;
    std::list<DatabasePDU> all;
    if (!db.Select(&all)) {
        return false;
    }

    db.Disconnect();
    for (auto &&p : all) {
        bool has_smsc;
        if (!FindDevice(p.device, &has_smsc)) {
            continue;
        }

        const bool sending = (p.type == "Outgoing");
        pdu::PDU pdu(p.pdu, sending, has_smsc);
        if (pdu.result() != pdu::Result::OK) {
            continue;
        }

        std::shared_ptr<const pdu::ConcatenatedShortMessages> c;
        switch (pdu.type()) {
        case pdu::Type::Deliver:
            if (pdu.deliver()->TPUserDataHeader.GetApplicationPortAddressingScheme()) {
                continue;
            }

            c = pdu.deliver()->TPUserDataHeader.GetConcatenatedShortMessages();
            if (!c) {
                execute({Deliver(p, pdu.deliver(), nullptr)}, {});
                continue;
            }

            _deliver[c->ReferenceNumber].push_back(Deliver(p, pdu.deliver(), c));
            break;
        case pdu::Type::Submit:
            if (pdu.submit()->TPUserDataHeader.GetApplicationPortAddressingScheme()) {
                continue;
            }

            c = pdu.submit()->TPUserDataHeader.GetConcatenatedShortMessages();
            if (!c) {
                continue;
            }

            _submit[c->ReferenceNumber].push_back(Submit(p, pdu.submit(), c));
            break;
        }
    }

    for (std::map<uint16_t, std::list<Deliver>>::iterator
         p = _deliver.begin(); p != _deliver.end();) {

        std::list<std::list<Deliver>> pdus;
        std::list<Deliver> duplicates;
        split(&p->second, &pdus, &duplicates);

        for (auto &&q : pdus) {
            if (!execute(q, duplicates)) {
                printf("wowowow3 %u\n", q.front()._c->ReferenceNumber);
                continue;
            }
        }

        if (p->second.empty()) {
            p = _deliver.erase(p);
        } else {
            ++p;
        }
    }

    return true;
}

bool Cleaner::Shutdown()
{
    return true; // Nothing to do
}

bool Cleaner::Clean()
{
    return true;
}
