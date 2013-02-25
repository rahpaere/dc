#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct log {
	char *prefix;
	size_t maxbytes;
	size_t maxcount;
	size_t bytes;
	size_t count;
	int fd;
};

static int log_next(struct log *log)
{
	char *filename;
	int olderr;

	if (log->fd > 0) {
		close(log->fd);
		log->fd = -1;
	}

	if (log->maxbytes > 0) {
		char *s;
		int guess;
		int length;
		int index;

		index = log->count;
		if (log->maxcount > 0)
			index %= log->maxcount;

		guess = strlen(log->prefix) + 10;
		s = malloc(guess);
		if (!s)
			return -1;

		length = snprintf(s, guess, "%s%d", log->prefix, index);
		if (length >= guess) {
			filename = realloc(s, length + 1);
			if (!filename) {
				free(s);
				return -1;
			}
			sprintf(filename, "%s%d", log->prefix, index);
		} else {
			filename = s;
		}
	} else {
		filename = strdup(log->prefix);
		if (!filename)
			return -1;
	}

	unlink(filename);
	log->fd = creat(filename, 0600);
	if (log->fd < 0) {
		olderr = errno;
		free(filename);
		errno = olderr;
		return -1;
	}

	free(filename);
	log->count++;
	log->bytes = 0;
	return 0;
}

struct log *log_start(char *prefix, size_t maxbytes, size_t maxcount)
{
	struct log *log;

	log = malloc(sizeof(*log));
	if (!log)
		return NULL;

	log->prefix = strdup(prefix);
	log->maxbytes = maxbytes;
	log->maxcount = maxcount;
	log->bytes = 0;
	log->count = 0;
	log->fd = -1;

	log_next(log);
	return log;
}

size_t log_write(struct log *log, char *data, size_t size)
{
	size_t n;
	size_t total = 0;
	ssize_t bytes;

	while (total < size) {
		n = size - total;
		if (log->maxbytes > 0 && log->bytes + n > log->maxbytes)
			n = log->maxbytes - log->bytes;

		bytes = write(log->fd, &data[total], n);
		if (bytes <= 0)
			break;

		total += bytes;
		log->bytes += bytes;
		if (log->maxbytes > 0 && log->bytes >= log->maxbytes) {
			if (log_next(log) < 0)
				break;
		}
	}

	return total;
}

void log_stop(struct log *log)
{
	if (log->fd > 0)
		close(log->fd);
	free(log->prefix);
	free(log);
}
