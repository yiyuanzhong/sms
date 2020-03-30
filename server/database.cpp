#include "sms/server/database.h"

#include <assert.h>
#include <string.h>

#include <mysql/mysql.h>

#include <flinter/types/tree.h>
#include <flinter/encode.h>
#include <flinter/logger.h>

#include "sms/server/configure.h"

class Database::Context {
public:
    MYSQL *_conn;
    MYSQL_STMT *_pspdu;
    MYSQL_STMT *_pssms;
    MYSQL_STMT *_pscall;
    MYSQL_STMT *_psselect;
    MYSQL_STMT *_psdelete;
    MYSQL_STMT *_psarchive;
}; // class Database::Context

static MYSQL_STMT *Prepare(MYSQL *conn, const char *statement)
{
    assert(conn);

    MYSQL_STMT *const st = mysql_stmt_init(conn);
    if (!st) {
        CLOG.Warn("mysql_stmt_init() = %d: %s",
                mysql_errno(conn), mysql_error(conn));

        return nullptr;
    }

    const unsigned long length = strlen(statement);
    if (mysql_stmt_prepare(st, statement, length)) {
        CLOG.Warn("mysql_stmt_prepare(%s) = %d: %s", statement,
                mysql_stmt_errno(st), mysql_stmt_error(st));

        mysql_stmt_close(st);
        return nullptr;
    }

    return st;
}

static int Insert(MYSQL_STMT *st, MYSQL_BIND *bind)
{
    if (mysql_stmt_bind_param(st, bind)) {
        CLOG.Warn("mysql_stmt_bind_param() = %d: %s",
                mysql_stmt_errno(st), mysql_stmt_error(st));

        return -1;
    }

    if (mysql_stmt_execute(st)) {
        if (mysql_stmt_errno(st) == 1062) {
            return 0;
        }

        CLOG.Warn("mysql_stmt_execute() = %d: %s",
                mysql_stmt_errno(st), mysql_stmt_error(st));

        return -1;
    }

    return static_cast<int>(mysql_stmt_insert_id(st));
}

static int Delete(MYSQL_STMT *st, int id)
{
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &id;

    if (mysql_stmt_bind_param(st, bind)) {
        CLOG.Warn("mysql_stmt_bind_param() = %d: %s",
                mysql_stmt_errno(st), mysql_stmt_error(st));

        return -1;
    }

    if (mysql_stmt_execute(st)) {
        CLOG.Warn("mysql_stmt_execute() = %d: %s",
                mysql_stmt_errno(st), mysql_stmt_error(st));

        return -1;
    }

    return static_cast<int>(mysql_stmt_affected_rows(st));
}

static void Close(MYSQL_STMT *&st)
{
    if (st) {
        mysql_stmt_close(st);
        st = nullptr;
    }
}

bool Database::Initialize()
{
    int ret = mysql_library_init(0, nullptr, nullptr);
    if (ret) {
        CLOG.Error("mysql_library_init() = %d", ret);
        return false;
    }

    if (!mysql_thread_safe()) {
        CLOG.Error("mysql_thread_safe() = 0");
        mysql_library_end();
        return false;
    }

    return true;
}

void Database::Cleanup()
{
    mysql_library_end();
}

bool Database::ThreadInitialize()
{
    return !!mysql_thread_init();
}

void Database::ThreadCleanup()
{
    mysql_thread_end();
}

Database::Database() : _c(new Context)
{
    memset(_c, 0, sizeof(*_c));
}

Database::~Database()
{
    Disconnect();
    delete _c;
}

