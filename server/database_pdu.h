#ifndef SMS_SERVER_DATABASE_PDU_H
#define SMS_SERVER_DATABASE_PDU_H

class DatabasePDU {
public:
    int id;
    int device;
    int64_t timestamp;
    int64_t uploaded;
    std::string type;
    std::string pdu;
}; // class DatabasePDU

#endif // SMS_SERVER_DATABASE_PDU_H
