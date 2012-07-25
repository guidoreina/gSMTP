CC      = gcc
PROGRAM = SmtpServer
CFLAGS  = -Wall -D_GNU_SOURCE -pedantic
#CFLAGS  += -g #-DDEBUG
CFLAGS  += -O3
LIBS    = -lresolv

all: ${PROGRAM}

${PROGRAM}: main.o server.o handle_connection.o connection.o buffer.o configuration.o domainlist.o input_stream.o stream_copy.o parser.o mail_transaction.o delivery.o switch_to_user.o log.o dns.o dnscache.o session.o handle_session.o relay.o stringlist.o ip_list.o
	${CC} -o $@ $^ ${CFLAGS} ${LIBS}

main.o: main.c server.h configuration.h parser.h
	${CC} -c main.c ${CFLAGS}

server.o: connection.h domainlist.h handle_connection.h delivery.h switch_to_user.h configuration.h ip_list.h server.h server.c
	${CC} -c server.c ${CFLAGS}

handle_connection.o: connection.h server.h reply_codes.h stream_copy.h version.h log.h relay.h ip_list.h handle_connection.h handle_connection.c
	${CC} -c handle_connection.c ${CFLAGS}

connection.o: input_stream.h mail_transaction.h buffer.h server.h connection.h connection.c
	${CC} -c connection.c ${CFLAGS}

buffer.o: buffer.h buffer.c
	${CC} -c buffer.c ${CFLAGS}

configuration.o: configuration.h configuration.c
	${CC} -c configuration.c ${CFLAGS}

domainlist.o: buffer.h parser.h domainlist.h domainlist.c
	${CC} -c domainlist.c ${CFLAGS}

input_stream.o: input_stream.h input_stream.c
	${CC} -c input_stream.c ${CFLAGS}

stream_copy.o: stream_copy.h stream_copy.c
	${CC} -c stream_copy.c ${CFLAGS}

parser.o: constants.h parser.h parser.c
	${CC} -c parser.c ${CFLAGS}

mail_transaction.o: domainlist.h constants.h mail_transaction.h mail_transaction.c
	${CC} -c mail_transaction.c ${CFLAGS}

delivery.o: server.h configuration.h parser.h switch_to_user.h relay.h delivery.h delivery.c
	${CC} -c delivery.c ${CFLAGS}

switch_to_user.o: switch_to_user.h switch_to_user.c
	${CC} -c switch_to_user.c ${CFLAGS}

log.o: connection.h server.h log.h log.c
	${CC} -c log.c ${CFLAGS}

dns.o: buffer.h parser.h dns.h dns.c
	${CC} -c dns.c ${CFLAGS}

dnscache.o: buffer.h dns.h dnscache.h dnscache.c
	${CC} -c dnscache.c ${CFLAGS}

session.o: buffer.h stringlist.h constants.h session.h session.c
	${CC} -c session.c ${CFLAGS}

handle_session.o: session.h server.h relay.h handle_session.h handle_session.c
	${CC} -c handle_session.c ${CFLAGS}

relay.o: session.h input_stream.h server.h configuration.h dnscache.h handle_session.h parser.h relay.h relay.c
	${CC} -c relay.c ${CFLAGS}

stringlist.o: stringlist.h stringlist.c
	${CC} -c stringlist.c ${CFLAGS}

ip_list.o: configuration.h ip_list.h ip_list.c
	${CC} -c ip_list.c ${CFLAGS}

clean:
	rm -f *.o ${PROGRAM}