bool Database::Connect()
{
    if (_c->_conn) {
        return true;
    }

    const flinter::Tree &c = (*g_configure)["database"];
    const uint16_t    port   = c["port"].as<uint16_t>();
    const char *const user   = c["username"].c_str();
    const char *const passwd = c["password"].c_str();
    const char *const db     = c["database"].c_str();
    const char *const host   = c["host"].c_str();

    _c->_conn = mysql_init(nullptr);
    if (!_c->_conn) {
        return false;
    }

    if (mysql_options(_c->_conn, MYSQL_SET_CHARSET_NAME, "utf8mb4")       ||
        mysql_options(_c->_conn, MYSQL_INIT_COMMAND, "SET NAMES utf8mb4") ){

        mysql_close(_c->_conn);
        _c->_conn = nullptr;
        return false;
    }

    if (!mysql_real_connect(_c->_conn,
            host,
            user,
            passwd,
            db,
            port,
            nullptr,
            CLIENT_IGNORE_SIGPIPE)) {

        CLOG.Error("mysql_real_connect(%s, %u, %s, %s, %s) = %s",
                host, port, user, "********", db, mysql_error(_c->_conn));
        mysql_close(_c->_conn);
        _c->_conn = nullptr;
        return false;
    }

    return true;
}

void Database::Disconnect()
{
    if (!_c->_conn) {
        return;
    }

    Close(_c->_pspdu);
    Close(_c->_pssms);
    Close(_c->_pscall);
    Close(_c->_psselect);
    Close(_c->_psdelete);
    Close(_c->_psarchive);

    mysql_close(_c->_conn);
    _c->_conn = nullptr;
}

bool Database::PrepareCall()
{
    if (_c->_pscall) {
        return true;
    }

    return !!(_c->_pscall = Prepare(_c->_conn,
            "INSERT INTO `call` (`device`, `timestamp`, `uploaded`, `peer`, "
            "`duration`, `type`, `raw`) VALUES(?, ?, ?, ?, ?, ?, ?)"));
}

int Database::InsertCall(const db::Call &call)
{
    if (Disabled()) {
        printf("=== SQL ===\nCALL %d %ld %ld %s %ld %s %s\n=== SQL ===\n",
               call.device, call.timestamp, call.uploaded, call.peer.c_str(),
               call.duration, call.type.c_str(), call.raw.c_str());
        return 0;
    }

    if (!Connect() || !PrepareCall()) {
        return -1;
    }

    unsigned long peer_length = call.peer.length();
    unsigned long type_length = call.type.length();
    unsigned long raw_length  = call.raw.length();

    MYSQL_BIND bind[7];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = const_cast<int *>(&call.device);

    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = const_cast<int64_t *>(&call.timestamp);

    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = const_cast<int64_t *>(&call.uploaded);

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer_length = peer_length;
    bind[3].length = &peer_length;
    bind[3].buffer = const_cast<char *>(call.peer.data());

    bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[4].buffer = const_cast<int64_t *>(&call.duration);

    bind[5].buffer_type = MYSQL_TYPE_STRING;
    bind[5].buffer_length = type_length;
    bind[5].length = &type_length;
    bind[5].buffer = const_cast<char *>(call.type.data());

    bind[6].buffer_type = MYSQL_TYPE_STRING;
    bind[6].buffer_length = raw_length;
    bind[6].length = &raw_length;
    bind[6].buffer = const_cast<char *>(call.raw.data());

    return Insert(_c->_pscall, bind);
}

bool Database::PreparePDU()
{
    if (_c->_pspdu) {
        return true;
    }

    return !!(_c->_pspdu = Prepare(_c->_conn,
            "INSERT INTO `pdu` (`device`, `timestamp`, `uploaded`, `type`, "
            "`pdu`) VALUES(?, ?, ?, ?, ?)"));
}

bool Database::PrepareArchive()
{
    if (_c->_psarchive) {
        return true;
    }

    return !!(_c->_psarchive = Prepare(_c->_conn,
            "INSERT INTO `archive` (`sms_id`, `device`, `timestamp`, "
            "`uploaded`, `type`, `pdu`) VALUES(?, ?, ?, ?, ?, ?)"));
}

