#include "sms/server/database.h"

#include <cppconn/driver.h>
#include <cppconn/connection.h>
#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>

#include <flinter/types/tree.h>
#include <flinter/encode.h>
#include <flinter/logger.h>

#include "sms/server/configure.h"

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
        CLOG.Warn("Database exception: %d: %s", e.getErrorCode(), e.what());
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
        CLOG.Warn("Database exception: %d: %s", e.getErrorCode(), e.what());
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
        CLOG.Warn("Database exception: %d: %s", e.getErrorCode(), e.what());
        return false;
    }
}

int Database::InsertCall(const db::Call &call)
{
    if (Disabled()) {
        printf("=== SQL ===\nCALL %d %ld %ld %s %ld %s %s\n=== SQL ===\n",
               call.device, call.timestamp, call.uploaded, call.peer.c_str(),
               call.duration, call.type.c_str(), call.raw.c_str());
        return 0;

    } else if (!Connect() || !PrepareCall()) {
        return -1;
    }

    try {
        _pscall->setInt   (1, call.device);
        _pscall->setInt64 (2, call.timestamp);
        _pscall->setInt64 (3, call.uploaded);
        _pscall->setString(4, call.peer);
        _pscall->setInt64 (5, call.duration);
        _pscall->setString(6, call.type);
        _pscall->setString(7, call.raw);
        _pscall->execute();
        return GetLastInsertID();

    } catch (const sql::SQLException &e) {
        if (e.getErrorCode() == 1062) {
            return 0;
        }

        CLOG.Warn("Database exception: %d: %s", e.getErrorCode(), e.what());
        return -1;
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
        CLOG.Warn("Database exception: %d: %s", e.getErrorCode(), e.what());
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
        CLOG.Warn("Database exception: %d: %s", e.getErrorCode(), e.what());
        return false;
    }
}

int Database::InsertPDU(const db::PDU &pdu)
{
    if (Disabled()) {
        printf("=== SQL ===\nPDU %d %ld %ld %s %s\n=== SQL ===\n",
               pdu.device, pdu.timestamp, pdu.uploaded, pdu.type.c_str(),
               flinter::EncodeHex(pdu.pdu).c_str());
        return 0;

    } else if (!Connect() || !PreparePDU()) {
        return -1;
    }

    try {
        _pspdu->setInt   (1, pdu.device);
        _pspdu->setInt64 (2, pdu.timestamp);
        _pspdu->setInt64 (3, pdu.uploaded);
        _pspdu->setString(4, pdu.type);
        _pspdu->setString(5, pdu.pdu);
        _pspdu->execute();
        return GetLastInsertID();

    } catch (const sql::SQLException &e) {
        if (e.getErrorCode() == 1062) {
            return 0;
        }

        CLOG.Warn("Database exception: %d: %s", e.getErrorCode(), e.what());
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
        CLOG.Warn("Database exception: %d: %s", e.getErrorCode(), e.what());
        return false;
    }
}

int Database::InsertSMS(const db::SMS &sms)
{
    if (Disabled()) {
        printf("=== SQL ===\nSMS %d %s %ld %ld %s %s %s\n=== SQL ===\n",
               sms.device, sms.type.c_str(), sms.sent, sms.received,
               sms.peer.c_str(), sms.subject.c_str(), sms.body.c_str());
        return 0;

    } else if (!Connect() || !PrepareSMS()) {
        return -1;
    }

    try {
        _pssms->setInt   (1, sms.device);
        _pssms->setString(2, sms.type);
        _pssms->setInt64 (3, sms.sent);
        _pssms->setInt64 (4, sms.received);
        _pssms->setString(5, sms.peer);
        _pssms->setString(6, sms.subject);
        _pssms->setString(7, sms.body);
        _pssms->execute();
        return GetLastInsertID();

    } catch (const sql::SQLException &e) {
        if (e.getErrorCode() == 1062) {
            return 0;
        }

        CLOG.Warn("Database exception: %d: %s", e.getErrorCode(), e.what());
        return -1;
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
        CLOG.Warn("Database exception: %d: %s", e.getErrorCode(), e.what());
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
        CLOG.Warn("Database exception: %d: %s", e.getErrorCode(), e.what());
        return false;
    }
}

bool Database::Select(std::list<db::PDU> *pdu)
{
    if (!Connect() || !PrepareSelect()) {
        return false;
    }

    try {
        std::unique_ptr<sql::ResultSet> rs(_psselect->executeQuery());
        pdu->clear();
        while (rs->next()) {
            db::PDU p;
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
        CLOG.Warn("Database exception: %d: %s", e.getErrorCode(), e.what());
        return false;
    }
}

bool Database::InsertArchive(
        const std::list<db::PDU> &pdu,
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
        CLOG.Warn("Database exception: %d: %s", e.getErrorCode(), e.what());
        return false;
    }
}

bool Database::Disabled() const
{
    const flinter::Tree &c = (*g_configure)["database"];
    return !!c["disabled"].as<int>();
}

int Database::GetLastInsertID()
{
    std::unique_ptr<sql::Statement> st(_conn->createStatement());
    std::unique_ptr<sql::ResultSet> rs(st->executeQuery(
                "SELECT LAST_INSERT_ID()"));

    if (!rs->next()) {
        return -1;
    }

    const int id = rs->getInt(1);
    return id;
}
