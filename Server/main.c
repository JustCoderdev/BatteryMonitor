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

typedef enum {
	M_GET = 0, M_HEAD, M_POST, M_PUT, M_DELETE,
	M_OPTIONS, M_TRACE, M_CONNECT, M_Count
} HTTP_Method;

CString HTTP_Method_labels[M_Count] = {
	"GET", "HEAD", "POST", "PUT", "DELETE",
	"OPTIONS", "TRACE", "CONNECT"
};


/* return -1 on error */
static long file_size_get(FILE* file, CString file_name)
{
	long file_size = -1,
	     file_start_pos = ftell(file);

	if(file_start_pos == -1) {
		core_log(CORE_ERROR, "Cannot save cursor position of file '%s': %s\n",
				file_name, strerror(errno));
		exit(failure);
	}

	if(fseek(file, 0L, SEEK_END) == -1) {
		core_log(CORE_ERROR, "Cannot seek end of file '%s': %s\n",
				file_name, strerror(errno));
		exit(failure);
	}

	file_size = ftell(file);
	if(file_size == -1) {
		core_log(CORE_ERROR, "Cannot get size of file '%s': %s\n",
				file_name, strerror(errno));
		exit(failure);
	}

	if(fseek(file, file_start_pos, SEEK_SET) == -1) {
		core_log(CORE_ERROR, "Cannot reset cursor of file '%s': %s\n",
				file_name, strerror(errno));
		exit(failure);
	}

	return file_size;
}

static error serve_file(int sockfd, CString path)
{
#define BUFF_LEN 1024
	char send_buffer[BUFF_LEN];
	n32 buffer_content_len = 0;
	n32 i;

	long file_size;
	FILE* file = fopen(path, "r");
	if(file == NULL) {
		core_log(CORE_ERROR, "Cannot open file '%s' to serve: %s\n", path,
				strerror(errno));
		return failure;
	}

	/* Set header */
	file_size = file_size_get(file, path);
	buffer_content_len = buffer_fmt(BUFF_LEN, send_buffer,
			"HTTP/1.1 200 Ok\r\n"
			"Server: urmom\r\n"
			/* "Last-Modified: \r\n" */
			"Content-Type: text/html; charset=UTF-8\r\n"
			"Content-Length: %d\r\n"
			"\r\n", file_size);

	printf("-- SOT --\n%s...\n", send_buffer);

	do {
		ssize_t sent_bytes = 0;
		n32 buff_free_space = BUFF_LEN - buffer_content_len;

		for(i = buffer_content_len;
			i < buff_free_space && file_size > 0;
			++i)
		{
			send_buffer[i] = (char)getc(file);
			assert(send_buffer[i] != EOF);

			file_size--;
			buffer_content_len++;
			/* printc(send_buffer[i]); */
		}

		sent_bytes = write(sockfd, send_buffer, buffer_content_len);
		if(sent_bytes == -1) {
			core_log(CORE_ERROR, "Cannot write to socket: %s\n",
					strerror(errno));
			return failure;
		}

		if(sent_bytes < buffer_content_len) {
			core_log(CORE_ERROR,
					"Sent less data than expected (%d insted of %d): %s\n",
					sent_bytes, buffer_content_len, strerror(errno));
			return failure;
		}


		core_log(CORE_DEBUG, "Sent %d bytes\n", sent_bytes);
		buffer_content_len -= (n32)sent_bytes;
	}
	while(file_size > 0);

	printf("\n-- EOT --\n");
	fclose(file);

	return success;
#undef BUFF_LEN
}

static error send_error_response(int sockfd)
{
	ssize_t sent_bytes = 0;

#define BUFF_LEN 128
	char buffer[BUFF_LEN] = "HTTP/1.1 500 Server Error\r\nContent-Length: 16\r\n\r\n500 Server Error";

	sent_bytes = write(sockfd, buffer, BUFF_LEN);
	if(sent_bytes < 0) {
		core_log(CORE_ERROR, "Cannot send data to client: %s\n",
				strerror(errno));
		return failure;
	}

	core_log(CORE_DEBUG, "Sending %d bytes response:\n", sent_bytes);
	printf("-- SOT --\n%s\n-- EOT --\n", buffer);

	return success;
#undef BUFF_LEN
}

static error request_read(int sockfd)
{
#define BUFF_LEN 1024
	char buffer[BUFF_LEN] = {0};
	ssize_t read_bytes = 0;
	ssize_t i;

	read_bytes = recv(sockfd, buffer, BUFF_LEN - 1, 0);
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

	core_log(CORE_DEBUG, "Read %d bytes:\n", read_bytes);

	printf("-- SOT --\n");
	for(i = 0; i < read_bytes; ++i) {
		printc(buffer[i]);
	}
	printf("\n-- EOT --\n\n");
	/* printf("-- SOT --\n%.*s\n-- EOT --\n\n", BUFF_LEN, buffer); */

	return success;
#undef BUFF_LEN
}

static error accept_connection(int sockfd)
{
	struct sockaddr_in client_addr = {0};
	socklen_t client_addr_len = sizeof(client_addr);
	int client_sockfd;
	bool REUSEADDR_enable = true;

	/* Establish connection */
	client_sockfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_addr_len);
	if(client_sockfd == -1) {
		core_log(CORE_WARN, "Cannot create socket with client: %s\n",
				strerror(errno));
		goto defer_accept_connection_failure;
	}

	assert(!setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
	       &REUSEADDR_enable, sizeof(REUSEADDR_enable)));

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

	if(request_read(client_sockfd))
		goto defer_accept_connection_failure;


	{
	/* Serve dashboard */
		CString file_name = "../../Dashboard/index.html";
		if(serve_file(sockfd, file_name))
		{
			send_error_response(client_sockfd);
			goto defer_accept_connection_failure;
		}
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
		core_log(CORE_ERROR, "Cannot create socket: %s\n", strerror(errno));
		goto defer_main_failure;
	}

	/* Bind socket to port and SERVER_ADDRESS */
	if(bind(sockfd, (struct sockaddr*)&sock_addr, sizeof(sock_addr)) == -1) {
		core_log(CORE_ERROR, "Cannot create bind socket to port %d: %s\n",
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
