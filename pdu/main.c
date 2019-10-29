#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>

#include "charset.h"
#include "json.h"
#include "pdu.h"

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define BIT(x, n) (!!((x) & (1 << n)))
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define BIT(x, n) (!!((x) & (128 >> n)))
#else
#error Unsupported endian
#endif

static void HEX(const char *what, const void *b, size_t l)
{
    static const char H[] = "0123456789ABCDEF";
    const unsigned char *p;
    size_t i;

    printf("%s: ", what);
    p = (const unsigned char *)b;
    for (i = 0; i < l; ++i) {
        printf("%c%c", H[p[i] / 16], H[p[i] % 16]);
    }
    printf("\n");
}

static int get_one(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    } else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    } else {
        return -1;
    }
}

static int get(const char *s)
{
    int h;
    int l;

    h = get_one(*s);
    if (h < 0) {
        return -1;
    }

    l = get_one(*(s + 1));
    if (l < 0) {
        return -1;
    }

    return (h << 4) | l;
}

static int decode_numeric(const unsigned char *s, size_t size, char *output, size_t outlen)
{
    size_t i;
    int c;
    int n;

    for (i = 0; i < size; ++i) {
        c = s[i];
        n = c & 0x0F;
        if (n >= 10) {
            return -1;
        }

        if (outlen-- < 2) { /* This char, and trailing zero */
            return -1;
        }

        *output++ = (char)(n + '0');

        n = c >> 4;
        if (n == 0xF) {
            if (i + 1 == size) {
                break;
            } else {
                return -1;
            }
        } else if (n >= 10) {
            return -1;
        }

        if (outlen-- < 2) { /* This char, and trailing zero */
            return -1;
        }

        *output++ = (char)(n + '0');
    }

    *output = '\0';
    return 0;
}

static int decode_alphanumeric(const unsigned char *s, size_t length, char *output, size_t outlen)
{
    uint64_t n;
    size_t max;
    size_t i;
    size_t j;

    if (outlen < length + 1) {
        return -1;
    }

    max = length * 7 / 8 + !!(length * 7 % 8);
    for (i = 0, j = 0, n = 0; i < max; ++i) {
        n >>= 8;
        n |= (uint64_t)s[i] << 48;
        if (i % 7 == 6) {
            output[j++] = (char)(n & 0x7F); n >>= 7;
            output[j++] = (char)(n & 0x7F); n >>= 7;
            output[j++] = (char)(n & 0x7F); n >>= 7;
            output[j++] = (char)(n & 0x7F); n >>= 7;
            output[j++] = (char)(n & 0x7F); n >>= 7;
            output[j++] = (char)(n & 0x7F); n >>= 7;
            output[j++] = (char)(n & 0x7F); n >>= 7;
            output[j++] = (char)(n & 0x7F); n >>= 7;
        }
    }

    if (n) {
        n >>= (7 - i % 7) * 8;
        for (; i % 7 != 1; ++i) {
            output[j++] = (char)(n & 0x7F); n >>= 7;
        }
    }

    output[j++] = '\0';
    return 0;
}

static time_t decode_absolute_timestamp(const unsigned char *s)
{
    struct tm tm;
    time_t ret;
    char b[16];
    int tz;

    if (decode_numeric(s, 7, b, sizeof(b))) {
        return -1;
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
        return -1;
    }

    ret -= timezone; /* UTC */
    ret -= tz * 900;
    return ret;
}

