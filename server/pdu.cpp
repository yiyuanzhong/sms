#include "sms/server/pdu.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sstream>

#include <arpa/inet.h>

#include <flinter/charset.h>
#include <flinter/encode.h>

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define BIT(x, n) (!!((x) & (1 << n)))
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define BIT(x, n) (!!((x) & (128 >> n)))
#else
#error Unsupported endian
#endif

#define LEN_7to8(x) ((x) * 7 / 8 + !!((x) * 7 % 8))
#define LEN_8to7(x) ((x) * 8 / 7 + !!((x) * 8 % 7))

namespace pdu {

class Exception : public std::runtime_error {
public:
    explicit Exception(Result result) : runtime_error(std::string())
                                      , _result(result) {}

    Exception(Result result, const std::string &what)
            : runtime_error(what), _result(result) {}

    const Result &result() const
    {
        return _result;
    }

private:
    const Result _result;

}; // class Exception

static void decode_numeric(
        const unsigned char *s,
        size_t size,
        std::string *output)
{
    size_t i;
    int c;
    int n;

    output->clear();

    for (i = 0; i < size; ++i) {
        c = s[i];
        n = c & 0x0F;
        if (n >= 10) {
            throw Exception(Result::Failed, "bad numeric input");
        }

        output->push_back(static_cast<char>(n + '0'));

        n = c >> 4;
        if (n == 0xF) {
            if (i + 1 == size) {
                break;
            } else {
                throw Exception(Result::Failed, "bad numeric terminator");
            }
        } else if (n >= 10) {
            throw Exception(Result::Failed, "bad numeric input");
        }

        output->push_back(static_cast<char>(n + '0'));
    }
}

static void decode_alphanumeric(
        const unsigned char *s,
        size_t length,
        std::string *output)
{
    uint64_t n;
    size_t max;
    size_t i;

    output->clear();
    max = LEN_7to8(length);
    for (i = 0, n = 0; i < max; ++i) {
        n >>= 8;
        n |= static_cast<uint64_t>(s[i]) << 48;
        if (i % 7 == 6) {
            output->push_back(static_cast<char>(n & 0x7F)); n >>= 7;
            output->push_back(static_cast<char>(n & 0x7F)); n >>= 7;
            output->push_back(static_cast<char>(n & 0x7F)); n >>= 7;
            output->push_back(static_cast<char>(n & 0x7F)); n >>= 7;
            output->push_back(static_cast<char>(n & 0x7F)); n >>= 7;
            output->push_back(static_cast<char>(n & 0x7F)); n >>= 7;
            output->push_back(static_cast<char>(n & 0x7F)); n >>= 7;
            output->push_back(static_cast<char>(n & 0x7F)); n >>= 7;
        }
    }

    if (n) {
        n >>= (7 - i % 7) * 8;
        for (; i % 7 != 1; ++i) {
            output->push_back(static_cast<char>(n & 0x7F)); n >>= 7;
        }
    }
}

static time_t decode_absolute_timestamp(const unsigned char *s)
{
    std::string b;
    struct tm tm;
    time_t ret;
    int tz;

    decode_numeric(s, 7, &b);
    if (b.length() != 14) {
        throw Exception(Result::Failed, "timestamp length is not 14");
    }

    memset(&tm, 0, sizeof(tm));
    tm.tm_year = (b[ 0] - '0') * 10 + (b[ 1] - '0');
    tm.tm_mon  = (b[ 2] - '0') * 10 + (b[ 3] - '0') - 1;
    tm.tm_mday = (b[ 4] - '0') * 10 + (b[ 5] - '0');
    tm.tm_hour = (b[ 6] - '0') * 10 + (b[ 7] - '0');
    tm.tm_min  = (b[ 8] - '0') * 10 + (b[ 9] - '0');
    tm.tm_sec  = (b[10] - '0') * 10 + (b[11] - '0');
    tz         = (b[12] - '0') * 10 + (b[13] - '0');

    if (tm.tm_year <= 37) { /* Y2038 */
        tm.tm_year += 100;
    }

    if (tz & 0x80) {
        tz = -tz & 0x7F;
    }

    ret = mktime(&tm);
    if (ret < 0) {
        throw Exception(Result::Failed, "mktime() failed");
    }

    ret -= timezone; /* UTC */
    ret -= tz * 900;
    return ret;
}

static time_t decode_relative_timestamp(const unsigned char *s)
{
    time_t t;

    t = static_cast<time_t>(*s);
    if (t <= 143) {
        t = (t + 1) * 300;
    } else if (t <= 167) {
        t = (t - 143) * 1800 + 43200;
    } else if (t <= 196) {
        t = (t - 166) * 86400;
    } else {
        t = (t - 192) * 604800;
    }

    return -t;
}

static void get_address_any(const unsigned char *s, size_t size, std::string *output)
{
    int numbering_plan_identification;
    int type_of_address;
    int type_of_number;

    type_of_address = *s;
    if (!(type_of_address & 0x80)) {
        throw Exception(Result::NotImplemented, "unsupported type of address");
    }

    ++s;
    --size;
    type_of_number = (type_of_address >> 4) & 0x07;
    numbering_plan_identification = type_of_address & 0x0F;

    /* International number : ANY */
    /* Subscriber number : ISDN/telephone numbering plan */
    if (type_of_number == 1 ||
        (type_of_number == 4 && numbering_plan_identification == 1)) {

        std::string number;
        decode_numeric(s, size, &number);

        output->assign("+");
        output->append(number);

    /* National number : ANY */
    /* Subscriber number : National numbering plan */
    } else if (type_of_number == 2 ||
               (type_of_number == 4 && numbering_plan_identification == 8)) {

        output->clear();
        decode_numeric(s, size, output);

    /* Alphanumeric */
    } else if (type_of_number == 5 && numbering_plan_identification == 0) {
        output->clear();
        decode_alphanumeric(s, size, output);

    } else {
        throw Exception(Result::NotImplemented, "unsupported number plan");
    }
}

static size_t get_address(const unsigned char *s, size_t max, std::string *output)
{
    size_t length;
    size_t size;

    if (!max) {
        throw Exception(Result::Failed, "bad address length");
    }

    length = static_cast<size_t>(*s);
    size = (length + (length % 2)) / 2 + 2;
    if (size > max) {
        throw Exception(Result::Failed, "bad address length");
    }

    get_address_any(s + 1, size - 1, output);
    return size;
}

static size_t get_smsc_number(const unsigned char *s, size_t max, std::string *output)
{
    size_t size;

    if (!max) {
        throw Exception(Result::Failed, "bad smsc length");
    }

    size = static_cast<size_t>(*s);
    if (size == 0) {
        *output = '\0';
        return 1;
    }

    ++s;
    --max;
    if (size > max) {
        throw Exception(Result::Failed, "bad smsc length");
    }

    get_address_any(s, size, output);
    return size + 1;
}

static void decode_ucs2(const void *input, size_t inlen, std::string *output)
{
    const std::string utf16be(reinterpret_cast<const char *>(input), inlen);
    if (flinter::charset_utf16be_to_utf8(utf16be, output)) {
        throw Exception(Result::Failed, "invalid UTF-16BE");
    }
}

#define DECODE_USER_DATA(s, max, pdu) decode_user_data( \
        (s), (max), \
        (pdu)->TPUserDataHeaderIndicator, \
        (pdu)->TPDataCodingScheme, \
        &(pdu)->TPUserDataHeader, \
        &(pdu)->TPUserData)

