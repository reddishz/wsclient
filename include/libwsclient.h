#ifndef LIB_WSCLIENT_H_
#define LIB_WSCLIENT_H_

#include <stddef.h>
#include <stdbool.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#define FRAME_CHUNK_LENGTH 1024
#define HELPER_RECV_BUF_SIZE 1024

#define FLAG_CLIENT_IS_SSL (1 << 0)
#define FLAG_CLIENT_CONNECTING (1 << 1)
#define FLAG_CLIENT_CLOSEING (1 << 2)	//最后一帧（close）以发送，以后不许再发任何数据。
#define FLAG_CLIENT_QUIT (1 << 3)		//主动退出

#define FLAG_REQUEST_HAS_CONNECTION (1 << 0)
#define FLAG_REQUEST_HAS_UPGRADE (1 << 1)
#define FLAG_REQUEST_VALID_STATUS (1 << 2)
#define FLAG_REQUEST_VALID_ACCEPT (1 << 3)


enum _WS_OP_CODE_
{
	OP_CODE_CONTINUE = 0,
	OP_CODE_TYPE_TEXT = 1,
	OP_CODE_TYPE_BINARY = 2,
	OP_CODE_CONTROL_CLOSE = 8,
	OP_CODE_CONTROL_PING = 9,
	OP_CODE_CONTROL_PONG = 10,
};

typedef struct _wsclient_frame_in
{
	unsigned int fin;
	unsigned int opcode;
	unsigned long long payload_len;
	unsigned char *payload;
	struct _wsclient_frame_in *next_frame;
	struct _wsclient_frame_in *prev_frame;
} wsclient_frame_in;


typedef struct _wsclient
{
	pthread_t handshake_thread;
	pthread_t run_thread;
	pthread_mutex_t lock;
	pthread_mutex_t send_lock;
	char *URI;
	int sockfd;
	int flags;
	int (*onopen)(struct _wsclient *);
	int (*onclose)(struct _wsclient *);
	int (*onerror)(struct _wsclient *, int code, char *msg);
	int (*onmessage)(struct _wsclient *, bool isText, unsigned long long lenth, unsigned char *data);
	wsclient_frame_in *current_frame;
	SSL_CTX *ssl_ctx;
	SSL *ssl;
	void *userdata;
} wsclient;

// Function defs

// 创建
wsclient *libwsclient_new(const char *URI);
// 设置参数
/*
void libwsclient_set_onopen(wsclient *client, int (*cb)(wsclient *c));
void libwsclient_set_onmessage(wsclient *client, int (*cb)(wsclient *c, bool isText, unsigned long long lenth, unsigned char *data));
void libwsclient_set_onerror(wsclient *client, int (*cb)(wsclient *c, int level, char *msg)); // level 0 = info; 1 = error; 2=fatal; ...
void libwsclient_set_onclose(wsclient *client, int (*cb)(wsclient *c));
*/
// 启动运行
void libwsclient_start_run(wsclient *c);

//可选等待线程结束。可以用于消息循环作用。
void libwsclient_wait_for_end(wsclient *client);

// 结束并清理
void libwsclient_close(wsclient *c);

// 发送消息
void libwsclient_send_data(wsclient *client, int opcode, unsigned char *payload, unsigned long long payload_len);
void libwsclient_send_string(wsclient *client, char *payload);

// 可选，定时发送ping
void libwsclient_send_ping(wsclient *client, char *payload);

#endif /* LIB_WSCLIENT_H_ */
