#ifndef LOG_H
#define LOG_H

#include <stdlib.h>

struct log;

struct log *log_start(char *prefix, size_t maxbytes, size_t maxcount);
size_t log_write(struct log *log, char *data, size_t size);
void log_stop(struct log *log);

#endif
