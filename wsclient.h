#ifndef WSCLIENT_H_
#define WSCLIENT_H_
/*
#include <stdint.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <stddef.h>
#include <stdbool.h>
*/
#define MAX_PAYLOAD_SIZE 1024

ssize_t _libwsclient_read(wsclient *c, void *buf, size_t length);
ssize_t _libwsclient_write(wsclient *c, const void *buf, size_t length);
int libwsclient_open_connection(const char *host, const char *port);
int stricmp(const char *s1, const char *s2);
void libwsclient_handle_control_frame(wsclient *c, wsclient_frame_in *ctl_frame);
void *libwsclient_run_thread(void *ptr);
void *libwsclient_handshake_thread(void *ptr);
void handle_on_data_frame_in(wsclient *c, wsclient_frame_in *pframe);
void libwsclient_send_data(wsclient *client, int opcode, unsigned char *payload, unsigned long long payload_len);
void libwsclient_send_string(wsclient *client, char *payload);
void update_wsclient_status(wsclient *c, int add, int del);

#endif /* WSCLIENT_H_ */
