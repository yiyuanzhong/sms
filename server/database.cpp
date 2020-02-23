#include "sms/server/database.h"

#include <cppconn/driver.h>
#include <cppconn/connection.h>
#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>

#include <flinter/types/tree.h>
#include <flinter/encode.h>

#include "sms/server/configure.h"
#include "sms/server/database_pdu.h"

Database::Database()
        : _driver(get_driver_instance())
        , _conn(nullptr)
        , _pspdu(nullptr)
        , _pssms(nullptr)
        , _pscall(nullptr)
        , _psselect(nullptr)
        , _psarchive(nullptr)
{
    // Intended left blank
}

Database::~Database()
{
    Disconnect();
}

bool Database::Connect()
{
    if (_conn) {
        return true;
    }

    const flinter::Tree &c = (*g_configure)["database"];
    sql::ConnectOptionsMap m;
    m["hostName"] = c["url"].value();
    m["userName"] = c["username"].value();
    m["password"] = c["password"].value();

    try {
        _conn.reset(_driver->connect(m));
        std::unique_ptr<sql::Statement> s(_conn->createStatement());
        s->execute("SET NAMES utf8mb4");
        return true;

    } catch (const sql::SQLException &e) {
        fprintf(stderr, "Database exception: %s\n", e.what());
        Disconnect();
        return false;
    }
}

void Database::Disconnect()
{
    try {
        _psarchive.reset();
        _psselect.reset();
        _pscall.reset();
        _pssms.reset();
        _pspdu.reset();
        _conn.reset();
    } catch (const sql::SQLException &e) {
        // Eat it
    }
}

bool Database::PrepareCall()
{
    if (_pscall) {
        return true;
    }

    try {
        _pscall.reset(_conn->prepareStatement("INSERT INTO `call` "
                "(`device`, `timestamp`, `uploaded`, "
                "`peer`, `duration`, `type`, `raw`) "
                "VALUES(?, ?, ?, ?, ?, ?, ?)"));
        return true;

    } catch (const sql::SQLException &e) {
        fprintf(stderr, "Database exception: %s\n", e.what());
        return false;
    }
}

bool Database::InsertCall(
        int device,
        int64_t timestamp,
        int64_t uploaded,
        const std::string &peer,
        int64_t duration,
        const std::string &type,
        const std::string &raw)
{
    if (Disabled()) {
        printf("=== SQL ===\nCALL %d %ld %ld %s %ld %s %s\n=== SQL ===\n",
                device, timestamp, uploaded, peer.c_str(), duration,
                type.c_str(), raw.c_str());
        return true;

    } else if (!Connect() || !PrepareCall()) {
        return false;
    }

    try {
        _pscall->setInt   (1, device);
        _pscall->setInt64 (2, timestamp);
        _pscall->setInt64 (3, uploaded);
        _pscall->setString(4, peer);
        _pscall->setInt64 (5, duration);
        _pscall->setString(6, type);
        _pscall->setString(7, raw);
        _pscall->execute();
        return true;

    } catch (const sql::SQLException &e) {
        if (e.getErrorCode() == 1062) {
            return true;
        }

        fprintf(stderr, "Database exception: %s\n", e.what());
        return false;
    }
}

bool Database::PreparePDU()
{
    if (_pspdu) {
        return true;
    }

    try {
        _pspdu.reset(_conn->prepareStatement("INSERT INTO `pdu` "
                "(`device`, `timestamp`, `uploaded`, `type`, `pdu`) "
                "VALUES(?, ?, ?, ?, ?)"));
        return true;

    } catch (const sql::SQLException &e) {
        fprintf(stderr, "Database exception: %s\n", e.what());
        return false;
    }
}

bool Database::PrepareArchive()
{
    if (_psarchive) {
        return true;
    }

    try {
        _psarchive.reset(_conn->prepareStatement("INSERT INTO `archive` "
                "(`sms_id`, `device`, `timestamp`, `uploaded`, `type`, `pdu`) "
                "VALUES(?, ?, ?, ?, ?, ?)"));
        return true;

    } catch (const sql::SQLException &e) {
        fprintf(stderr, "Database exception: %s\n", e.what());
        return false;
    }
}

int Database::InsertPDU(
        int device,
        int64_t timestamp,
        int64_t uploaded,
        const std::string &type,
        const std::string &pdu)
{
    if (Disabled()) {
        printf("=== SQL ===\nPDU %d %ld %ld %s %s\n=== SQL ===\n",
                device, timestamp, uploaded, type.c_str(),
                flinter::EncodeHex(pdu).c_str());
        return 0;

    } else if (!Connect() || !PreparePDU()) {
        return -1;
    }

    try {
        _pspdu->setInt   (1, device);
        _pspdu->setInt64 (2, timestamp);
        _pspdu->setInt64 (3, uploaded);
        _pspdu->setString(4, type);
        _pspdu->setString(5, pdu);
        _pspdu->execute();

        std::unique_ptr<sql::Statement> st(_conn->createStatement());
        std::unique_ptr<sql::ResultSet> rs(st->executeQuery(
                    "SELECT LAST_INSERT_ID()"));

        if (!rs->next()) {
            return -1;
        }

        const int id = rs->getInt(1);
        return id;

    } catch (const sql::SQLException &e) {
        if (e.getErrorCode() == 1062) {
            return 0;
        }

        fprintf(stderr, "Database exception: %s\n", e.what());
        return -1;
    }
}

