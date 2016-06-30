/*
 * Redirect a port to a (possibly remote) port
 * TODO: Bind to a specific incoming host
 */
#include <ctype.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static	int	client(const char **argv, const struct protoent *protoent, int sockout);

int
main(int argc, const char **argv)
{
	const struct protoent *protoent;
	in_port_t port;
	in_addr_t addr;

	if(argc != 4) {
		fprintf(stderr, "Usage: %s inport outIP outport\n", argv[0]);
		return 1;
	}

	protoent = getprotobyname("tcp");

	if(protoent == NULL) {
		fputs("Unknown protocol 'tcp'\n", stderr);
		return 1;
	}

	if(!isdigit(argv[3][0])) {
		const struct servent *servent = getservbyname(argv[3], "tcp");

		if(servent == (const struct servent *)NULL) {
			fprintf(stderr, "Unknown TCP service '%s'\n", argv[3]);
			return 1;
		}

		port = servent->s_port;
	} else
		port = htons(atoi(argv[3]));

	addr = inet_addr(argv[2]);
	if(addr == (in_addr_t)-1) {
		const struct hostent *hostent = gethostbyname(argv[2]);

		if(hostent == NULL) {
			fprintf(stderr, "IP address/hostname '%s' is invalid\n", argv[2]);
			return 1;
		}
		/*addr = inet_addr(hostent->h_addr);

		if(addr == (in_addr_t)-1) {
			fprintf(stderr, "IP address '%s' for hostname '%s' is invalid\n", hostent->h_addr, argv[2]);
			return 1;
		}*/
		memcpy(&addr, hostent->h_addr, sizeof(in_addr_t));
	}

	for(;;) {
		/* Connect to outgoing */
		int sockout = socket(AF_INET, SOCK_STREAM, protoent->p_proto);
		int ret;
#ifdef	SO_REUSEADDR
		const int on = 1;
#endif
		struct sockaddr_in sin;

		if(sockout < 0) {
			perror("socket");
			return errno;
		}

#ifdef	SO_REUSEADDR
		if(setsockopt(sockout, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
			perror("setsockopt");	/* warning */
#endif

		memset(&sin, '\0', sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = port;
		sin.sin_addr.s_addr = addr;
		if(connect(sockout, (struct sockaddr *)&sin, sizeof(struct sockaddr_in)) < 0) {
			perror("connect");
			return errno;
		}
		printf("Connected to %s\n", argv[2]);

		ret = client(argv, protoent, sockout);

		close(sockout);

		if(ret != 0)
			return ret;
	}
}

static int
client(const char **argv, const struct protoent *protoent, int sockout)
{
	int s, sockin;
	struct sockaddr_in sin;
	socklen_t len = sizeof(struct sockaddr_in);
#ifdef	SO_REUSEADDR
	const int on = 1;
#endif

	/* Listen for incoming */
	s = socket(PF_INET, SOCK_STREAM, protoent->p_proto);

	if(s < 0) {
		perror("socket");
		return errno;
	}

#ifdef	SO_REUSEADDR
	if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		perror("setsockopt");	/* warning */
#endif
	memset(&sin, '\0', sizeof(sin));

	sin.sin_family = AF_INET;
	sin.sin_port = htons(atoi(argv[1]));

	if(bind(s, (struct sockaddr *)&sin, sizeof(struct sockaddr)) < 0) {
		perror("bind");
		return errno;
	}

	if(listen(s, 1) < 0) {
		perror("listen");
		return errno;
	}

	sockin = accept(s, (struct sockaddr *)&sin, &len);

	close(s);

	if(sockin < 0) {
		perror("accept");
		return errno;
	}

	printf("Connection from %s\n", inet_ntoa(sin.sin_addr));

	for(;;) {
		fd_set rfds;
		char buf[BUFSIZ];
		int nbytes;

		FD_ZERO(&rfds);
		FD_SET(sockin, &rfds);
		FD_SET(sockout, &rfds);

		if(select(sockin + 1, &rfds, NULL, NULL, NULL) < 0) {
			perror("select");
			close(sockin);
			return errno;
		}

		if(FD_ISSET(sockin, &rfds)) {
			nbytes = recv(sockin, buf, sizeof(buf), 0);
			if(nbytes < 0) {
				perror("recv");
				close(sockin);
				return 0;
			} else if(nbytes == 0)
				break;

			/*printf("Client sends %d bytes\n", nbytes);*/
			if(send(sockout, buf, nbytes, 0) < 0) {
				perror("send");
				close(sockin);
				return errno;
			}
		} else if(FD_ISSET(sockout, &rfds)) {
			nbytes = recv(sockout, buf, sizeof(buf), 0);
			if(nbytes < 0) {
				perror("recv");
				close(sockin);
				return errno;
			}
			/*printf("Server sends %d bytes\n", nbytes);*/
			if(send(sockin, buf, nbytes, 0) < 0) {
				perror("send");
				close(sockin);
				return errno;
			}
		} else {
			fputs("select is broken\n", stderr);
			close(sockin);
			return 1;
		}
	}
	return close(sockin);
}
