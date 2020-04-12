#ifndef SMS_SERVER_SPLITTER_H
#define SMS_SERVER_SPLITTER_H

#include <list>
#include <memory>
#include <unordered_map>

#include "sms/server/db.h"
#include "sms/server/pdu.h"

class Splitter {
public:
    bool Add(const db::PDU &db);
    void Split();

    template <class F>
    bool Process(F &&f)
    {
        for (auto p = _deliver.begin(); p != _deliver.end();) {
            if (!f(p->_sms, p->_pdus, p->_duplicates)) {
                return false;
            }

            p = _deliver.erase(p);
        }

        return true;
    }

private:
    class Deliver {
    public:
        db::PDU _db;
        std::shared_ptr<const pdu::Deliver> _pdu;
        std::shared_ptr<const pdu::ConcatenatedShortMessages> _c;
    }; // class Deliver

    class Submit {
    public:
        db::PDU _db;
        std::shared_ptr<const pdu::Submit> _pdu;
        std::shared_ptr<const pdu::ConcatenatedShortMessages> _c;
    }; // class Submit

    class Done {
    public:
        db::SMS _sms;
        std::list<db::PDU> _pdus;
        std::list<db::PDU> _duplicates;
    }; // class Done

    static bool FindDevice(int device, bool *has_smsc);
    void Split(std::list<Deliver> *input);

    void Finish(const std::list<Deliver> &delivers,
                const std::list<Deliver> &duplicates);

    void Finish(const std::list<Submit> &sumits,
                const std::list<Submit> &duplicates);

    std::list<Done> _submit;
    std::list<Done> _deliver;
    std::unordered_map<uint16_t, std::list<Submit>> _submits;
    std::unordered_map<uint16_t, std::list<Deliver>> _delivers;

}; // class Splitter

#endif // SMS_SERVER_SPLITTER_H
