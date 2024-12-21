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
	MT_HTML,
	MT_CSS,
	MT_Count
} MimeType;

CString MT_format[MT_Count] = { "text/html", "text/css" };


typedef struct {
	CString name; MimeType type;
} StaticFile;

#define STATIC_FILES_PATH "../../Dashboard"
#define STATIC_FILES_COUNT 2
const StaticFile static_files[STATIC_FILES_COUNT] = {
	{ "/index.html", MT_HTML }, { "/styles.css", MT_CSS }
};


typedef enum {
	M_GET = 0, M_HEAD, M_POST, M_PUT, M_DELETE,
	M_OPTIONS, M_TRACE, M_CONNECT, M_Count
} HTTP_Method;

CString HTTP_Method_labels[M_Count] = {
	"GET", "HEAD", "POST", "PUT", "DELETE",
	"OPTIONS", "TRACE", "CONNECT"
};


typedef struct {
	FString key;
	FString value;
} Keyvalue;

typedef struct {
	Keyvalue* items;
	n64 count;
	n64 capacity;
} Keyvalues;

typedef struct {
	MimeType type;
	FString buffer;
} Body;

typedef struct
{
	HTTP_Method method;
	FString uri;
	Keyvalues headers;
	Body body;
} Request;


typedef error (*Endpoint_Action_func)(Request* req);

typedef struct {
	HTTP_Method method;
	CString uri;
	Endpoint_Action_func callback;
} Endpoint;

#define ENDPOINTS_COUNT 1
const Endpoint endpoints[ENDPOINTS_COUNT] = {
	{ M_POST, "/update", NULL }
};


static bool request_match_endpoint(const Request* req, Endpoint endpoint)
{
	if(req->method != endpoint.method) return false;
	return fstring_equals(req->uri, CSTR(endpoint.uri));
}

static void request_free(Request* req)
{
	n64 i;

	fstring_free(&req->uri);

	for(i = 0; i < req->headers.count; ++i)
	{
		fstring_free(&req->headers.items[i].key);
		fstring_free(&req->headers.items[i].value);
	}
	dfree(&req->headers.items);

	fstring_free(&req->body.buffer);
}


/* ------------------------------------------------------------ */

#undef CORE_LOG_MODULE
#define CORE_LOG_MODULE "stream"

#define client_send(SOCK, SIZE, BUFF) client_send_(SOCK, SIZE, BUFF, __FILE__, __LINE__)
static error client_send_(int client_sockfd, n32 buffer_size, void* buffer, char* file, int line)
{
	ssize_t sent_bytes = write(client_sockfd, buffer, buffer_size);

	if(sent_bytes == -1) {
		core_log(CORE_ERROR_, "Cannot write to client socket: %s\n",
				strerror(errno));
		return failure;
	}

	core_log(CORE_DEBUG_, "Sent %d bytes\n", sent_bytes);

	if(sent_bytes < buffer_size) {
		core_log(CORE_ERROR_,
				"Sent less data to client than expected (%d insted of %u): %s\n",
				sent_bytes, buffer_size, strerror(errno));
		return failure;
	}

	return success;
}

#define client_read(SOCK, SIZE, BUFF) client_read_(SOCK, SIZE, BUFF, __FILE__, __LINE__)
static error client_read_(int client_sockfd, n32 buffer_size, void* buffer, char* file, int line)
{
	ssize_t read_bytes = recv(client_sockfd, buffer, buffer_size, 0);

	if(read_bytes == 0) {
		core_log(CORE_DEBUG_, "No message from client\n");
		return success;
	}

	if(read_bytes < 0) {
		core_log(CORE_ERROR_, "Cannot recv data from client: %s\n",
				strerror(errno));
		return failure;
	}

	core_log(CORE_DEBUG_, "Read %d bytes from client\n", read_bytes);
	return success;
}

#undef CORE_LOG_MODULE
#define CORE_LOG_MODULE NULL

/* ------------------------------------------------------------ */


