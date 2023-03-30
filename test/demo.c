#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
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
	fprintf(stderr, "received %s message of len %ld:\n", isText ? "Text" : "Binary", lenth);
	if (isText)
		fprintf(stderr, "TEXT: %s\n", (char*)payload);
	else{
		for (int i = 0; i < lenth; i++)
		{
			fprintf(stderr, " %x ", payload[i]);
			if (i % 16 == 0)
				fprintf(stderr, " \n ");
		}
	}

	return 0;
}

static int filter_data(const struct dirent* det)
{
	return  strncmp(det->d_name, "input",5) == 0;
}

int onopen(wsclient *c) {
	fprintf(stderr, "onopen called: %d\n", c->sockfd);
	
	struct dirent **namelist;
    int n;

   n = scandir(".", &namelist, filter_data, alphasort);
    if (n < 0){
		fprintf(stderr, "no input* files found on current directory.");
		return 0;
	}
	
	while (n--) {
		sleep(1);
		char* fname = namelist[n]->d_name;		
		int isText = fname[strlen(fname)-1] == 's';
		
		char buff[1024*10] = {0};
		FILE* finput = open(fname, 'r');
		size_t flen = read(finput, buff, 1024*10);
		close(finput);
		
		if (isText)
			libwsclient_send_string(c, buff);
		else
			libwsclient_send_data(c, OP_CODE_TYPE_BINARY, (unsigned char*)buff, flen);

		fprintf(stderr, "sending %ld byte %s from: %s\n", flen, isText ? "Text" : "Data",  fname);
		free(namelist[n]);
	}
	free(namelist);

	return 0;
}

int main(int argc, char **argv) {

	char buff[512] = {"wss://nls-gateway.cn-shanghai.aliyuncs.com/ws/v1?token=7b98df51a2c540d88ce84a2c5734fee4"};
	FILE *finput = open("cfg.url", 'r');
	size_t flen = read(finput, buff, 1024 * 10);
	close(finput);

	//Initialize new wsclient * using specified URI
	wsclient *client = libwsclient_new(buff);
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