static void decode_user_data(
        const unsigned char *s,
        size_t               max,
        uint8_t              TPUserDataHeaderIndicator,
        uint8_t              TPDataCodingScheme,
        UserDataHeader      *TPUserDataHeader,
        std::string         *TPUserData)
{
    uint32_t TPUserDataHeaderLength;
    uint32_t TPUserDataLength;
    uint32_t len;

    if (!max) {
        throw Exception(Result::Failed, "bad user data length");
    }

    TPUserDataLength = *s;
    --max;
    ++s;

    if (TPDataCodingScheme & 0xE0) {
        throw Exception(Result::NotImplemented, "unsupported data coding scheme");
    }

    switch ((TPDataCodingScheme & 0x0C) >> 2) {
    case 0: /* alphabet */
        len = LEN_7to8(TPUserDataLength);
        if (len != max) {
            throw Exception(Result::Failed, "bad alphabet user data length");
        }

        decode_alphanumeric(s, TPUserDataLength, TPUserData);

        if (TPUserDataHeaderIndicator) {
            TPUserDataHeaderLength = *s;
            if (1 + TPUserDataHeaderLength > len) {
                throw Exception(Result::Failed, "bad alphabet user data header length");
            }

            len = LEN_8to7(1 + TPUserDataHeaderLength);
            assert(len <= TPUserDataLength);

            TPUserDataHeader->Decode(s + 1, TPUserDataHeaderLength);
            TPUserData->erase(TPUserData->begin(), TPUserData->begin() + len);
        }

        break;

    case 1: /* 8 bit */
        if (TPUserDataLength != max) {
            throw Exception(Result::Failed, "bad 8 bit user data length");
        }

        if (TPUserDataHeaderIndicator) {
            TPUserDataHeaderLength = *s;
            if (1 + TPUserDataHeaderLength > TPUserDataLength) {
                throw Exception(Result::Failed, "bad 8 bit user data header length");
            }

            TPUserDataHeader->Decode(s, TPUserDataHeaderLength);
            TPUserData->assign(
                    reinterpret_cast<const char *>(s) + 1 + TPUserDataHeaderLength,
                    TPUserDataLength - 1 - TPUserDataHeaderLength);

        } else {
            TPUserData->assign(reinterpret_cast<const char *>(s), TPUserDataLength);
        }

        break;

    case 2: /* UCS-2 */
        if (TPUserDataLength != max) {
            throw Exception(Result::Failed, "bad UCS-2 user data length");
        }

        TPUserDataHeaderLength = 0;
        if (TPUserDataHeaderIndicator) {
            TPUserDataHeaderLength = *s;
            if (1 + TPUserDataHeaderLength >= TPUserDataLength) {
                throw Exception(Result::Failed, "bad UCS-2 user data header length");
            }

            TPUserDataHeader->Decode(s + 1, TPUserDataHeaderLength);
            decode_ucs2(s + 1 + TPUserDataHeaderLength,
                        TPUserDataLength - 1 - TPUserDataHeaderLength,
                        TPUserData);

        } else {
            decode_ucs2(s, TPUserDataLength, TPUserData);
        }

        break;

    case 3:  /* Reserved */
    default: /* Unreachable */
        throw Exception(Result::NotImplemented, "unsupported data coding scheme");
    }
}

