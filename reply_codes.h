#ifndef REPLY_CODES_H
#define REPLY_CODES_H

#define REPLY_CODE_220                "220 %s Service ready - %s\r\n"
#define REPLY_CODE_221                "221 2.0.0 %s closing connection\r\n"

#define REPLY_CODE_250_2_0_0          "250 2.0.0 OK\r\n"
#define SENDER_OK                     "250 2.1.0 Sender ok\r\n"
#define RECIPIENT_OK                  "250 2.1.5 Recipient ok\r\n"
#define RESET_STATE                   "250 2.0.0 Reset state\r\n"
#define MESSAGE_ACCEPTED_FOR_DELIVERY "250 2.0.0 Message accepted for delivery\r\n"

#define EHLO_RESPONSE                 "250-%s\r\n250-8BITMIME\r\n250-SIZE %lu\r\n250 CHUNKING\r\n"
#define HELO_RESPONSE                 "250 %s\r\n"

#define REPLY_CODE_354                "354 Enter mail, end with \".\" on a line by itself\r\n"
#define REPLY_CODE_421                "421 4.7.0 %s closing connection\r\n"

#define REPLY_CODE_450                "450 Requested mail action not taken: mailbox unavailable\r\n"
#define TOO_MANY_TRANSACTIONS         "450 4.7.1 Error: too much mail from %s\r\n"

#define REPLY_CODE_451                "451 4.3.2 Please try again later\r\n"

#define INSUFFICIENT_DISK_SPACE       "452 4.4.5 Insufficient disk space; try again later\r\n"
#define TOO_MANY_RECIPIENTS           "452 4.5.3 Too many recipients\r\n"

#define REPLY_CODE_500                "500 5.5.1 Command unrecognized\r\n"

#define EHLO_REQUIRES_DOMAIN_ADDRESS  "501 5.0.0 ehlo requires domain address\r\n"
#define HELO_REQUIRES_DOMAIN_ADDRESS  "501 5.0.0 helo requires domain address\r\n"
#define INVALID_DOMAIN_NAME           "501 5.0.0 Invalid domain name\r\n"

#define SYNTAX_ERROR_MAIL_FROM        "501 5.5.2 Syntax error in parameters scanning \"from\"\r\n"
#define BAD_SENDER                    "501 5.1.7 Syntax error in mailbox address\r\n"

#define SYNTAX_ERROR_RCPT_TO          "501 5.5.2 Syntax error in parameters scanning \"to\"\r\n"
#define BAD_RECIPIENT                 "501 5.1.3 Syntax error in mailbox address\r\n"

#define RSET_SYNTAX                   "501 5.5.4 Syntax: \"RSET\"\r\n"
#define BDAT_SYNTAX                   "501 Syntax: \"BDAT\" SP chunk-size[SP \"LAST\"]\r\n"
#define DATA_SYNTAX                   "501 5.5.4 Syntax: \"DATA\"\r\n"

#define REPLY_CODE_502                "502 5.5.1 Command not implemented\r\n"

#define NEED_HELO_COMMAND             "503 5.0.0 Polite people say HELO first\r\n"
#define NEED_MAIL_COMMAND             "503 5.0.0 Need MAIL before RCPT\r\n"
#define NEED_RCPT_COMMAND             "503 5.0.0 Need RCPT (recipient)\r\n"
#define MAIL_TRANSACTION_IN_PROGRESS  "503 5.5.1 Error: MAIL transaction in progress\r\n"

#define SENDER_ALREADY_SPECIFIED      "503 5.5.0 Sender already specified\r\n"

#define REPLY_CODE_550                "550 5.1.1 Addressee unknown\r\n"
#define REPLY_CODE_551                "551 5.1.6 User not local; please try %s\r\n"
#define REPLY_CODE_552                "552 5.2.3 Message size exceeds maximum value\r\n"

#endif /* REPLY_CODES_H */
