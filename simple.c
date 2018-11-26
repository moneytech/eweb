#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eweb.h"

static void simple_response(struct hitArgs *args, char *path, char *request_body, http_verb type) {
	UNUSED(request_body);
	UNUSED(type);
	ok_200(args, "\nContent-Type: text/html",
	       "<html><head><title>Test Page</title></head>"
	       "<body><h1>Testing...</h1>This is a test response.</body>"
	       "</html>", path);
}

int main(int argc, char **argv) {
	if (argc != 2 || !strcmp(argv[1], "-?")) {
		printf("hint: simple [port number]\n");
		exit(0);
	}
	dwebserver(atoi(argv[1]), &simple_response, NULL);
	return 0;
}