static void decode_sms_deliver(const unsigned char *s, size_t max, Deliver *pdu)
{
    size_t ret;

    if (!max) {
        throw Exception(Result::Failed, "bad message length");
    }

    pdu->TPMessageTypeIndicator0   = BIT(*s, 0);
    pdu->TPMessageTypeIndicator1   = BIT(*s, 1);
    pdu->TPMoreMessagesToSend      = BIT(*s, 2);
    pdu->TPStatusReportIndication  = BIT(*s, 5);
    pdu->TPUserDataHeaderIndicator = BIT(*s, 6);
    pdu->TPReplyPath               = BIT(*s, 7);

    --max;
    ++s;

    ret = get_address(s, max, &pdu->TPOriginatingAddress);
    max -= ret;
    s += ret;

    if (max < 10) {
        throw Exception(Result::Failed, "bad message length");
    }

    pdu->TPProtocolIdentifier = *s;
    --max;
    ++s;

    pdu->TPDataCodingScheme = *s;
    --max;
    ++s;

    pdu->TPServiceCentreTimeStamp = decode_absolute_timestamp(s);
    max -= 7;
    s += 7;

    DECODE_USER_DATA(s, max, pdu);
}

static void decode_sms_submit(const unsigned char *s, size_t max, Submit *pdu)
{
    size_t ret;

    if (!max) {
        throw Exception(Result::Failed, "bad message length");
    }

    pdu->TPMessageTypeIndicator0   = BIT(*s, 0);
    pdu->TPMessageTypeIndicator1   = BIT(*s, 1);
    pdu->TPRejectDuplicates        = BIT(*s, 2);
    pdu->TPValidityPeriodFormat3   = BIT(*s, 3);
    pdu->TPValidityPeriodFormat4   = BIT(*s, 4);
    pdu->TPStatusReportRequest     = BIT(*s, 5);
    pdu->TPUserDataHeaderIndicator = BIT(*s, 6);
    pdu->TPReplyPath               = BIT(*s, 7);
    --max;
    ++s;

    if (!max) {
        throw Exception(Result::Failed, "bad message length");
    }

    pdu->TPMessageReference = *s;
    --max;
    ++s;

    ret = get_address(s, max, &pdu->TPDestinationAddress);
    max -= ret;
    s += ret;

    if (max < 4) {
        throw Exception(Result::Failed, "bad message length");
    }

    pdu->TPProtocolIdentifier = *s;
    --max;
    ++s;

    pdu->TPDataCodingScheme = *s;
    --max;
    ++s;

    pdu->TPValidityPeriod = 0;
    if (pdu->TPValidityPeriodFormat4 == 1 && pdu->TPValidityPeriodFormat3 == 0) {
        pdu->TPValidityPeriod = decode_relative_timestamp(s);
        --max;
        ++s;

    } else if (pdu->TPValidityPeriodFormat4 == 0 && pdu->TPValidityPeriodFormat3 == 1) {
        throw Exception(Result::NotImplemented, "unsupported validity period");

    } else if (pdu->TPValidityPeriodFormat4 == 1 && pdu->TPValidityPeriodFormat3 == 1) {
        pdu->TPValidityPeriod = decode_absolute_timestamp(s);
        max -= 7;
        s += 7;
    }

    DECODE_USER_DATA(s, max, pdu);
}