static error client_send_file(int client_sockfd, FString uri, MimeType mimetype)
{
#define BUFF_CAP 1024
	char send_buffer[BUFF_CAP];
	n32 buffer_count = 0;

	long file_size;
	n32 i;

#define FNAME_LEN 32
	FILE* file;
	char file_path[FNAME_LEN] = {0};

	buffer_fmt(FNAME_LEN, file_path, "%s" STR_FMT,
			STATIC_FILES_PATH, FSTR(uri));

	file = fopen(file_path, "r");
	if(file == NULL) {
		core_log(CORE_ERROR, "Cannot open file '%s' to serve: %s\n", file_path,
				strerror(errno));
		return failure;
	}

	/* Set header */
	file_size = file_size_get(file, file_path);
	buffer_count
		= buffer_fmt(BUFF_CAP, send_buffer,
			"HTTP/1.1 200 Ok\r\n"
			"Server: urmom\r\n"
			/* "Last-Modified: \r\n" */
			"Content-Type: %s; charset=UTF-8\r\n"
			"Content-Length: %d\r\n"
			"\r\n", MT_format[mimetype], file_size);

/* #if DEBUG_ENABLE */
/* 	printf("-- SOT --\n%s", send_buffer); */
/* #endif */

	do {
		n32 buff_free_space = BUFF_CAP - buffer_count;

		for(i = buffer_count;
			i < buff_free_space && file_size > 0;
			++i)
		{
			send_buffer[i] = (char)getc(file);
			assert(send_buffer[i] != EOF);

			file_size--;
			buffer_count++;
			/* printc(send_buffer[i]); */
		}

		if(client_send(client_sockfd, buffer_count, send_buffer))
			return failure;

/* #if DEBUG_ENABLE */
/* 		printf(STR_FMT, buffer_count, send_buffer); */
/* #endif */

		buffer_count = 0;
	}
	while(file_size > 0);

	/* printf("\n-- EOT --\n"); */
	fclose(file);

	return success;

#undef BUFF_CAP
#undef FNAME_LEN
}

#define client_send_error_response(CLIENT_SOCKFD, MESSAGE) client_send_error_response_(CLIENT_SOCKFD, MESSAGE, __FILE__, __LINE__)
static error client_send_error_response_(int client_sockfd, CString message,
		char* file, int line)
{
#define BUFF_CAP 128
	char buffer[BUFF_CAP] =
		"HTTP/1.1 500 Server Error\r\n"
		"Content-Length: 16\r\n"
		"\r\n"
		"500 Server Error";

	core_log(CORE_WARN_, "Sending error response: %s\n", message);

	return client_send(client_sockfd, (n32)strlen(buffer), buffer);
#undef BUFF_CAP
}

static error client_send_response(int client_sockfd, Request* req)
{
	n8 i;

	/* Check if asking for static resource */
	if(req->method == M_GET)
	{
		if(fstring_equals_CStr(req->uri, "/"))
		{
			error status;

			FString index_file_name = {0};
			fstring_new_from(&index_file_name, strlen("/index.html"), "/index.html");

			status = client_send_file(client_sockfd, index_file_name, MT_HTML);
			fstring_free(&index_file_name);

			return status;
		}

		for(i = 0; i < STATIC_FILES_COUNT; ++i)
		{
			StaticFile sfile = static_files[i];
			if(fstring_equals_CStr(req->uri, sfile.name))
			{
				return client_send_file(client_sockfd, req->uri, sfile.type);
			}
		}
	}

	/* Check if asking for endpoint */
	for(i = 0; i < ENDPOINTS_COUNT; ++i)
	{
		Endpoint endpoint = endpoints[i];
		if(request_match_endpoint(req, endpoint))
		{
			if(endpoint.callback == NULL) {
				core_log(CORE_ERROR,
						"Callback for endpoint '"STR_FMT"' is NULL\n",
						CSTR(endpoint.uri));

				if(client_send_error_response(client_sockfd, "Callback failure"))
					core_log(CORE_WARN, "Could not send error response to client for a NULL callback\n");

				return failure;
			}

			if(endpoint.callback(req))
			{
				if(client_send_error_response(client_sockfd, "Callback failure\n"))
					core_log(CORE_WARN, "Could not send error response to client for Callback failure\n");

				return failure;
			}

			return success;
		}
	}

	/* request not found... fallback */
	return client_send_error_response(client_sockfd, "Resource not found");
}