int Database::InsertPDU(const db::PDU &pdu)
{
    if (Disabled()) {
        printf("=== SQL ===\nPDU %d %ld %ld %s %s\n=== SQL ===\n",
               pdu.device, pdu.timestamp, pdu.uploaded, pdu.type.c_str(),
               flinter::EncodeHex(pdu.pdu).c_str());
        return 0;
    }

    if (!Connect() || !PreparePDU()) {
        return -1;
    }

    unsigned long type_length = pdu.type.length();
    unsigned long pdu_length = pdu.pdu.length();

    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = const_cast<int *>(&pdu.device);

    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = const_cast<int64_t *>(&pdu.timestamp);

    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = const_cast<int64_t *>(&pdu.uploaded);

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer_length = type_length;
    bind[3].length = &type_length;
    bind[3].buffer = const_cast<char *>(pdu.type.data());

    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer_length = pdu_length;
    bind[4].length = &pdu_length;
    bind[4].buffer = const_cast<char *>(pdu.pdu.data());

    return Insert(_c->_pspdu, bind);
}

bool Database::PrepareSMS()
{
    if (_c->_pssms) {
        return true;
    }

    return !!(_c->_pssms = Prepare(_c->_conn,
            "INSERT INTO `sms` (`device`, `type`, `sent`, `received`, "
            "`peer`, `subject`, `body`) VALUES(?, ?, ?, ?, ?, ?, ?)"));
}

static int DoInsertSMS(MYSQL_STMT *st, const db::SMS &sms)
{
    unsigned long subject_length = sms.subject.length();
    unsigned long type_length = sms.type.length();
    unsigned long peer_length = sms.peer.length();
    unsigned long body_length = sms.body.length();

    MYSQL_BIND bind[7];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = const_cast<int *>(&sms.device);

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer_length = type_length;
    bind[1].length = &type_length;
    bind[1].buffer = const_cast<char *>(sms.type.data());

    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = const_cast<int64_t *>(&sms.sent);

    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = const_cast<int64_t *>(&sms.received);

    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer_length = peer_length;
    bind[4].length = &peer_length;
    bind[4].buffer = const_cast<char *>(sms.peer.data());

    bind[5].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[5].buffer_length = subject_length;
    bind[5].length = &subject_length;
    bind[5].buffer = const_cast<char *>(sms.subject.data());

    bind[6].buffer_type = MYSQL_TYPE_STRING;
    bind[6].buffer_length = body_length;
    bind[6].length = &body_length;
    bind[6].buffer = const_cast<char *>(sms.body.data());

    return Insert(st, bind);
}

int Database::InsertSMS(const db::SMS &sms)
{
    if (Disabled()) {
        printf("=== SQL ===\nSMS %d %s %ld %ld %s %s %s\n=== SQL ===\n",
               sms.device, sms.type.c_str(), sms.sent, sms.received,
               sms.peer.c_str(), sms.subject.c_str(), sms.body.c_str());
        return 0;
    }

    if (!Connect() || !PrepareSMS()) {
        return -1;
    }

    return DoInsertSMS(_c->_pssms, sms);
}

bool Database::PrepareSelect()
{
    if (_c->_psselect) {
        return true;
    }

    return !!(_c->_psselect = Prepare(_c->_conn, "SELECT `id`, `device`, "
                "`timestamp`, `uploaded`, `type`, `pdu` FROM `pdu`"));
}

bool Database::PrepareDelete()
{
    if (_c->_psdelete) {
        return true;
    }

    return !!(_c->_psdelete = Prepare(_c->_conn,
            "DELETE FROM `pdu` WHERE `id` = ?"));
}

