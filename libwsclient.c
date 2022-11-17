
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>

#include <sys/types.h>
#include <string.h>

#include <sys/time.h>

#include "./include/libwsclient.h"
#include "wsclient.h"

#include "sha1.h"
#include "utils.h"

wsclient *libwsclient_new(const char *URI)
{
	wsclient *client = NULL;

	client = (wsclient *)calloc(sizeof(wsclient), 1);
	if (!client)
	{
		// LIBWSCLIENT_ON_ERROR(client, "Unable to allocate memory in libwsclient_new.\n");
		return NULL;
	}
	if ((pthread_mutex_init(&client->lock, NULL) != 0) || (pthread_mutex_init(&client->send_lock, NULL) != 0))
	{
		LIBWSCLIENT_ON_ERROR(client, "Unable to init mutex or send lock in libwsclient_new.\n");
		free(client);
		return NULL;
	}
	update_wsclient_status(client, FLAG_CLIENT_CONNECTING, 0);
	client->URI = (char *)calloc(strlen(URI) + 1, 1);
	if (!client->URI)
	{
		LIBWSCLIENT_ON_ERROR(client, "Unable to allocate memory in libwsclient_new.\n");
		free(client);
		return NULL;
	}
	strncpy(client->URI, URI, strlen(URI));

	if (pthread_create(&client->handshake_thread, NULL, libwsclient_handshake_thread, (void *)client))
	{
		LIBWSCLIENT_ON_ERROR(client, "Unable to create handshake thread.\n");
		free(client);
		return NULL;
	}
	return client;
}

void libwsclient_start_run(wsclient *c)
{
	if (TEST_FLAG(c, FLAG_CLIENT_CONNECTING))
	{
		pthread_join(c->handshake_thread, NULL);

		update_wsclient_status(c, 0, FLAG_CLIENT_CONNECTING);

		free(c->URI);
		c->URI = NULL;
	}
	if (c->sockfd)
	{
		pthread_create(&c->run_thread, NULL, libwsclient_run_thread, (void *)c);
	}
	else
	{
		LIBWSCLIENT_ON_ERROR(c, "network failed.\n");
	}
}

void libwsclient_wait_for_end(wsclient *client)
{
	if (client->run_thread)
	{
		pthread_join(client->run_thread, NULL);
	}
}

void libwsclient_close(wsclient *client)
{
	if (!TEST_FLAG(client, FLAG_CLIENT_CLOSEING))
	{
		char *reason = "0 byebye";
		libwsclient_send_data(client, OP_CODE_CONTROL_CLOSE, (unsigned char*)reason, strlen(reason));
		update_wsclient_status(client, FLAG_CLIENT_CLOSEING, 0);
	};
	// 提示退出
	update_wsclient_status(client, FLAG_CLIENT_QUIT, 0);
	libwsclient_wait_for_end(client);
	pthread_mutex_destroy(&client->lock);
	pthread_mutex_destroy(&client->send_lock);
	free(client);
}

void libwsclient_send_string(wsclient *client, char *payload)
{
#ifdef DEBUG
	char buff[1024] = {0};
	sprintf(buff, "websocket 发送数据 message: %s", payload);
	//if (ctl_frame->payload_len > 0)
	{
		LIBWSCLIENT_ON_INFO(client, buff);
	}
#endif

	int nlen = strlen(payload);
	if (nlen <= 0)
		return;
	void *pdata = calloc(1, nlen);
	memcpy(pdata, payload, nlen);
	libwsclient_send_data(client, OP_CODE_TYPE_TEXT, pdata, nlen);
	free(pdata);
}

