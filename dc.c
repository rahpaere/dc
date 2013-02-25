#include "log.h"

#include <tcpr/types.h>

#include <ctype.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static const uint16_t masterport = 6666;

struct arguments {
	char *name;
	char *logprefix;
	int logbytes;
	int logcount;
	char *pullhost;
	char *pullport;
	char *pushhost;
	char *pushport;
	char *id;
};

static void usage(struct arguments *args)
{
	fprintf(stderr, "Usage: %s [args] "
		"src-ip src-port stream-id dst-ip dst-port\n", args->name);
	fprintf(stderr, "Optional arguments:\n");
	fprintf(stderr, "	-l log-file:  "
		"prefix of log file name [default = no logging]\n");
	fprintf(stderr, "	-s log-size:  "
		"maximum size of a log file [default = unlimited]\n");
	fprintf(stderr, "	-n log-count: "
		"maximum #log files [default = unlimited]\n");
	exit(1);
}

static void parse_arguments(struct arguments *args, int argc, char **argv)
{
	int c;

	args->name = argv[0];
	while ((c = getopt(argc, argv, "l:n:s:")) != -1)
		switch (c) {
		case 'l':
			args->logprefix = optarg;
			break;
		case 's':
			args->logbytes = atoi(optarg);
			if (args->logbytes <= 0)
				usage(args);
			break;
		case 'n':
			args->logcount = atoi(optarg);
			if (args->logcount <= 0)
				usage(args);
			break;
		default:
			usage(args);
		}

	if (argc - optind != 5)
		usage(args);

	args->pullhost = argv[optind++];
	args->pullport = argv[optind++];
	args->id = argv[optind++];
	args->pushhost = argv[optind++];
	args->pushport = argv[optind++];
}

static int resolve_address(struct sockaddr_in *addr, const char *host,
			   const char *port)
{
	struct addrinfo hints;
	struct addrinfo *ai;
	int err;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	err = getaddrinfo(host, port, &hints, &ai);
	if (err)
		return err;

	memcpy(addr, ai->ai_addr, ai->ai_addrlen);
	freeaddrinfo(ai);
	return 0;
}

static void *do_handle_slaves(void *arg)
{
	int sock;
	int slavesock;
	int yes = 1;
	struct sockaddr_in self;
	char buffer[1];

	(void)arg;

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	self.sin_family = AF_INET;
	self.sin_port = htons(masterport);
	self.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sock, (struct sockaddr *)&self, sizeof(self)) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}
	if (listen(sock, 1) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	for (;;) {
		slavesock = accept(sock, NULL, NULL);
		if (slavesock < 0) {
			perror("accept");
			continue;
		}

		while (recv(slavesock, buffer, sizeof(buffer), 0) > 0) ;

		close(slavesock);
	}

	close(sock);
	return NULL;
}

static void handle_slaves(void)
{
	pthread_t t;

	if (pthread_create(&t, NULL, do_handle_slaves, NULL)) {
		perror("Creating thread to handle slaves");
		exit(EXIT_FAILURE);
	}

	pthread_detach(t);
}

static int connect_to_tcpr(struct sockaddr_in *tcpraddr)
{
	int s;

	s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s < 0)
		return -1;

	if (connect(s, (struct sockaddr *)tcpraddr, sizeof(*tcpraddr)) < 0) {
		close(s);
		return -1;
	}

	return s;
}

static int get_tcpr_state(struct tcpr_ip4 *state, int tcprsock,
			  struct sockaddr_in *peeraddr, uint16_t bindport)
{
	memset(state, 0, sizeof(*state));
	state->peer_address = peeraddr->sin_addr.s_addr;
	state->tcpr.hard.peer.port = peeraddr->sin_port;
	state->tcpr.hard.port = htons(bindport);

	if (send(tcprsock, state, sizeof(*state), 0) < 0)
		return -1;
	if (recv(tcprsock, state, sizeof(*state), 0) < 0)
		return -1;

	return 0;
}

static int claim_tcpr_state(struct tcpr_ip4 *state, int tcprsock,
			    struct sockaddr_in *peeraddr, uint16_t bindport)
{
	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);

	if (getsockname(tcprsock, (struct sockaddr *)&addr, &len) < 0)
		return -1;

	if (get_tcpr_state(state, tcprsock, peeraddr, bindport) < 0)
		return -1;

	state->address = addr.sin_addr.s_addr;
	if (send(tcprsock, state, sizeof(*state), 0) < 0)
		return -1;

	return 0;
}

static int connect_to_peer(struct sockaddr_in *peeraddr, uint16_t bindport)
{
	int s;
	int yes = 1;
	struct sockaddr_in self;

	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s < 0)
		return -1;

	if (bindport) {
		self.sin_family = AF_INET;
		self.sin_addr.s_addr = htonl(INADDR_ANY);
		self.sin_port = htons(bindport);

		setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

		if (bind(s, (struct sockaddr *)&self, sizeof(self)) < 0) {
			close(s);
			return -1;
		}
	}

	if (connect(s, (struct sockaddr *)peeraddr, sizeof(*peeraddr)) < 0) {
		close(s);
		return -1;
	}

	return s;
}

