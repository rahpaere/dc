#CFLAGS = -Wall -Wextra -g -pthread -DTCPR
CFLAGS = -Wall -Wextra -g -pthread
LDFLAGS = -pthread

.PHONY: all
all: dc pmuplayer pmudumper pmucat

.PHONY: clean
clean:
	rm -f *.o dc pmuplayer pmudumper pmucat

dc: dc.o log.o

pmuplayer: pmuplayer.o c37.o

pmuplayer.o: pmuplayer.c c37.h

pmudumper: pmudumper.o c37.o

pmudumper.o: pmudumper.c c37.h

pmucat: pmucat.c

c37.o: c37.c c37.h
