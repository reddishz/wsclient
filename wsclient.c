#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <signal.h>
#include <stdbool.h>

#include <pthread.h>

#include "./include/libwsclient.h"
#include "wsclient.h"

#include "sha1.h"
#include "utils.h"



void *libwsclient_run_thread(void *ptr)
{
	wsclient *c = (wsclient *)ptr;
	size_t n;
	do
	{
		if (TEST_FLAG(c, FLAG_CLIENT_QUIT))
			break;
		unsigned char head[2] = {0};
		n = _libwsclient_read(c, head, 2);
		if (n < 2)
			break;

		// frame header
		bool fin = head[0] & 0x80;
		int op = head[0] & 0x0f;
		bool mask = head[1] & 0x80; // always false as it come from server.
		(void)mask;
		unsigned long long len = head[1] & 0x7f;
		if (len == 126)
		{
			uint16_t ulen = 0;
			n = _libwsclient_read(c, &ulen, 2);
			if (n < 2)
				break;
			len = ntohs(ulen);
		}
		else if (len == 127)
		{
			uint64_t ulen = 0;
			n = _libwsclient_read(c, &ulen, 8);
			if (n < 8)
				break;
			len = ntoh64(ulen);
		}

		// 注，作为client来说，收到的frame来自server，按照 rfc6455 规范，总是没有mask的。此处忽略mask处理。
		wsclient_frame_in *pframe = calloc(sizeof(wsclient_frame_in), 1);
		pframe->fin = fin;
		pframe->opcode = op;
		pframe->payload_len = len;
		pframe->payload = calloc(len, 1);

		n = _libwsclient_read(c, pframe->payload, len);
		if (n < len){
			char buff[128] = {0};
			sprintf(buff, "wsclient try to read %lld bytes, but get %ld bytes.", len, n);
			LIBWSCLIENT_ON_ERROR(c, buff);
			break;
		}

		handle_on_data_frame_in(c, pframe);

	} while (n > 0);

	if (!TEST_FLAG(c, FLAG_CLIENT_QUIT))
	{	//不是主动退出的。
		LIBWSCLIENT_ON_ERROR(c, "Error receiving data in client run thread");
	}

	if (c->onclose)
	{
		c->onclose(c);
	}
	close(c->sockfd);
	return NULL;
}


void libwsclient_handle_control_frame(wsclient *c, wsclient_frame_in *ctl_frame)
{
	// rfc6455: 控制帧payload必须在125内，且不能分片。
	// char mask[4];
	// int mask_int;
	// struct timeval tv;
	// gettimeofday(&tv, NULL);
	// srand(tv.tv_sec * tv.tv_usec);
	// mask_int = rand();
	// memcpy(mask, &mask_int, 4);
	switch (ctl_frame->opcode)
	{
	case OP_CODE_CONTROL_CLOSE:
#ifdef DEBUG
		// LIBWSCLIENT_ON_INFO(c, "websocket 收到控制---关闭.\n");
		if (ctl_frame->payload_len > 0)
		{
			char buff[1024] = {0};
			sprintf(buff, "websocket 收到控制---关闭, len: %llu; code: %x,%x; reason: %s", ctl_frame->payload_len, ctl_frame->payload[0], ctl_frame->payload[1], ctl_frame->payload + 2);
			LIBWSCLIENT_ON_INFO(c, buff);
		}
#endif 
		// close frame 定义 by rfc6455:
		// 1. payload 一般为空。如果不为空，格式为： 2byte 错误码 + 任意长度的错误原因。
		// 1.1 收到有playload的close frame，回复的close frame，需要原样带上payload。
		// 2. 收到 close frame，必须回复一个 close frame，除非是自己主动发的(避免死循环).
		// 3. close frame 必须是最后一个frame. 此后不允许再发任何包。
		if (!(TEST_FLAG(c, FLAG_CLIENT_CLOSEING)))
		{
			// server request close.  Send close frame as acknowledgement.
			libwsclient_send_data(c, OP_CODE_CONTROL_CLOSE, ctl_frame->payload, ctl_frame->payload_len);
			update_wsclient_status(c, FLAG_CLIENT_CLOSEING, 0);
		}
		break;
	// ping, pong in rfc6455:
	// 1. ping 可以携带payload，如果有携带， pong需要原样带上（除了mask）。
	// 2. 如果来不及 pong，那么只需要对最近一次的ping做pong就行。
	// 3. 可以在没有ping的时候，主动pong。这就形成了单向的（无需回应的）的心跳检查机制。
	case OP_CODE_CONTROL_PING:
#ifdef DEBUG
		LIBWSCLIENT_ON_INFO(c, "websocket 收到控制---PING.\n");
#endif 
		libwsclient_send_data(c, OP_CODE_CONTROL_PONG, ctl_frame->payload, ctl_frame->payload_len);
		break;
	case OP_CODE_CONTROL_PONG:
#ifdef DEBUG
		LIBWSCLIENT_ON_INFO(c, "websocket 收到控制---PONG.\n");
#endif 
		// 无需响应
		break;
	default:
		LIBWSCLIENT_ON_ERROR(c, "Unhandled control frame received.\n");
		break;
	}
}