void UserDataHeader::Decode(const void *buffer, size_t udhlen)
{
    const uint8_t *p = reinterpret_cast<const unsigned char *>(buffer);
    uint8_t InformationElementIdentifier;
    uint8_t Length;

    _m.clear();
    while (udhlen) {
        if (udhlen < 3) {
            throw Exception(Result::Failed, "bad user data header length");
        }

        InformationElementIdentifier = *p;
        Length = *(p + 1);
        udhlen -= 2;
        p += 2;

        if (udhlen < Length) {
            throw Exception(Result::Failed, "bad user data header length");
        }

        uint8_t expected = 0;
        switch (InformationElementIdentifier) {
        case 0: expected = 3; break;
        case 8: expected = 4; break;
        };

        if (expected && expected != Length) {
            throw Exception(Result::Failed, "bad user data header");
        }

        _m.insert(std::make_pair(InformationElementIdentifier,
                std::string(reinterpret_cast<const char *>(p), Length)));

        udhlen -= Length;
        p += Length;
    }
}

std::shared_ptr<const ConcatenatedShortMessages>
UserDataHeader::getConcatenatedShortMessages() const
{
    auto p8 = _m.find(0);
    if (p8 != _m.end()) {
        assert(p8->second.length() == 3);
        auto const p = reinterpret_cast<const uint8_t *>(p8->second.data());
        auto r = std::make_shared<ConcatenatedShortMessages>();
        r->ReferenceNumber = p[0];
        r->Maximum         = p[1];
        r->Sequence        = p[2];
        return r;
    }

    auto p16 = _m.find(8);
    if (p16 != _m.end()) {
        assert(p16->second.length() == 4);
        auto const p = reinterpret_cast<const uint8_t *>(p8->second.data());
        auto r = std::make_shared<ConcatenatedShortMessages>();
        r->ReferenceNumber = ntohs(*reinterpret_cast<const uint16_t *>(p));
        r->Maximum         = p[2];
        r->Sequence        = p[3];
        return r;
    }

    return nullptr;
}

PDU::PDU(const std::string &pdu, bool sending, bool has_smsc)
{
    try {
        Decode(pdu, sending, has_smsc);
        _result = Result::OK;

    } catch (const Exception &e) {
        std::ostringstream s;
        _result = e.result();
        switch (e.result()) {
        case Result::OK:
            s << "internal error: ";
            _result = Result::Failed;
            break;
        case Result::Failed:
            s << "invalid PDU: ";
            break;
        case Result::NotImplemented:
            s << "unsupported PDU: ";
            break;
        }

        s << e.what();
        _why = s.str();
    }
}

void PDU::Decode(const std::string &hex, bool sending, bool has_smsc)
{
    auto s = reinterpret_cast<const unsigned char *>(hex.data());
    auto max = hex.length();

    if (has_smsc) {
        size_t ret;
        ret = get_smsc_number(s, max, &_smsc);
        max -= ret;
        s += ret;
    }

    auto TPMessageTypeIndicator0 = BIT(*s, 0);
    auto TPMessageTypeIndicator1 = BIT(*s, 1);

    if (sending) {
        if (TPMessageTypeIndicator1 == 0 && TPMessageTypeIndicator0 == 0) {
            throw Exception(Result::NotImplemented, "SMS Deliver Report");

        } else if (TPMessageTypeIndicator1 == 1 && TPMessageTypeIndicator0 == 0) {
            throw Exception(Result::NotImplemented, "SMS Command");

        } else if (TPMessageTypeIndicator1 == 0 && TPMessageTypeIndicator0 == 1) {
            auto submit = std::make_shared<Submit>();
            decode_sms_submit(s, max, submit.get());
            _type = Type::Submit;
            _submit = submit;

        } else {
            throw Exception(Result::NotImplemented, "unsupported outgoing message type");
        }

    } else {
        if (TPMessageTypeIndicator1 == 0 && TPMessageTypeIndicator0 == 0) {
            auto deliver = std::make_shared<Deliver>();
            decode_sms_deliver(s, max, deliver.get());
            _type = Type::Deliver;
            _deliver = deliver;

        } else if (TPMessageTypeIndicator1 == 1 && TPMessageTypeIndicator0 == 0) {
            throw Exception(Result::NotImplemented, "SMS Status Report");

        } else if (TPMessageTypeIndicator1 == 0 && TPMessageTypeIndicator0 == 1) {
            throw Exception(Result::NotImplemented, "SMS Submit Report");

        } else {
            throw Exception(Result::NotImplemented, "unsupported incoming message type");
        }
    }
}

} // namespace pdu
