#ifndef SMS_SERVER_DATABASE_H
#define SMS_SERVER_DATABASE_H

#include <stdint.h>

#include <list>
#include <memory>
#include <string>

namespace sql {
class Connection;
class Driver;
class PreparedStatement;
} // namespace sql

class DatabasePDU;

class Database {
public:
    Database();
    ~Database();

    void Disconnect();

    bool InsertCall(
            int device,
            int64_t timestamp,
            int64_t uploaded,
            const std::string &peer,
            int64_t duration,
            const std::string &type,
            const std::string &raw);

    int InsertPDU(
            int device,
            int64_t timestamp,
            int64_t uploaded,
            const std::string &type,
            const std::string &pdu);

    bool InsertSMS(
            int device,
            const std::string &type,
            int64_t sent,
            int64_t received,
            const std::string &peer,
            const std::string &subject,
            const std::string &body);

    bool Select(
            std::list<DatabasePDU> *pdu);

    bool InsertArchive(
            const std::list<DatabasePDU> &pdu,
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