inline void handle_on_data_frame_in(wsclient *c, wsclient_frame_in *pframe)
{
#ifdef DEBUG
	LIBWSCLIENT_ON_INFO(c, "websocket 收到数据.\n");
#endif
	if (pframe->fin)
	{
		if (pframe->opcode == OP_CODE_CONTINUE)
		{ // 多幁，需合并。
			pframe->prev_frame = c->current_frame;
			c->current_frame->next_frame = pframe;
			wsclient_frame_in *p = pframe;
			unsigned long long payload_len = p->payload_len;
			while (p->prev_frame)
			{
				p = p->prev_frame;
				payload_len += p->payload_len;
			}
			int op = p->opcode;
			unsigned char *payload = calloc(payload_len, 1);
			int offset = 0;
			memcpy(payload, p->payload, p->payload_len);
			offset += p->payload_len;
			while (p->next_frame)
			{
				free(p->payload);
				free(p);
				p = p->next_frame;
				memcpy(payload + offset, p->payload, p->payload_len);
				offset += p->payload_len;
			}
			free(p->payload);
			free(p);
			c->current_frame = NULL;

			// 按照rfc6455, 多帧只可能是数据帧，控制帧只能是单帧，且payload在126以内。
			if (c->onmessage)
				c->onmessage(c, op & OP_CODE_TYPE_TEXT, payload_len, payload);
			free(payload);
		}
		else
		{
			// 单独一帧
			if ((pframe->opcode & OP_CODE_CONTROL_CLOSE) == OP_CODE_CONTROL_CLOSE)
			{
				// 控制帧。 
				libwsclient_handle_control_frame(c, pframe);
			}
			else
			{
				// 单帧消息
				if (c->onmessage)
					c->onmessage(c, pframe->opcode & OP_CODE_TYPE_TEXT, pframe->payload_len, pframe->payload);
			}
			free(pframe->payload);
			free(pframe);
		}
	}
	else
	{ // 多帧合并，尚未结束。
		if (c->current_frame == NULL)
			c->current_frame = pframe;
		else
		{
			c->current_frame->next_frame = pframe;
			pframe->prev_frame = c->current_frame;
			c->current_frame = pframe;
		}
	}
}

int libwsclient_open_connection(const char *host, const char *port)
{
	struct addrinfo hints, *servinfo, *p;
	int rv, sockfd;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if ((rv = getaddrinfo(host, port, &hints, &servinfo)) != 0)
	{
		return 0;
	}

	for (p = servinfo; p != NULL; p = p->ai_next)
	{
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
		{
			continue;
		}
		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
		{
			close(sockfd);
			continue;
		}
		break;
	}
	freeaddrinfo(servinfo);
	if (p == NULL)
	{
		return 0;
	}
	return sockfd;
}

