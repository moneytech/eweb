#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "dwebsvr.h"

void write_html(int socket_fd, char *head, char *html)
{
	char headbuf[255];
	sprintf(headbuf, "%s\nContent-Length: %d\r\n\r\n", head, (int)strlen(html)+1);
	write(socket_fd, headbuf, strlen(headbuf));
	write(socket_fd, html, strlen(html));
	write(socket_fd, "\n", 1);
}

void forbidden_403(int socket_fd, char *info)
{
	write_html(socket_fd, "HTTP/1.1 403 Forbidden\nServer: dweb\nConnection: close\nContent-Type: text/html", 
		"<html><head>\n<title>403 Forbidden</title>\n"
		"</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple webserver.\n</body>"
		"</html>");
	logger(LOG, "403 FORBIDDEN", info, socket_fd);
}

void notfound_404(int socket_fd, char *info)
{
	write_html(socket_fd, "HTTP/1.1 404 Not Found\nServer: dweb\nConnection: close\nContent-Type: text/html",
		"<html><head>\n<title>404 Not Found</title>\n"
		"</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>");

	logger(LOG, "404 NOT FOUND", info, socket_fd);
}

void ok_200(int socket_fd, char *html, char *path)
{
	write_html(socket_fd, "HTTP/1.1 200 OK\nServer: dweb\nConnection: close\nContent-Type: text/html", html);
	
	logger(LOG, "200 OK", path, socket_fd);
}

void logger(int type, char *s1, char *s2, int socket_fd)
{
	switch (type)
	{
		case ERROR:
			printf("ERROR: %s: %s (errno=%d pid=%d socket=%d)\n",s1, s2, errno, getpid(), socket_fd);
			break;
		default:
			printf("INFO: %s: %s (pid=%d socket=%d)\n",s1, s2, getpid(), socket_fd);
			break;
	}
	fflush(stdout);
}

http_verb request_type(char *request)
{
	if (strncmp(request, "GET ", 4)==0 || strncmp(request, "get ", 4)==0)
	{
		return HTTP_GET;
	}
	else if (strncmp(request, "POST ", 4)==0 || strncmp(request, "post ", 4)==0)
	{
		return HTTP_POST;
	}
	else
	{
		return HTTP_NOT_SUPPORTED;
	}
}

// this is a child web server process, we can safely exit on errors
void webhit(int socketfd, int hit, void (*responder_func)(char*, char*, int, http_verb))
{
	int j;
	http_verb type;
	long i, ret;
	static char buffer[BUFSIZE+1];	// static, filled with zeroes
	char *body;

	ret = read(socketfd, buffer, BUFSIZE); // read whole web request
	if (ret == 0 || ret == -1)
	{
		// cannot read request, so we'll stop
		forbidden_403(socketfd, "failed to read http request");
		exit(3);
	}
	if (ret > 0 && ret < BUFSIZE)
	{
		buffer[ret] = 0; // null terminate after chars
	}
	else
	{
		buffer[0] = 0;
	}
	for (i=0; i<ret; i++)
	{
		// replace CF and LF with asterisks
		if(buffer[i] == '\r' || buffer[i] == '\n')
		{
			buffer[i]='*';
		}
	}
	logger(LOG, "request", buffer, hit);
	
	if (type = request_type(buffer), type == HTTP_NOT_SUPPORTED)
	{
		forbidden_403(socketfd, "Only simple GET and POST operations are supported");
		exit(3);
	}
	
	// get a pointer to the request body (or NULL if it's not there)
	body = strstr(buffer, "****") + 4;
	
	// the request will be "GET URL " or "POST URL " followed by other details
	// we will terminate after the second space, to ignore everything else
	for (i = (type==HTTP_GET) ? 4 : 5; i<BUFSIZE; i++)
	{
		if(buffer[i] == ' ')
		{
			buffer[i] = 0; // second space, terminate string here
			break;
		}
	}

	for (j=0; j<i-1; j++)
	{
		// check for parent directory use
		if(buffer[j] == '.' && buffer[j+1] == '.')
		{
			forbidden_403(socketfd, "Parent paths (..) are not supported");
			exit(3);
		}
	}
	
	// call the "responder function" which has been provided to do the rest
	responder_func((type==HTTP_GET) ? &buffer[5] : &buffer[6], body, socketfd, type);
	exit(1);
}

int dwebserver(int port, void (*responder_func)(char*, char*, int, http_verb))
{
	int pid, listenfd, socketfd, hit;
	socklen_t length;
	static struct sockaddr_in cli_addr; // static = initialised to zeros
	static struct sockaddr_in serv_addr; // static = initialised to zeros

	if (port <= 0 || port > 60000)
	{
		logger(ERROR, "Invalid port number (try 1 - 60000)", "", 0);
		exit(3);
	}
	
	logger(LOG, "dweb server starting\nPress CTRL+C to quit", "", 0);
	
	if ((listenfd = socket(AF_INET, SOCK_STREAM,0)) < 0)
	{
		logger(ERROR, "system call", "socket", 0);
		exit(3);
	}
	
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port);
	if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) <0)
	{
		logger(ERROR, "system call", "bind", 0);
		exit(3);
	}
	if (listen(listenfd, 64) <0)
	{
		logger(ERROR, "system call", "listen", 0);
		exit(3);
	}

	for (hit=1; ; hit++)
	{
		length = sizeof(cli_addr);
		if ((socketfd = accept(listenfd, (struct sockaddr*)&cli_addr, &length)) < 0)
		{
			logger(ERROR, "system call", "accept", 0);
			exit(3);
		}
		if ((pid = fork()) < 0)
		{
			logger(ERROR, "system call", "fork", 0);
			exit(3);
		}
		else
		{
			if (pid == 0) 
			{ 	
				// child
				close(listenfd);
				webhit(socketfd, hit, responder_func); // never returns
			}
			else
			{
				// parent
				close(socketfd);
			}
		}
	}
}