static int wait_for_master(struct tcpr_ip4 *state)
{
	int s;
	char buffer[1];
	struct sockaddr_in masteraddr;

	if (!state->address)
		return 0;

	masteraddr.sin_family = AF_INET;
	masteraddr.sin_addr.s_addr = state->address;
	masteraddr.sin_port = htons(masterport);

	s = connect_to_peer(&masteraddr, 0);
	if (s < 0)
		return 1;

	while (recv(s, buffer, sizeof(buffer), 0) > 0) ;

	close(s);
	return 1;
}

static int copy_data(struct tcpr_ip4 *state, struct log *log, int pullsock,
		     int pushsock, int tcprsock)
{
	char buffer[65536];
	ssize_t nr;
	ssize_t ns;
	size_t n;

	for (;;) {
		nr = recv(pullsock, buffer, sizeof(buffer), 0);
		if (nr < 0)
			return -1;
		else if (nr == 0)
			break;

		if (log) {
			if (log_write(log, buffer, nr) < (size_t)nr)
				return -1;
		}

		for (n = 0; n < (size_t)nr; n += ns) {
			ns = send(pushsock, &buffer[n], nr - n, 0);
			if (ns < 0)
				return -1;

			state->tcpr.hard.ack =
			    htonl(ntohl(state->tcpr.hard.ack) + ns);
			if (send(tcprsock, state, sizeof(*state), 0) < 0)
				return -1;
		}
	}

	state->tcpr.hard.done_reading = 1;
	state->tcpr.hard.done_writing = 1;
	if (send(tcprsock, state, sizeof(*state), 0) < 0)
		return -1;

	return 0;
}

int main(int argc, char **argv)
{
	static const uint16_t selfport = 6667;
	struct sockaddr_in pulladdr;
	struct sockaddr_in pushaddr;
	int tcprsock;
	int pullsock;
	int pushsock;
	int err;
	int recovering = 0;
	struct tcpr_ip4 state;
	struct arguments args;
	struct log *log;

	memset(&args, 0, sizeof(args));
	parse_arguments(&args, argc, argv);

	err = resolve_address(&pulladdr, args.pullhost, args.pullport);
	if (err) {
		fprintf(stderr, "%s:%s: %s\n", args.pullhost, args.pullport,
			gai_strerror(err));
		exit(EXIT_FAILURE);
	}

	err = resolve_address(&pushaddr, args.pushhost, args.pushport);
	if (err) {
		fprintf(stderr, "%s:%s: %s\n", args.pushhost, args.pushport,
			gai_strerror(err));
		exit(EXIT_FAILURE);
	}

	printf("Connecting to TCPR.\n");
	tcprsock = connect_to_tcpr(&pulladdr);
	if (tcprsock < 0) {
		perror("Connecting to TCPR");
		exit(EXIT_FAILURE);
	}

	printf("Waiting for existing master, if any.\n");
	if (get_tcpr_state(&state, tcprsock, &pulladdr, selfport) < 0) {
		perror("Getting TCPR state");
		exit(EXIT_FAILURE);
	}

	recovering = wait_for_master(&state);
	if (recovering) {
		printf("Recovering from failed master.\n");
		if (claim_tcpr_state(&state, tcprsock, &pulladdr, selfport) < 0) {
			perror("Claiming TCPR state");
			exit(EXIT_FAILURE);
		}
	} else {
		printf("Creating fresh connection.\n");
	}

	handle_slaves();

	printf("Connecting to data source.\n");
	pullsock = connect_to_peer(&pulladdr, selfport);
	if (pullsock < 0) {
		perror("Connecting to data source");
		exit(EXIT_FAILURE);
	}

	printf("Connecting to data sink.\n");
	pushsock = connect_to_peer(&pushaddr, 0);
	if (pushsock < 0) {
		perror("Connecting to data sink");
		exit(EXIT_FAILURE);
	}

	if (args.logprefix) {
		printf("Opening log.\n");
		log = log_start(args.logprefix, args.logbytes, args.logcount);
	}

	if (get_tcpr_state(&state, tcprsock, &pulladdr, selfport) < 0) {
		perror("Getting TCPR state");
		exit(EXIT_FAILURE);
	}

	if (!recovering) {
		printf("Sending ID to data source.\n");
		if (send(pullsock, args.id, strlen(args.id), 0) < 0) {
			perror("Sending session ID");
			exit(EXIT_FAILURE);
		}
	}

	printf("Copying data from source to sink.\n");
	if (copy_data(&state, log, pullsock, pushsock, tcprsock) < 0) {
		perror("Copying data");
		exit(EXIT_FAILURE);
	}

	printf("Done.\n");
	close(tcprsock);
	close(pullsock);
	close(pushsock);
	return EXIT_SUCCESS;
}