void *libwsclient_handshake_thread(void *ptr)
{
	wsclient *client = (wsclient *)ptr;
	const char *URI = client->URI;
	SHA1Context shactx;
	const char *UUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	unsigned char sha1bytes[20] = {0};
	char websocket_key[256];
	unsigned char key_nonce[16] = {0};
	char scheme[10];
	char host[200];
	char request_host[256];
	char port[10];
	char path[255];
	char recv_buf[1024];
	char *URI_copy = NULL, *p = NULL, *rcv = NULL, *tok = NULL;
	int i, sockfd, n, flags = 0;
	URI_copy = (char *)malloc(strlen(URI) + 1);
	if (!URI_copy)
	{
		LIBWSCLIENT_ON_ERROR(client, "Unable to allocate memory in libwsclient_new.\n");
		return NULL;
	}
	memset(URI_copy, 0, strlen(URI) + 1);
	strncpy(URI_copy, URI, strlen(URI));
	p = strstr(URI_copy, "://");
	if (p == NULL)
	{
		LIBWSCLIENT_ON_ERROR(client, "Malformed or missing scheme for URI.\n");
		return NULL;
	}
	strncpy(scheme, URI_copy, p - URI_copy);
	scheme[p - URI_copy] = '\0';
	if (strcmp(scheme, "ws") != 0 && strcmp(scheme, "wss") != 0)
	{
		LIBWSCLIENT_ON_ERROR(client, "Invalid scheme for URI");
		return NULL;
	}
	if (strcmp(scheme, "ws") == 0)
	{
		strncpy(port, "80", 9);
	}
	else
	{
		strncpy(port, "443", 9);
		update_wsclient_status(client, FLAG_CLIENT_IS_SSL, 0);
	}
	size_t z = 0;
	for (i = p - URI_copy + 3, z = 0; *(URI_copy + i) != '/' && *(URI_copy + i) != ':' && *(URI_copy + i) != '\0'; i++, z++)
	{
		host[z] = *(URI_copy + i);
	}
	host[z] = '\0';
	if (*(URI_copy + i) == ':')
	{
		i++;
		p = strchr(URI_copy + i, '/');
		if (!p)
			p = strchr(URI_copy + i, '\0');
		strncpy(port, URI_copy + i, (p - (URI_copy + i)));
		port[p - (URI_copy + i)] = '\0';
		i += p - (URI_copy + i);
	}
	if (*(URI_copy + i) == '\0')
	{
		// end of URI request path will be /
		strncpy(path, "/", 2);
	}
	else
	{
		strncpy(path, URI_copy + i, 254);
	}
	free(URI_copy);
	sockfd = libwsclient_open_connection(host, port);

	if (sockfd <= 0)
	{
		LIBWSCLIENT_ON_ERROR(client, "Error while getting address info");

		return NULL;
	}

	if (TEST_FLAG(client, FLAG_CLIENT_IS_SSL))
	{
		static bool b_ssl_need_inited = true;
		if (b_ssl_need_inited)
		{ // openssl 版本号小于等于 1.0.2 时，需要加入这个初始化；大于 1.1.0 则无需调用，自动完成。
			SSL_library_init();
			SSL_load_error_strings();
			b_ssl_need_inited = false;
		}
		client->ssl_ctx = SSL_CTX_new(SSLv23_method());
		client->ssl = SSL_new(client->ssl_ctx);
		SSL_set_fd(client->ssl, sockfd);
		SSL_connect(client->ssl);
	}

	pthread_mutex_lock(&client->lock);
	client->sockfd = sockfd;
	pthread_mutex_unlock(&client->lock);
	// perform handshake
	// generate nonce
	srand(time(NULL));
	for (z = 0; z < 16; z++)
	{
		key_nonce[z] = rand() & 0xff;
	}
	base64_encode(key_nonce, 16, websocket_key, 256);

	if (strcmp(port, "80") != 0)
	{
		snprintf(request_host, 256, "%s:%s", host, port);
	}
	else
	{
		snprintf(request_host, 256, "%s", host);
	}
	char request_headers[1024] = {0};
	snprintf(request_headers, 1024, "GET %s HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nHost: %s\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n", path, request_host, websocket_key);
	n = _libwsclient_write(client, request_headers, strlen(request_headers));
	z = 0;
	memset(recv_buf, 0, 1024);
	// TODO: actually handle data after \r\n\r\n in case server
	//  sends post-handshake data that gets coalesced in this recv
	do
	{
		n = _libwsclient_read(client, recv_buf + z, 1023 - z);
		z += n;
	} while ((z < 4 || strstr(recv_buf, "\r\n\r\n") == NULL) && n > 0);

	if (n <= 0)
	{
		LIBWSCLIENT_ON_ERROR(client, "WS_HANDSHAKE_REMOTE_CLOSED_or_other_receive_ERR");
		return NULL;
	}

	// parse recv_buf for response headers and assure Accept matches expected value
	rcv = (char *)calloc(strlen(recv_buf) + 1, 1);
	if (!rcv)
	{
		LIBWSCLIENT_ON_ERROR(client, "Unable to allocate memory in libwsclient_new.\n");
		return NULL;
	}
	strncpy(rcv, recv_buf, strlen(recv_buf));

	char pre_encode[256] = {0};
	snprintf(pre_encode, 256, "%s%s", websocket_key, UUID);
	SHA1Reset(&shactx);
	SHA1Input(&shactx, (unsigned char*)pre_encode, strlen(pre_encode));
	SHA1Result(&shactx);
	memset(pre_encode, 0, 256);
	snprintf(pre_encode, 256, "%08x%08x%08x%08x%08x", shactx.Message_Digest[0], shactx.Message_Digest[1], shactx.Message_Digest[2], shactx.Message_Digest[3], shactx.Message_Digest[4]);
	for (z = 0; z < (strlen(pre_encode) / 2); z++)
		sscanf(pre_encode + (z * 2), "%02hhx", sha1bytes + z);
	char expected_base64[512] = {0};
	base64_encode(sha1bytes, 20, expected_base64, 512);
	for (tok = strtok(rcv, "\r\n"); tok != NULL; tok = strtok(NULL, "\r\n"))
	{
		if (*tok == 'H' && *(tok + 1) == 'T' && *(tok + 2) == 'T' && *(tok + 3) == 'P')
		{
			p = strchr(tok, ' ');
			p = strchr(p + 1, ' ');
			*p = '\0';
			if (strcmp(tok, "HTTP/1.1 101") != 0 && strcmp(tok, "HTTP/1.0 101") != 0)
			{
				LIBWSCLIENT_ON_ERROR(client, "Remote web server responded with bad HTTP status during handshake");
				LIBWSCLIENT_ON_INFO(client, "handshake resp: \n\t");
				LIBWSCLIENT_ON_INFO(client, rcv);

				return NULL;
			}
			flags |= FLAG_REQUEST_VALID_STATUS;
		}
		else
		{
			p = strchr(tok, ' ');
			*p = '\0';
			if (strcmp(tok, "Upgrade:") == 0)
			{
				if (stricmp(p + 1, "websocket") == 0)
				{
					flags |= FLAG_REQUEST_HAS_UPGRADE;
				}
			}
			if (strcmp(tok, "Connection:") == 0)
			{
				if (stricmp(p + 1, "upgrade") == 0)
				{
					flags |= FLAG_REQUEST_HAS_CONNECTION;
				}
			}
			if (strcmp(tok, "Sec-WebSocket-Accept:") == 0)
			{
				if (strcmp(p + 1, expected_base64) == 0)
				{
					flags |= FLAG_REQUEST_VALID_ACCEPT;
				}
			}
		}
	}
	if (!(flags & (FLAG_REQUEST_HAS_UPGRADE | FLAG_REQUEST_HAS_CONNECTION | FLAG_REQUEST_VALID_ACCEPT)))
	{
		LIBWSCLIENT_ON_ERROR(client, "Remote web server did not respond with expcet ( update, accept, connection) header during handshake");
		return NULL;
	}
#ifdef DEBUG
	// LIBWSCLIENT_ON_INFO(client, "websocket握手完成.\n");
#endif
	update_wsclient_status(client, 0, FLAG_CLIENT_CONNECTING);

	if (client->onopen != NULL)
	{
		client->onopen(client);
	}
	return NULL;
}

