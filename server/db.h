#ifndef SMS_SERVER_DB_H
#define SMS_SERVER_DB_H

namespace db {

class Call {
public:
    Call() : id(0), device(0), timestamp(0), uploaded(0), duration(0) {}
    int id;
    int device;
    int64_t timestamp;
    int64_t uploaded;
    std::string peer;
    int64_t duration;
    std::string type;
    std::string raw;
}; // class Call

class SMS {
public:
    SMS() : id(0), device(0), sent(0), received(0) {}
    int id;
    int device;
    std::string type;
    int64_t sent;
    int64_t received;
    std::string peer;
    std::string subject;
    std::string body;
}; // class SMS

class PDU {
public:
    PDU() : id(0), device(0), timestamp(0), uploaded(0) {}
    int id;
    int device;
    int64_t timestamp;
    int64_t uploaded;
    std::string type;
    std::string pdu;
}; // class PDU

} // namespace db

#endif // SMS_SERVER_DB_H
