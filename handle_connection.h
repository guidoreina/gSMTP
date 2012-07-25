#ifndef HANDLE_CONNECTION_H
#define HANDLE_CONNECTION_H

#include <sys/epoll.h>
#include "connection.h"

int handle_connection (connection_t *connection, struct epoll_event *event);

#endif /* HANDLE_CONNECTION_H */
