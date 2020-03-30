#ifndef SMS_SERVER_DATABASE_H
#define SMS_SERVER_DATABASE_H

#include <stdint.h>

#include <list>
#include <memory>
#include <string>

#include "sms/server/db.h"

class Database {
public:
    Database();
    ~Database();

    static bool ThreadInitialize();
    static void ThreadCleanup();
    static bool Initialize();
    static void Cleanup();

    void Disconnect();

    int InsertCall(const db::Call &call);

    int InsertPDU(const db::PDU &pdu);

    int InsertSMS(const db::SMS &sms);

    bool Select(std::list<db::PDU> *pdu);

    bool InsertArchive(
            const std::list<db::PDU> &pdu,
            int64_t sent,
            int64_t received,
            const std::string &peer,
            const std::string &subject,
            const std::string &body);

protected:
    bool Disabled() const;
    bool Connect();
    bool PreparePDU();
    bool PrepareSMS();
    bool PrepareCall();
    bool PrepareSelect();
    bool PrepareDelete();
    bool PrepareArchive();

private:
    class Context;
    Context *const _c;

}; // class Database

#endif // SMS_SERVER_DATABASE_H