// 发送数据
// client: wsclient 对象;
// opcode: 类型， OP_CODE_TEXT 或者 OP_CODE_BINARY
// payload: 待发送数据 (utf8字符串，或者字节数据)
// payload_len: 待发送数据长度。
void libwsclient_send_data(wsclient *client, int opcode, unsigned char *payload, unsigned long long payload_len)
{
	int mask_int = 0;

	struct timeval tv;
	gettimeofday(&tv, NULL);
	srand(tv.tv_usec * tv.tv_sec);
	mask_int = rand();



	if (TEST_FLAG(client, (FLAG_CLIENT_CLOSEING | FLAG_CLIENT_QUIT)))
	{
		LIBWSCLIENT_ON_ERROR(client, "Attempted to send after close frame was sent");
		return;
	}
	if (TEST_FLAG(client, FLAG_CLIENT_CONNECTING))
	{
		LIBWSCLIENT_ON_ERROR(client, "Attempted to send during connect");
		return;
	}
	
	// 新分配缓存区方便做mask亦或，以便传入的是const char* 字符串。
	unsigned char *sendbuf = calloc(payload_len + 1, 1);
	memcpy(sendbuf, payload, payload_len);
	payload = sendbuf;

	if (payload_len <= 125)
	{
		unsigned char header[6] = {0};
		header[0] = 0x80 | (opcode & 0x0f); // pframe->fin & 0x80; 	// fin flag and op code
		header[1] = payload_len | 0x80;		// add mask
		memcpy(&header[2], &mask_int, 4);

		for (size_t i = 0; i < payload_len; i++)
			*(payload + i) ^= (header[2 + i % 4] & 0xff); // mask payload

		_libwsclient_write(client, header, 6);
		_libwsclient_write(client, payload, payload_len);
	}
	else if (payload_len > 125 && payload_len <= 0xffff)
	{
		// 是否需要分片
		if (payload_len > MAX_PAYLOAD_SIZE)
		{
			int nfragsize = MAX_PAYLOAD_SIZE;
			int nfrag = payload_len / MAX_PAYLOAD_SIZE;
			if (payload_len % MAX_PAYLOAD_SIZE)
				nfrag += 1;
			int istep = 0;
			int b1 = opcode & 0x0f; // fin = 0, opcode= code;
			do
			{
				unsigned char header[8] = {0};
				header[0] = b1 & 0xff;
				header[1] = 126 | 0x80;
				nfragsize &= 0xffff;
				for (int i = 0; i < 2; i++)
				{
					header[2 + i] = *((char *)&nfragsize + (2 - i - 1));
				}
				memcpy(&header[4], &mask_int, 4);

				for (int i = 0; i < MAX_PAYLOAD_SIZE; i++)
					*(payload + MAX_PAYLOAD_SIZE * istep + i) ^= (header[4 + i % 4] & 0xff); // mask payload
				_libwsclient_write(client, header, 8);
				_libwsclient_write(client, payload + MAX_PAYLOAD_SIZE * istep, nfragsize);

				// next op = continue;
				b1 = OP_CODE_CONTINUE & 0x0f;
			} while (istep < (nfrag - 1));
			// 最后一帧
			{
				// last fin = true;  op = continue;
				b1 = 0x80 | (OP_CODE_CONTINUE & 0x0f);
				nfragsize = payload_len % MAX_PAYLOAD_SIZE;
				if (nfragsize == 0)
					nfragsize = MAX_PAYLOAD_SIZE;

				unsigned char header[8] = {0};
				header[0] = b1 & 0xff;
				header[1] = 126 | 0x80;
				nfragsize &= 0xffff;
				for (int i = 0; i < 2; i++)
				{
					header[2 + i] = *((char *)&nfragsize + (2 - i - 1));
				}
				memcpy(&header[4], &mask_int, 4);
				for (int i = 0; i < nfragsize; i++)
					*(payload + MAX_PAYLOAD_SIZE * istep + i) ^= (header[4 + i % 4] & 0xff); // mask payload
				_libwsclient_write(client, header, 6);
				_libwsclient_write(client, payload + MAX_PAYLOAD_SIZE * (nfrag - 1), nfragsize);
			}
		}
		else
		{ // 单帧
			unsigned char header[8] = {0};
			header[0] = 0x80 | (opcode & 0x0f);
			header[1] = 126 | 0x80;
			int nfragsize = payload_len & 0xffff;
			for (int i = 0; i < 2; i++)
			{
				header[2 + i] = *((char *)&nfragsize + (2 - i - 1));
			}
			memcpy(&header[4], &mask_int, 4);
			for (int i = 0; i < nfragsize; i++)
				*(payload + i) ^= (header[4 + i % 4] & 0xff); // mask payload
			_libwsclient_write(client, header, 8);
			_libwsclient_write(client, payload, nfragsize);
		}
	}
	else if (payload_len > 0xffff && payload_len <= 0xffffffffffffffffLL)
	{
		// 是否需要分片
		if (payload_len > MAX_PAYLOAD_SIZE)
		{
			unsigned long long nfragsize = MAX_PAYLOAD_SIZE;
			unsigned long long nfrag = payload_len / MAX_PAYLOAD_SIZE;
			if (payload_len % MAX_PAYLOAD_SIZE)
				nfrag += 1;
			size_t istep = 0;
			int b1 = opcode & 0x0f; // fin = 0, opcode= code;
			do
			{
				unsigned char header[14] = {0};
				header[0] = b1 & 0xff;
				header[1] = 127 | 0x80;
				for (int i = 0; i < 8; i++)
				{
					header[2 + i] = *((char *)&nfragsize + (8 - i - 1));
				}
				memcpy(&header[10], &mask_int, 4);

				for (unsigned long long i = 0; i < MAX_PAYLOAD_SIZE; i++)
					*(payload + MAX_PAYLOAD_SIZE * istep + i) ^= (header[10 + i % 4] & 0xff); // mask payload
				_libwsclient_write(client, header, 14);
				_libwsclient_write(client, payload + MAX_PAYLOAD_SIZE * istep, nfragsize);

				// next op = continue;
				b1 = OP_CODE_CONTINUE & 0x0f;
			} while (istep < (nfrag - 1));
			// 最后一帧
			{
				// last fin = true;  op = continue;
				b1 = 0x80 | (OP_CODE_CONTINUE & 0x0f);
				nfragsize = payload_len % MAX_PAYLOAD_SIZE;
				if (nfragsize == 0)
					nfragsize = MAX_PAYLOAD_SIZE;

				unsigned char header[14] = {0};
				header[0] = b1 & 0xff;
				header[1] = 127 | 0x80;
				for (int i = 0; i < 8; i++)
				{
					header[2 + i] = *((char *)&nfragsize + (2 - i - 1));
				}
				memcpy(&header[10], &mask_int, 4);
				for (unsigned long long i = 0; i < nfragsize; i++)
					*(payload + MAX_PAYLOAD_SIZE * istep + i) ^= (header[10 + i % 4] & 0xff); // mask payload
				_libwsclient_write(client, header, 14);
				_libwsclient_write(client, payload + MAX_PAYLOAD_SIZE * (nfrag - 1), nfragsize);
			}
		}
		else
		{ // 单帧
			unsigned char header[14] = {0};
			header[0] = 0x80 | (opcode & 0x0f);
			header[1] = 126 | 0x80;
			int nfragsize = payload_len;
			for (int i = 0; i < 8; i++)
			{
				header[2 + i] = *((char *)&nfragsize + (2 - i - 1));
			}
			memcpy(&header[10], &mask_int, 4);
			for (int i = 0; i < nfragsize; i++)
				*(payload + i) ^= (header[10 + i % 4] & 0xff); // mask payload
			_libwsclient_write(client, header, 14);
			_libwsclient_write(client, payload, nfragsize);
		}
	}
	else
	{
		LIBWSCLIENT_ON_ERROR(client, "Attempted to send too much data");
	}
	free(sendbuf);
}

void libwsclient_send_ping(wsclient *client, char *payload)
{
	if (NULL == payload)
		payload = "ok";
	libwsclient_send_data(client, OP_CODE_CONTROL_PING, (unsigned char*)payload, strlen(payload));
}
