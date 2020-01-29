#ifndef SMS_SERVER_CLEANER_H
#define SMS_SERVER_CLEANER_H

#include <stdint.h>

#include <list>
#include <mutex>
#include <unordered_map>

#include "sms/server/database_pdu.h"
#include "sms/server/pdu.h"

class Cleaner {
public:
    bool Initialize();
    bool Shutdown();
    bool Clean();

    class Deliver {
    public:
        Deliver(const DatabasePDU &db,
                std::shared_ptr<const pdu::Deliver> pdu,
                std::shared_ptr<const pdu::ConcatenatedShortMessages> c)
                : _c(c), _pdu(pdu), _db(db) {}
        std::shared_ptr<const pdu::ConcatenatedShortMessages> _c;
        std::shared_ptr<const pdu::Deliver> _pdu;
        DatabasePDU _db;
    }; // class Message

    class Submit {
    public:
        Submit(const DatabasePDU &db,
               std::shared_ptr<const pdu::Submit> pdu,
               std::shared_ptr<const pdu::ConcatenatedShortMessages> c)
               : _c(c), _pdu(pdu), _db(db) {}
        std::shared_ptr<const pdu::ConcatenatedShortMessages> _c;
        std::shared_ptr<const pdu::Submit> _pdu;
        DatabasePDU _db;
    }; // class Submit

protected:
    bool FindDevice(int device, bool *has_smsc) const;
    std::map<uint16_t, std::list<Deliver>> _deliver;
    std::map<uint16_t, std::list<Submit>> _submit;
    std::mutex _mutex;

}; // class Cleaner

#endif // SMS_SERVER_CLEANER_H
