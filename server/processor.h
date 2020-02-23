#ifndef SMS_SERVER_PROCESSOR_H
#define SMS_SERVER_PROCESSOR_H

#include <stdint.h>

#include <condition_variable>
#include <list>
#include <mutex>
#include <unordered_map>

#include "sms/server/database_pdu.h"
#include "sms/server/pdu.h"

class Processor {
public:
    bool Initialize();
    bool Shutdown();
    bool Cleanup();

    bool Received(const DatabasePDU &db);

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
    void Split(std::list<Deliver> *delivers);
    bool Add(const DatabasePDU &db);
    void DebugPrint() const;
    void Split();

    template <class T>
    bool Execute(
            const std::list<T> &pdus,
            const std::list<T> &duplicates) const;

    // Only access from server thread, no need to lock
    std::map<uint16_t, std::list<Deliver>> _deliver;
    std::map<uint16_t, std::list<Submit>> _submit;

    // Access from both server thread and main thread
    std::list<std::pair<int64_t, std::function<bool ()>>> _done;
    std::condition_variable _condition;
    std::mutex _mutex;

}; // class Processor

#endif // SMS_SERVER_PROCESSOR_H