bool Database::Select(std::list<db::PDU> *pdu)
{
    if (!Connect() || !PrepareSelect()) {
        return false;
    }

    if (mysql_stmt_execute(_c->_psselect)) {
        CLOG.Warn("mysql_stmt_execute() = %d: %s",
                mysql_stmt_errno(_c->_psselect),
                mysql_stmt_error(_c->_psselect));
        return false;
    }

    int id;
    int device;
    int64_t timestamp;
    int64_t uploaded;
    char type[32];
    char spdu[256];

    unsigned long type_length;
    unsigned long spdu_length;

    MYSQL_BIND bind[6];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &id;

    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &device;

    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = &timestamp;

    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = &uploaded;

    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer_length = sizeof(type);
    bind[4].length = &type_length;
    bind[4].buffer = type;

    bind[5].buffer_type = MYSQL_TYPE_STRING;
    bind[5].buffer_length = sizeof(spdu);
    bind[5].length = &spdu_length;
    bind[5].buffer = spdu;

    if (mysql_stmt_bind_result(_c->_psselect, bind)) {
        CLOG.Warn("mysql_stmt_bind_result() = %d: %s",
                mysql_stmt_errno(_c->_psselect),
                mysql_stmt_error(_c->_psselect));

        mysql_stmt_free_result(_c->_psselect);
        return false;
    }

    pdu->clear();
    bool result = false;
    while (true) {
        const int ret = mysql_stmt_fetch(_c->_psselect);
        if (ret == MYSQL_NO_DATA) {
            result = true;
            break;

        } else if (ret == 1) {
            CLOG.Warn("mysql_stmt_fetch() = %d: %s",
                    mysql_stmt_errno(_c->_psselect),
                    mysql_stmt_error(_c->_psselect));
            break;

        } else if (ret == MYSQL_DATA_TRUNCATED) {
            CLOG.Warn("mysql_stmt_fetch() = MYSQL_DATA_TRUNCATED");
            break;

        } else if (ret) {
            CLOG.Warn("mysql_stmt_fetch() = %d", ret);
            break;
        }

        db::PDU p;
        p.id        = id;
        p.device    = device;
        p.timestamp = timestamp;
        p.uploaded  = uploaded;
        p.type.assign(type, type_length);
        p.pdu.assign(spdu, spdu_length);
        pdu->push_back(p);
    }

    mysql_stmt_free_result(_c->_psselect);
    return result;
}

static int DoInsertArchive(
        MYSQL_STMT *st,
        int sms_id,
        const db::PDU &pdu)
{
    unsigned long type_length = pdu.type.length();
    unsigned long pdu_length = pdu.pdu.length();

    MYSQL_BIND bind[6];
    memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &sms_id;

    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = const_cast<int *>(&pdu.device);

    bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[2].buffer = const_cast<int64_t *>(&pdu.timestamp);

    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = const_cast<int64_t *>(&pdu.uploaded);

    bind[4].buffer_type = MYSQL_TYPE_STRING;
    bind[4].buffer_length = type_length;
    bind[4].length = &type_length;
    bind[4].buffer = const_cast<char *>(pdu.type.data());

    bind[5].buffer_type = MYSQL_TYPE_STRING;
    bind[5].buffer_length = pdu_length;
    bind[5].length = &pdu_length;
    bind[5].buffer = const_cast<char *>(pdu.pdu.data());

    return Insert(st, bind);
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

    if (mysql_query(_c->_conn, "START TRANSACTION")) {
        CLOG.Warn("mysql_query() = %d: %s",
                mysql_errno(_c->_conn), mysql_error(_c->_conn));
        return false;
    }

    db::SMS sms;
    sms.device   = pdu.front().device;
    sms.type     = pdu.front().type;
    sms.sent     = sent;
    sms.received = received;
    sms.peer     = peer;
    sms.subject  = subject;
    sms.body     = body;

    const int sms_id = DoInsertSMS(_c->_pssms, sms);
    if (sms_id < 0) {
        mysql_rollback(_c->_conn);
        return false;
    }

    for (auto &&p : pdu) {
        int ret = Delete(_c->_psdelete, p.id);
        if (ret != 1) {
            CLOG.Warn("Deleteing pdu[%d] but affected %d rows", p.id, ret);
            mysql_rollback(_c->_conn);
            return false;
        }

        if (sms_id) {
            ret = DoInsertArchive(_c->_psarchive, sms_id, p);
            if (ret < 0) {
                mysql_rollback(_c->_conn);
                return false;
            }
        }
    }

    if (mysql_commit(_c->_conn)) {
        CLOG.Warn("mysql_commit() = %d: %s",
                mysql_errno(_c->_conn), mysql_error(_c->_conn));
        mysql_rollback(_c->_conn);
        return false;
    }

    return true;
}

bool Database::Disabled() const
{
    const flinter::Tree &c = (*g_configure)["database"];
    return !!c["disabled"].as<int>();
}