bool Database::PrepareSMS()
{
    if (_pssms) {
        return true;
    }

    try {
        _pssms.reset(_conn->prepareStatement("INSERT INTO `sms` "
                "(`device`, `type`, `sent`, `received`, ""`peer`, "
                "`subject`, `body`) VALUES(?, ?, ?, ?, ?, ?, ?)"));
        return true;

    } catch (const sql::SQLException &e) {
        fprintf(stderr, "Database exception: %s\n", e.what());
        return false;
    }
}

bool Database::InsertSMS(
        int device,
        const std::string &type,
        int64_t sent,
        int64_t received,
        const std::string &peer,
        const std::string &subject,
        const std::string &body)
{
    if (Disabled()) {
        printf("=== SQL ===\nSMS %d %s %ld %ld %s %s %s\n=== SQL ===\n",
                device, type.c_str(), sent, received, peer.c_str(),
                subject.c_str(), body.c_str());
        return true;

    } else if (!Connect() || !PrepareSMS()) {
        return false;
    }

    try {
        _pssms->setInt   (1, device);
        _pssms->setString(2, type);
        _pssms->setInt64 (3, sent);
        _pssms->setInt64 (4, received);
        _pssms->setString(5, peer);
        _pssms->setString(6, subject);
        _pssms->setString(7, body);
        _pssms->execute();
        return true;

    } catch (const sql::SQLException &e) {
        if (e.getErrorCode() == 1062) {
            return true;
        }

        fprintf(stderr, "Database exception: %s\n", e.what());
        return false;
    }
}

bool Database::PrepareSelect()
{
    if (_psselect) {
        return true;
    }

    try {
        _psselect.reset(_conn->prepareStatement("SELECT `id`, `device`, "
                "`timestamp`, `uploaded`, `type`, `pdu` FROM `pdu`"));
        return true;

    } catch (const sql::SQLException &e) {
        fprintf(stderr, "Database exception: %s\n", e.what());
        return false;
    }
}

bool Database::PrepareDelete()
{
    if (_psdelete) {
        return true;
    }

    try {
        _psdelete.reset(_conn->prepareStatement(
                "DELETE FROM `pdu` WHERE `id` = ?"));
        return true;

    } catch (const sql::SQLException &e) {
        fprintf(stderr, "Database exception: %s\n", e.what());
        return false;
    }
}

bool Database::Select(std::list<DatabasePDU> *pdu)
{
    if (!Connect() || !PrepareSelect()) {
        return false;
    }

    try {
        std::unique_ptr<sql::ResultSet> rs(_psselect->executeQuery());
        pdu->clear();
        while (rs->next()) {
            DatabasePDU p;
            p.id        = rs->getInt(1);
            p.device    = rs->getInt(2);
            p.timestamp = rs->getInt64(3);
            p.uploaded  = rs->getInt64(4);
            p.type      = rs->getString(5);
            p.pdu       = rs->getString(6);
            pdu->push_back(p);
        }

        return true;
    } catch (const sql::SQLException &e) {
        fprintf(stderr, "Database exception: %s\n", e.what());
        return false;
    }
}

bool Database::InsertArchive(
        const std::list<DatabasePDU> &pdu,
        int64_t sent,
        int64_t received,
        const std::string &peer,
        const std::string &subject,
        const std::string &body)
{
    if (!Connect() || !PrepareSMS() || !PrepareDelete() || !PrepareArchive()) {
        return false;
    }

    try {
        std::unique_ptr<sql::Statement> st(_conn->createStatement());
        st->execute("START TRANSACTION");

        _pssms->setInt   (1, pdu.front().device);
        _pssms->setString(2, pdu.front().type);
        _pssms->setInt64 (3, sent);
        _pssms->setInt64 (4, received);
        _pssms->setString(5, peer);
        _pssms->setString(6, subject);
        _pssms->setString(7, body);
        _pssms->execute();

        std::unique_ptr<sql::ResultSet> rs(st->executeQuery(
                    "SELECT LAST_INSERT_ID()"));

        if (!rs->next()) {
            st->execute("ROLLBACK");
            return false;
        }

        const int sms_id = rs->getInt(1);
        for (auto &&p : pdu) {
            _psdelete->setInt(1, p.id);
            _psdelete->execute();
        }

        for (auto &&p : pdu) {
            _psarchive->setInt   (1, sms_id);
            _psarchive->setInt   (2, p.device);
            _psarchive->setInt64 (3, p.timestamp);
            _psarchive->setInt64 (4, p.uploaded);
            _psarchive->setString(5, p.type);
            _psarchive->setString(6, p.pdu);
            _psarchive->execute();
        }

        st->execute("COMMIT");
        return true;

    } catch (const sql::SQLException &e) {
        fprintf(stderr, "Database exception: %s\n", e.what());
        return false;
    }
}

bool Database::Disabled() const
{
    const flinter::Tree &c = (*g_configure)["database"];
    return !!c["disabled"].as<int>();
}
