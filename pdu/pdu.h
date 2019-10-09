#ifndef PDU_H
#define PDU_H

#include <stdint.h>
#include <time.h>

typedef struct pdu {
    uint8_t TPMessageTypeIndicator;
    uint8_t TPMoreMessagesToSend;
    uint8_t TPRejectDuplicates;
    uint8_t TPLoopPrevention;
    uint8_t TPValidityPeriodFormat;
    uint8_t TPStatusReportIndication;
    uint8_t TPStatusReportRequest;
    uint8_t TPStatusReportQualifier;
    uint8_t TPUserDataHeaderIndicator;
    uint8_t TPReplyPath;
    uint8_t TPFailureCause;
    uint8_t TPMessageReference;
    char    TPDestinationAddress[24];
    char    TPOriginatingAddress[24];
    char    TPRecipientAddress[24];
    time_t  TPServiceCentreTimeStamp;
    time_t  TPDischargeTime;
    uint8_t TPStatus;
    uint8_t TPParameterIndicator;
    uint8_t TPProtocolIdentifier;
    uint8_t TPDataCodingScheme;
    time_t  TPValidityPeriod;
    uint8_t TPUserDataLength;
    uint8_t TPCommandType;
    uint8_t TPMessageNumber;
    uint8_t TPCommandDataLength;
    char    TPUserData[512];
    char    TPCommandData[512];
} pdu_t;

typedef struct user_data_header {
} user_data_header_t;

typedef struct sms_deliver {
    uint8_t TPMessageTypeIndicator0     :1;
    uint8_t TPMessageTypeIndicator1     :1;
    uint8_t TPMoreMessagesToSend        :1;
    uint8_t TPStatusReportIndication    :1;
    uint8_t TPUserDataHeaderIndicator   :1;
    uint8_t TPReplyPath                 :1;

    char    TPOriginatingAddress[24];
    uint8_t TPProtocolIdentifier;
    uint8_t TPDataCodingScheme;
    time_t  TPServiceCentreTimeStamp;
    uint8_t TPUserDataHeaderLength;
    char    TPUserDataHeader[128];
    uint8_t TPUserDataLength;
    char    TPUserData[512];
} sms_deliver_t;

typedef struct sms_submit {
    uint8_t TPMessageTypeIndicator0     :1;
    uint8_t TPMessageTypeIndicator1     :1;
    uint8_t TPRejectDuplicates          :1;
    uint8_t TPValidityPeriodFormat3     :1;
    uint8_t TPValidityPeriodFormat4     :1;
    uint8_t TPReplyPath                 :1;
    uint8_t TPUserDataHeaderIndicator   :1;
    uint8_t TPStatusReportRequest       :1;

    uint8_t TPMessageReference;
    char    TPDestinationAddress[24];
    uint8_t TPProtocolIdentifier;
    uint8_t TPDataCodingScheme;
    time_t  TPValidityPeriod;
    uint8_t TPUserDataHeaderLength;
    char    TPUserDataHeader[128];
    uint8_t TPUserDataLength;
    char    TPUserData[512];
} sms_submit_t;

#endif /* PDU_H */