static error client_read_request(int client_sockfd, Request* req)
{
#define BUFF_CAP 1024
	char buffer[BUFF_CAP] = {0};

	if(client_read(client_sockfd, BUFF_CAP - 1, buffer))
		return failure;

	{
	/* TODO: parse data into request */
		n8 method_index;
		n32 i;

		for(method_index = 0; method_index < M_Count; ++method_index)
		{
			CString method_label = HTTP_Method_labels[method_index];
			const n8 method_label_len = (n8)strlen(method_label);

			if(buffer_equals(method_label_len, (n8*)&buffer[0],
						method_label_len, (const n8*)method_label))
			{
				req->method = method_index;

				/* TODO: Check for weird constrains */

				/* Count URI len, after "GET " to ' ' */
				for(i = (method_label_len + 1); i < BUFF_CAP; ++i)
					if(buffer[i] == ' ') break;

				fstring_new_from(&req->uri, i - (method_label_len + 1), &buffer[method_label_len + 1]);

				core_log(CORE_INFO, "Received "STR_FMT" request for '"STR_FMT"'\n",
						CSTR(HTTP_Method_labels[req->method]), FSTR(req->uri));

				/*
				 * METHOD URI VERSION \r\n
				 * Key: Value \r\n
				 * \r\n
				 * BODY
				 */

				{
				/* skip version */
					while(i < BUFF_CAP && !(buffer[i] == '\r' && buffer[i + i] == '\n'))
						++i;

					i += 2; /* skip \r\n */

				/* check headers */
					while(true)
					{
#define HEADER_BUFFER_LEN 1024
						char header_buffer[HEADER_BUFFER_LEN] = {0};
						n8 header_length = 0;

						FString key = {0};
						FString val = {0};

						Keyvalue keyval = {0};

#error Check here with debugger...

						for(; i - 1 < BUFF_CAP && !(buffer[i] == '\r' && buffer[i + 1] == '\n'); ++i)
						{
							if(buffer[i] == ' ') continue;

							if(key.count == 0 && buffer[i] == ':')
							{
								fstring_new_from(&key, header_length, (char*)&header_buffer);
								header_length = 0;
								continue;
							}

							header_buffer[header_length++] = buffer[i];
						}

						/* Entered body */
						if(key.count == 0) break;

						/* This one could cause problems (header_length - 2) */
						fstring_new_from(&val, header_length - 2, (char*)&header_buffer);
						keyval.key = key;
						keyval.value = val;

						if(fstring_equals_CStr(key, "Content-Length"))
						{
							core_log(CORE_INFO, "Content-Length: '"STR_FMT"'\n", FSTR(val));
						}

						arr_append(&req->headers, keyval);
#undef HEADER_BUFFER_LEN
					}


				/* TODO: finish parsing request headers */

				}

				return success;
			}
		}

		core_log(CORE_ERROR, "What method even is this? (8 char slice '"STR_FMT"')\n", 8, buffer);
		return failure;
	}

	return success;
#undef BUFF_CAP
}

static error accept_connection(int sockfd)
{
	struct sockaddr_in client_addr = {0};
	socklen_t client_addr_len = sizeof(client_addr);
	int client_sockfd;

	/* Establish connection */
	client_sockfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_addr_len);
	if(client_sockfd == -1) {
		core_log(CORE_WARN, "Cannot create socket with client: %s\n",
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

	{
		Request req = {0};

		if(client_read_request(client_sockfd, &req))
			goto defer_accept_connection_failure;

		if(client_send_response(client_sockfd, &req))
			goto defer_accept_connection_failure;

		request_free(&req);
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

static void handle_sigint(int code) {
	putchar('\n');
	core_log(CORE_INFO, "Code interrupted by signal %d\n", code);
	should_stop = true;
}

static void handle_sigpipe(int code) {
	putchar('\n');
	core_log(CORE_ERROR,
			"DUMBASS! you're probably writing to sockfd instead of client_sockfd (%d)\n",
			code);
	should_stop = true;
}


int main(int argc, char** argv)
{
	char* program = shift(&argc, &argv);

	int sockfd;
	/* int REUSEADDR_enable = 1; */

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

	/* assert(!setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, */
	/*        &REUSEADDR_enable, sizeof(REUSEADDR_enable))); */

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

	signal(SIGPIPE, handle_sigpipe);
	signal(SIGINT, handle_sigint);
	while(!should_stop)
	{
		putchar('\n');
		core_log(CORE_INFO, "Waiting for client connection...\n\n", SERVER_PORT);
		if(accept_connection(sockfd) == failure) {
			core_log(CORE_WARN, "Failed transmission with client\n");
		}
	}

	close(sockfd);
	core_log(CORE_INFO, "Socket closed, exiting...\n");
	return success;

defer_main_failure:
	close(sockfd);
	core_log(CORE_ERROR, "Closed socket over failure, exiting...\n");
	return failure;
}

