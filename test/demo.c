#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "libwsclient.h"

int onclose(wsclient *c) {
	fprintf(stderr, "onclose called: %d\n", c->sockfd);
	return 0;
}

int onerror(wsclient *c, int code, char *msg)
{
	fprintf(stderr, "onerror: (%d): %s\n", code, msg);
	return 0;
}

int onmessage(wsclient *c, bool isText, unsigned long long lenth, unsigned char *payload)
{
	fprintf(stderr, "onmessage: (%llu): %s\n", lenth, payload);
	return 0;
}

int onopen(wsclient *c) {
	fprintf(stderr, "onopen called: %d\n", c->sockfd);
	libwsclient_send_string(c, "Hello onopen");
	return 0;
}

int main(int argc, char **argv) {
	//Initialize new wsclient * using specified URI
	wsclient *client = libwsclient_new("wss://nls-gateway.cn-shanghai.aliyuncs.com/ws/v1?token=7b98df51a2c540d88ce84a2c5734fee4");
	if(!client) {
		fprintf(stderr, "Unable to initialize new WS client.\n");
		exit(1);
	}
	//set callback functions for this client
	client->onopen=onopen;
	client->onmessage= &onmessage;
	client->onerror= &onerror;
	client->onclose= &onclose;

	//starts run thread.
	libwsclient_start_run(client);
	//blocks until run thread for client is done.
	libwsclient_wait_for_end(client);
	return 0;
}

