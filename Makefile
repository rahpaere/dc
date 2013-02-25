CFLAGS = -Wall -Wextra -g -pthread
LDFLAGS = -pthread

.PHONY: all
all: dc

.PHONY: clean
clean:
	rm -f dc.o log.o dc

dc: dc.o log.o

