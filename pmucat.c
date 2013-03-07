#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>

void do_copy(int input, int output){
	char buf[1024];

	int n;
	while ((n = read(input, buf, sizeof(buf))) > 0) {
		char *p = buf;
		while (n > 0) {
			int m = write(output, buf, n);
			if (m <= 0) {
				perror("do_copy: write");
			}
			p += m;
			n -= m;
		}
	}
	if (n < 0) {
		perror("do_copy: read");
	}
}

int do_connect(char *peer, int port){
	struct hostent *h;

	if ((h = gethostbyname(peer)) == 0) {
		fprintf(stderr, "can't lookup %s\n", peer);
		exit(1);
	}

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_family = h->h_addrtype;
	addr.sin_addr = * (struct in_addr *) *h->h_addr_list;
	addr.sin_port = htons(port);

	/* Create and connect the socket.
	 */
	int s;
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		exit(1);
	}
	if (connect(s, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror(peer);
		exit(1);
	}

	return s;
}

/* If called without arguments, listen for connections.  Otherwise make a
 * connection to the specified first argument.
 */
int main(int argc, char *argv[]){
	int inport = atoi(argv[1]);
	int outport = atoi(argv[2]);

	int input = do_connect("localhost", inport);
	int output = do_connect("localhost", outport);

	(void)argc;

	do_copy(input, output);
	return 0;
}