static time_t decode_relative_timestamp(const unsigned char *s)
{
    time_t t;

    t = (time_t)(*s);
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

static int get_address_any(const unsigned char *s, size_t size, char *output, size_t outlen)
{
    int numbering_plan_identification;
    int type_of_address;
    int type_of_number;

    type_of_address = *s;
    if (!(type_of_address & 0x80)) {
        return -1;
    }

    ++s;
    --size;
    type_of_number = (type_of_address >> 4) & 0x07;
    numbering_plan_identification = type_of_address & 0x0F;

    /* International number : ANY */
    /* Subscriber number : ISDN/telephone numbering plan */
    if (type_of_number == 1 ||
        (type_of_number == 4 && numbering_plan_identification == 1)) {

        *output = '+';
        if (decode_numeric(s, size, output + 1, outlen - 1)) {
            return -1;
        }

    /* National number : ANY */
    /* Subscriber number : National numbering plan */
    } else if (type_of_number == 2 ||
               (type_of_number == 4 && numbering_plan_identification == 8)) {

        if (decode_numeric(s, size, output, outlen)) {
            return -1;
        }

    /* Alphanumeric */
    } else if (type_of_number == 5 && numbering_plan_identification == 0) {
        if (decode_alphanumeric(s, size, output, outlen)) {
            return -1;
        }

    } else {
        return -1;
    }

    return 0;
}

static ssize_t get_address(const unsigned char *s, size_t max, char *output, size_t outlen)
{
    size_t length;
    size_t size;

    if (!max) {
        return -1;
    }

    length = (size_t)(*s);
    size = (length + (length % 2)) / 2 + 2;
    if (size > max) {
        return -1;
    }

    if (get_address_any(s + 1, size - 1, output, outlen)) {
        return -1;
    }

    return (ssize_t)size;
}

static ssize_t get_smsc_number(const unsigned char *s, size_t max, char *output, size_t outlen)
{
    size_t size;

    if (!max) {
        return -1;
    }

    size = (size_t)*s;
    if (size == 0) {
        *output = '\0';
        return 1;
    }

    ++s;
    --max;
    if (size > max) {
        return -1;
    }

    if (get_address_any(s, size, output, outlen)) {
        return -1;
    }

    return (ssize_t)(size + 1);
}

static int print_user_data_header(const void *udh, size_t udhlen)
{
    uint8_t InformationElementIdentifier;
    const uint8_t *p;
    char buffer[32];
    uint8_t Length;

    p = (const uint8_t *)udh;
    while (udhlen) {
        if (udhlen < 3) {
            return -1;
        }

        InformationElementIdentifier = *p;
        Length = *(p + 1);
        udhlen -= 2;
        p += 2;

        if (udhlen < Length) {
            return -1;
        }

        sprintf(buffer, "IEI=%u LEN=%u", InformationElementIdentifier, Length);
        HEX(buffer, p, Length);

        switch (InformationElementIdentifier) {
        case 0x00:
            if (Length != 3) {
                return -1;
            }

            printf("Concatenated short messages, 8-bit reference number: "
                   "REF=%u TOTAL=%u SEQ=%u\n", *p, *(p + 1), *(p + 2));
            break;

        case 0x05:
            if (Length != 4) {
                return -1;
            }

            printf("Application port addressing scheme, 16 bit address: "
                   "SRC=%u DST=%u\n",
                   ntohs(*(const uint16_t *)(p + 2)),
                   ntohs(*(const uint16_t *)p));
            break;

        default:
            break;
        }

        udhlen -= Length;
        p += Length;
    }

    return 0;
}

static int print_timestamp(const char *what, time_t t)
{
    char buffer[64];
    struct tm tm;
    int ret;

    if (t < 0) {
        printf("%s: %lds\n", what, -t);
        return 0;
    }

    if (!localtime_r(&t, &tm)) {
        return -1;
    }

    if (timezone) {
        ret = snprintf(buffer, sizeof(buffer),
                "%04d-%02d-%02dT%02d:%02d:%02d%c%04ld",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec,
                timezone < 0 ? '+' : '-',
                (timezone < 0 ? -timezone : timezone) / 36);

    } else {
        ret = snprintf(buffer, sizeof(buffer),
                "%04d-%02d-%02dT%02d:%02d:%02dZ",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
    }

    if (ret < 0 || (size_t)ret >= sizeof(buffer)) {
        return -1;
    }

    printf("%s: [%s]\n", what, buffer);
    return 0;
}

#define DECODE_USER_DATA(s, max, pdu) decode_user_data( \
        (s), (max), \
        (pdu)->TPUserDataHeaderIndicator, \
        (pdu)->TPDataCodingScheme, \
        (pdu)->TPUserDataHeader, \
        sizeof((pdu)->TPUserDataHeader), \
        &(pdu)->TPUserDataHeaderLength, \
        (pdu)->TPUserData, \
        sizeof((pdu)->TPUserData), \
        &(pdu)->TPUserDataLength)

static int decode_user_data(
        const unsigned char *s,
        size_t               max,
        uint8_t              TPUserDataHeaderIndicator,
        uint8_t              TPDataCodingScheme,
        void                *TPUserDataHeader,
        size_t               udhmax,
        uint8_t             *udhlen,
        char                *TPUserData,
        size_t               udmax,
        uint8_t             *udlen)
{
    uint32_t TPUserDataHeaderLength;
    uint32_t TPUserDataLength;
    uint32_t len;
    ssize_t  ret;

    if (!max) {
        return -1;
    }

    TPUserDataLength = *s;
    --max;
    ++s;

    if (TPDataCodingScheme & 0xE0) {
        return -1;
    }

    switch ((TPDataCodingScheme & 0x0C) >> 2) {
    case 0: /* alphabet */
        len = (uint32_t)TPUserDataLength * 7 / 8 + !!((uint32_t)TPUserDataLength * 7 % 8);
        if (len > max) {
            return -1;
        }

        if (decode_alphanumeric(s, TPUserDataLength, TPUserData, udmax)) {
            return -1;
        }

        *udlen = (uint8_t)TPUserDataLength;
        if (TPUserDataHeaderIndicator) {
            TPUserDataHeaderLength = (unsigned int)(*s) + 1;
            if (TPUserDataHeaderLength > max) {
                return -1;
            }

            len = TPUserDataHeaderLength * 8 / 7 + !!(TPUserDataHeaderLength * 8 % 7);
            if (udhmax < len) {
                return -1;
            }

            *udhlen = *s;
            memcpy(TPUserDataHeader, s + 1, *s);
            *udlen = (uint8_t)(TPUserDataLength - len);
            memmove(TPUserData, TPUserData + len, TPUserDataLength - len + 1);
        }

        return 0;

    case 1: /* 8 bit */
        if (TPUserDataLength > max) {
            return -1;
        }

        if (TPUserDataHeaderIndicator) {
            TPUserDataHeaderLength = (unsigned int)(*s);
            if (TPUserDataHeaderLength > max) {
                return -1;
            }

            if (udhmax < TPUserDataHeaderLength) {
                return -1;
            }

            *udhlen = (uint8_t)TPUserDataHeaderLength;
            *udlen = (uint8_t)(TPUserDataLength - 1 - TPUserDataHeaderLength);
            if (udmax < *udlen) {
                return -1;
            }

            memcpy(TPUserDataHeader, s + 1, TPUserDataHeaderLength);
            memcpy(TPUserData,
                   s + TPUserDataHeaderLength + 1,
                   TPUserDataLength - TPUserDataHeaderLength - 1);

        } else {
            *udlen = (uint8_t)TPUserDataLength;
            if (udmax < *udlen) {
                return -1;
            }

            memcpy(TPUserData, s, TPUserDataLength);
        }

        return 0;

    case 2: /* UCS-2 */
        if (TPUserDataLength > max) {
            return -1;
        }

        TPUserDataHeaderLength = 0;
        if (TPUserDataHeaderIndicator) {
            TPUserDataHeaderLength = (unsigned int)(*s) + 1;
            if (TPUserDataHeaderLength >= max) {
                return -1;
            }

            if (udhmax < TPUserDataHeaderLength) {
                return -1;
            }

            *udhlen = (uint8_t)(TPUserDataHeaderLength - 1);
            memcpy(TPUserDataHeader, s + 1, TPUserDataHeaderLength - 1);
        }

        if ((ret = charset(s + TPUserDataHeaderLength,
                           TPUserDataLength - TPUserDataHeaderLength,
                           TPUserData, udmax,
                           "UCS-2BE", "UTF-8")) < 0) {

            return -1;
        }

        *udlen = (uint8_t)ret;
        return 0;

    case 3:  /* Reserved */
    default: /* Unreachable */
        return -1;
    }
}

static int decode_sms_deliver(
        const unsigned char *s,
        size_t max,
        struct sms_deliver *pdu)
{
    ssize_t ret;

    printf("Type: SMS-DELIVER\n");
    memset(pdu, 0, sizeof(*pdu));
    if (!max) {
        return -1;
    }

    pdu->TPMessageTypeIndicator0   = BIT(*s, 0);
    pdu->TPMessageTypeIndicator1   = BIT(*s, 1);
    pdu->TPMoreMessagesToSend      = BIT(*s, 2);
    pdu->TPStatusReportIndication  = BIT(*s, 5);
    pdu->TPUserDataHeaderIndicator = BIT(*s, 6);
    pdu->TPReplyPath               = BIT(*s, 7);

    --max;
    ++s;

    if ((ret = get_address(s, max,
                           pdu->TPOriginatingAddress,
                           sizeof(pdu->TPOriginatingAddress))) < 0) {

        printf("FAILED %d\n", __LINE__);
        return -1;
    }

    max -= (size_t)ret;
    s += ret;

    if (max < 10) {
        printf("FAILED %d\n", __LINE__);
        return -1;
    }

    pdu->TPProtocolIdentifier = *s;
    --max;
    ++s;

    pdu->TPDataCodingScheme = *s;
    --max;
    ++s;

    if ((pdu->TPServiceCentreTimeStamp = decode_absolute_timestamp(s)) < 0) {
        printf("FAILED %d\n", __LINE__);
        return -1;
    }

    max -= 7;
    s += 7;

    pdu->TPUserDataLength = *s;
    if (DECODE_USER_DATA(s, max, pdu)) {
        printf("FAILED %d\n", __LINE__);
        return -1;
    }

    printf("From: [%s]\n", pdu->TPOriginatingAddress);
    printf("TP-PID: %d\n", pdu->TPProtocolIdentifier);
    printf("TP-DCS: %d\n", pdu->TPDataCodingScheme);
    print_timestamp("TP-SCTS", pdu->TPServiceCentreTimeStamp);
    if (pdu->TPUserDataHeaderIndicator) {
        HEX("UDH", pdu->TPUserDataHeader, pdu->TPUserDataHeaderLength);
        print_user_data_header(pdu->TPUserDataHeader, pdu->TPUserDataHeaderLength);
    }
    printf("UDL: [%u]\n", pdu->TPUserDataLength);
    printf("Text: [%.*s]\n", pdu->TPUserDataLength, pdu->TPUserData);
    HEX("UD", pdu->TPUserData, pdu->TPUserDataLength);
    return 0;
}

static int decode_sms_submit(const unsigned char *s, size_t max, struct sms_submit *pdu)
{
    ssize_t ret;

    printf("Type: SMS-SUBMIT\n");
    memset(pdu, 0, sizeof(*pdu));
    if (max < 2) {
        return -1;
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

    pdu->TPMessageReference = *s;
    --max;
    ++s;

    if ((ret = get_address(s, max,
                           pdu->TPDestinationAddress,
                           sizeof(pdu->TPDestinationAddress))) < 0) {

        return -1;
    }

    max -= (size_t)ret;
    s += ret;

    if (max < 4) {
        return -1;
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
        return -1;
    } else if (pdu->TPValidityPeriodFormat4 == 1 && pdu->TPValidityPeriodFormat3 == 1) {
        if ((pdu->TPValidityPeriod = decode_absolute_timestamp(s)) < 0) {
            return -1;
        }

        max -= 7;
        s += 7;
    }

    pdu->TPUserDataLength = *s;
    if (DECODE_USER_DATA(s, max, pdu)) {
        return -1;
    }

    printf("To: [%s]\n", pdu->TPDestinationAddress);
    printf("TP-MR: %d\n", pdu->TPMessageReference);
    printf("TP-PID: %d\n", pdu->TPProtocolIdentifier);
    printf("TP-DCS: %d\n", pdu->TPDataCodingScheme);
    print_timestamp("TP-VP", pdu->TPValidityPeriod);
    if (pdu->TPUserDataHeaderIndicator) {
        HEX("UDH", pdu->TPUserDataHeader, pdu->TPUserDataHeaderLength);
        print_user_data_header(pdu->TPUserDataHeader, pdu->TPUserDataHeaderLength);
    }
    printf("UDL: [%u]\n", pdu->TPUserDataLength);
    printf("Text: [%.*s]\n", pdu->TPUserDataLength, pdu->TPUserData);
    HEX("UD", pdu->TPUserData, pdu->TPUserDataLength);
    return 0;
}

static ssize_t unhex(const char *input, unsigned char *output, size_t outlen)
{
    ssize_t ret;
    size_t max;
    size_t i;

    max = strlen(input);
    if (max % 2) {
        return -1;
    }

    max /= 2;
    if (max > outlen) {
        return -1;
    }

    for (i = 0; i < max; ++i) {
        if ((ret = get(input + i * 2)) < 0) {
            return -1;
        }

        output[i] = (unsigned char)ret;
    }

    return (ssize_t)max;
}

static int process(const char *input, int sending, int has_smsc)
{
    struct sms_deliver deliver;
    unsigned char buffer[1024];
    unsigned char json[1024];
    struct sms_submit submit;
    const char *psmsc;
    unsigned char *s;
    char smsc[32];
    size_t max;

    ssize_t ret;
    uint8_t TPMessageTypeIndicator0;
    uint8_t TPMessageTypeIndicator1;

    if ((ret = unhex(input, buffer, sizeof(buffer))) < 0) {
        return -1;
    }

    max = (size_t)ret;
    s = buffer;

    psmsc = NULL;
    if (has_smsc) {
        if ((ret = get_smsc_number(s, max, smsc, sizeof(smsc))) < 0) {
            return -1;
        }
        printf("SMSC: [%s]\n", smsc);
        psmsc = smsc;

        s += ret;
        max -= (size_t)ret;
    }

    printf("Head: %d\n", (int)(*s));

    TPMessageTypeIndicator0 = BIT(*s, 0);
    TPMessageTypeIndicator1 = BIT(*s, 1);
    printf("TP-MTI: %u %u\n", TPMessageTypeIndicator1, TPMessageTypeIndicator0);

    if (sending) {
        if (TPMessageTypeIndicator1 == 0 && TPMessageTypeIndicator0 == 0) {
            //return decode_sms_deliver_report(s, max, &deliver);
        } else if (TPMessageTypeIndicator1 == 1 && TPMessageTypeIndicator0 == 0) {
            //return decode_sms_command(s, max, &deliver);
        } else if (TPMessageTypeIndicator1 == 0 && TPMessageTypeIndicator0 == 1) {
            if (decode_sms_submit(s, max, &submit)                        ||
                json_encode_sms_submit(psmsc, &submit, json, sizeof(json)) ){

                return -1;
            }
        }
    } else {
        if (TPMessageTypeIndicator1 == 0 && TPMessageTypeIndicator0 == 0) {
            if (decode_sms_deliver(s, max, &deliver)                        ||
                json_encode_sms_deliver(psmsc, &deliver, json, sizeof(json)) ){

                return -1;
            }

        } else if (TPMessageTypeIndicator1 == 1 && TPMessageTypeIndicator0 == 0) {
            //return decode_sms_status_report(s, max, &deliver);
        } else if (TPMessageTypeIndicator1 == 0 && TPMessageTypeIndicator0 == 1) {
            //return decode_sms_submit_report(s, max, &submit);
        }
    }

    printf("JSON: %s\n", json);
    return 0;
}

int main(int argc, char *argv[])
{
    int sending = 0;
    int smsc = 1;

    if (argc < 2) {
        return EXIT_FAILURE;
    } else if (argc >= 3) {
        sending = 1;
    }

    if (process(argv[1], sending, smsc)) {
        printf("ERROR\n");
        return EXIT_FAILURE;
    }

    printf("OK\n");
    return EXIT_SUCCESS;
}
