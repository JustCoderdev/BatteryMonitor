#include <core.h>

/* `netstat -tulpn` to see whose using your port */

#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SERVER_PORT 25565
#define SERVER_ADDRESS INADDR_ANY

#define ADDR_FMT "%d.%d.%d.%d"
#define ADDR(N) (N & 0x000000FF),       (N & 0x0000FF00) >> 8, \
                (N & 0x00FF0000) >> 16, (N & 0xFF000000) >> 24

static error request_read(int sockfd)
{
#define BUFF_LEN 1024
	char buffer[BUFF_LEN];

	ssize_t read_bytes = 0;
	read_bytes = read(sockfd, buffer, BUFF_LEN);

	assert(read_bytes < BUFF_LEN);

	if(read_bytes == 0) {
		core_log(CORE_DEBUG, "No message from client\n");
		return success;
	}

	if(read_bytes < 0) {
		core_log(CORE_ERROR, "Can't recv data from client: %s\n",
				strerror(errno));
		return failure;
	}

	core_log(CORE_DEBUG, "Reading request:\n");
	printf("-- SOT --\n%s\n-- EOT --\n\n", buffer);

	return success;
}

static error accept_connection(int sockfd)
{
	struct sockaddr_in client_addr = {0};
	socklen_t client_addr_len = sizeof(client_addr);
	int client_sockfd;

	/* Establish connection */
	client_sockfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_addr_len);
	if(client_sockfd == -1) {
		core_log(CORE_WARN, "Can't create socket with client: %s\n",
				strerror(errno));
		goto defer_accept_connection_failure;
	}

	/* Check SERVER_ADDRESS */
	if(sizeof(client_addr) != client_addr_len) {
		core_log(CORE_ERROR,
				"Mismatched size between the client SERVER_ADDRESS and the provided structure: %lu != %lu\n",
				sizeof(client_addr), client_addr_len);
		goto defer_accept_connection_failure;
	}

	core_log(CORE_INFO, "Accepted client '"ADDR_FMT":%d'\n",
			ADDR(client_addr.sin_addr.s_addr),
			ntohs(client_addr.sin_port));

	if(request_read(client_sockfd)) {
		goto defer_accept_connection_failure;
	}

	{
	/* Send response */
#define BUFF_LEN 1024
		char buffer[BUFF_LEN] = "HTTP/1.1 200 Ok\r\nContent-Length: 2\r\n\r\nok";
		ssize_t sent_bytes = 0;

		sent_bytes = write(client_sockfd, buffer, BUFF_LEN);
		if(sent_bytes < 0) {
			core_log(CORE_ERROR, "Can't send data to client: %s\n",
					strerror(errno));
			goto defer_accept_connection_failure;
		}

		core_log(CORE_DEBUG, "Sending response:\n");
		printf("-- SOT --\n%s\n-- EOT --\n", buffer);
	}

	close(client_sockfd);
	core_log(CORE_INFO, "Closed socket with client '"ADDR_FMT":%d'\n",
			ADDR(client_addr.sin_addr.s_addr),
			ntohs(client_addr.sin_port));

	return success;

defer_accept_connection_failure:
	close(client_sockfd);
	core_log(CORE_WARN, "Closed socket with client over a failure\n");
	return failure;
}

bool should_stop = false;

static void handle_interrupt(int code) {
	putchar('\n');
	core_log(CORE_INFO, "Code interrupted by signal %d\n", code);
	should_stop = true;
}

int main(int argc, char** argv)
{
	char* program = shift(&argc, &argv);

	int sockfd;
	/* int SO_REUSE_ADDR_enable = true; */
	struct in_addr addr = { SERVER_ADDRESS };
	struct sockaddr_in sock_addr = {0};
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(SERVER_PORT);
	sock_addr.sin_addr = addr;

	printf("Executed as: %s\n", program);

	/* Create socket */
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd == -1) {
		core_log(CORE_ERROR, "Can't create socket: %s\n", strerror(errno));
		goto defer_main_failure;
	}

	/* assert(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &SO_REUSE_ADDR_enable, sizeof(SO_REUSE_ADDR_enable)) != -1); */

	/* Bind socket to port and SERVER_ADDRESS */
	if(bind(sockfd, (struct sockaddr*)&sock_addr, sizeof(sock_addr)) == -1) {
		core_log(CORE_ERROR, "Can't create bind socket to port %d: %s\n",
				SERVER_PORT, strerror(errno));
		goto defer_main_failure;
	}

	core_log(CORE_INFO, "Listening for requests to '"ADDR_FMT":%d'\n", ADDR(SERVER_ADDRESS), SERVER_PORT);
	if(listen(sockfd, 0) == -1) {
		core_log(CORE_ERROR, "Socket refuses to listen on port %d: %s\n",
				SERVER_PORT, strerror(errno));
		goto defer_main_failure;
	}

	signal(SIGINT, handle_interrupt);
	while(!should_stop)
	{
		core_log(CORE_INFO, "Waiting for client connection...\n", SERVER_PORT);
		if(accept_connection(sockfd) == failure) {
			core_log(CORE_WARN, "Failed transmission with client\n");
		}
	}

	close(sockfd);
	core_log(CORE_INFO, "Socket closed, exiting...\n");
	return success;

defer_main_failure:
	close(sockfd);
	core_log(CORE_ERROR, "Closing socket over failure, exiting...\n");
	return failure;
}
