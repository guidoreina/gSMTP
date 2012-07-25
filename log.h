#ifndef LOG_H
#define LOG_H

#include <time.h>
#include "connection.h"

void log_mail (struct tm *local_time, connection_t *connection);

#endif /* LOG_H */