// somewhat hackish stricmp
int stricmp(const char *s1, const char *s2)
{
	register unsigned char c1, c2;
	register unsigned char flipbit = ~(1 << 5);
	do
	{
		c1 = (unsigned char)*s1++ & flipbit;
		c2 = (unsigned char)*s2++ & flipbit;
		if (c1 == '\0')
			return c1 - c2;
	} while (c1 == c2);
	return c1 - c2;
}

ssize_t _libwsclient_read(wsclient *c, void *buf, size_t length)
{
	ssize_t n = 0;
	char* sp = "";

	if (TEST_FLAG(c, FLAG_CLIENT_IS_SSL))
	{
		sp = "ssl";
		n = (ssize_t)SSL_read(c->ssl, buf, length);
	}
	else
	{
		n = recv(c->sockfd, buf, length, 0);
	}
#ifdef DEBUG
	char buff[256] = {0};
	sprintf(buff, "wsclient %s read %ld bytes.",sp, n);
	LIBWSCLIENT_ON_INFO(c, buff);
#endif
	return n;
}

ssize_t _libwsclient_write(wsclient *c, const void *buf, size_t length)
{
	pthread_mutex_lock(&c->send_lock);
	ssize_t len = 0;
	char* sp = "";
	if (TEST_FLAG(c, FLAG_CLIENT_IS_SSL))
	{
		sp = "ssl";
		len = (ssize_t) SSL_write(c->ssl, buf, length);
	}
	else
	{
		sp = "";
		len =  send(c->sockfd, buf, length, 0);
	}
	pthread_mutex_unlock(&c->send_lock);
#ifdef DEBUG
	char buff[256] = {0};
	sprintf(buff, "wsclient %s send %ld of %ld bytes.",sp, len, length);
	LIBWSCLIENT_ON_INFO(c, buff);
#endif
	return len;
}

void update_wsclient_status(wsclient *c, int add, int del)
{
	pthread_mutex_lock(&c->lock);
	if (add)
		c->flags |= add;
	if (del)
		c->flags &= ~del;
	pthread_mutex_unlock(&c->lock);
}
