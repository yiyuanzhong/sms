#ifndef SMS_SERVER_PDU_H
#define SMS_SERVER_PDU_H

#include <stdint.h>
#include <time.h>

#include <map>
#include <memory>
#include <string>

namespace pdu {

enum class Result {
    OK             = 0,
    Failed         = 1,
    NotImplemented = 2,
}; // enum class Result

enum class Type {
    Submit,
    Deliver,
}; // enum class Type

class ConcatenatedShortMessages {
public:
    uint16_t ReferenceNumber;
    uint8_t  Maximum;
    uint8_t  Sequence;
}; // class ConcatenatedShortMessages

class UserDataHeader {
public:
    void Decode(const void *buffer, size_t udhlen);

    std::shared_ptr<const ConcatenatedShortMessages>
    GetConcatenatedShortMessages() const;

private:
    std::multimap<uint8_t, std::string> _m;

}; // class UserDataHeader

class Deliver {
public:
    uint8_t TPMessageTypeIndicator0  :1;
    uint8_t TPMessageTypeIndicator1  :1;
    uint8_t TPMoreMessagesToSend     :1;
    uint8_t TPStatusReportIndication :1;
    uint8_t TPUserDataHeaderIndicator:1;
    uint8_t TPReplyPath              :1;

    std::string     TPOriginatingAddress;
    uint8_t         TPProtocolIdentifier;
    uint8_t         TPDataCodingScheme;
    time_t          TPServiceCentreTimeStamp;
    UserDataHeader  TPUserDataHeader;
    std::string     TPUserData;
}; // class Deliver

class Submit {
public:
    uint8_t TPMessageTypeIndicator0  :1;
    uint8_t TPMessageTypeIndicator1  :1;
    uint8_t TPRejectDuplicates       :1;
    uint8_t TPValidityPeriodFormat3  :1;
    uint8_t TPValidityPeriodFormat4  :1;
    uint8_t TPReplyPath              :1;
    uint8_t TPUserDataHeaderIndicator:1;
    uint8_t TPStatusReportRequest    :1;

    uint8_t         TPMessageReference;
    std::string     TPDestinationAddress;
    uint8_t         TPProtocolIdentifier;
    uint8_t         TPDataCodingScheme;
    time_t          TPValidityPeriod;
    UserDataHeader  TPUserDataHeader;
    std::string     TPUserData;
}; // class Submit

class PDU {
public:
    PDU(const std::string &hex, bool sending, bool has_smsc);

    const std::string &why() const
    {
        return _why;
    }

    Type type() const
    {
        return _type;
    }

    Result result() const
    {
        return _result;
    }

    const std::string &smsc() const
    {
        return _smsc;
    }

    std::shared_ptr<const Submit> submit() const
    {
        return _submit;
    }

    std::shared_ptr<const Deliver> deliver() const
    {
        return _deliver;
    }

protected:
    void Decode(const std::string &pdu, bool sending, bool has_smsc);

private:
    Type _type;
    Result _result;
    std::string _why;
    std::string _smsc;
    std::shared_ptr<Submit> _submit;
    std::shared_ptr<Deliver> _deliver;

}; // class PDU

} // namespace pdu

#endif // SMS_SERVER_PDU_H
