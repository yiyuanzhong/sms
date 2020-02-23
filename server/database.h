#ifndef SMS_SERVER_DATABASE_H
#define SMS_SERVER_DATABASE_H

#include <stdint.h>

#include <list>
#include <memory>
#include <string>

#include "sms/server/db.h"

namespace sql {
class Connection;
class Driver;
class PreparedStatement;
} // namespace sql

class Database {
public:
    Database();
    ~Database();

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
    bool PrepareCall();
    bool PreparePDU();
    bool PrepareSMS();
    bool PrepareSelect();
    bool PrepareDelete();
    bool PrepareArchive();

    int GetLastInsertID();

private:
    sql::Driver *const _driver;
    std::unique_ptr<sql::Connection> _conn;

    std::unique_ptr<sql::PreparedStatement> _pspdu;
    std::unique_ptr<sql::PreparedStatement> _pssms;
    std::unique_ptr<sql::PreparedStatement> _pscall;
    std::unique_ptr<sql::PreparedStatement> _psselect;
    std::unique_ptr<sql::PreparedStatement> _psdelete;
    std::unique_ptr<sql::PreparedStatement> _psarchive;

}; // class Database

#endif // SMS_SERVER_DATABASE_H
