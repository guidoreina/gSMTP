#ifndef HANDLE_SESSION_H
#define HANDLE_SESSION_H

#include <sys/epoll.h>
#include "session.h"

int handle_session (session_t *session, struct epoll_event *event);

#endif /* HANDLE_SESSION_H */
