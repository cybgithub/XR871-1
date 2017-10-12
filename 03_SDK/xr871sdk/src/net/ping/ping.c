#include <stdio.h>
#include <string.h>
#include <lwip/tcpip.h>
#include <lwip/inet.h>
#include "lwip/sockets.h"
#include <lwip/icmp.h>
#include <lwip/inet_chksum.h>
#include "lwip/mem.h"
#include "net/ping/ping.h"

int PING_IDs = 0x1234;
#define PING_TO		5000    /* timeout to wait every reponse(ms) */
#define PING_ID		0xABCD
#define PING_DATA_SIZE	100     /* size of send frame buff, not include ICMP frma head */
#define GET_TICKS	OS_GetTicks

static void generate_ping_echo(u8_t *buf, u32_t len, u16_t seq)
{
	u32_t i;
	u32_t data_len = len - sizeof(struct icmp_echo_hdr);
	struct icmp_echo_hdr *pecho;

	pecho = (struct icmp_echo_hdr *)buf;

	ICMPH_TYPE_SET(pecho, ICMP_ECHO);
	ICMPH_CODE_SET(pecho, 0);

	pecho->chksum = 0;
	pecho->id = PING_IDs;
	pecho->seqno = htons(seq);

	/* fill the additional data buffer with some data */
	for (i = 0; i < data_len; i++) {
		buf[sizeof(struct icmp_echo_hdr) + i] = (unsigned char)i;
	}
	/* Checksum of icmp header and data */
	pecho->chksum = inet_chksum(buf, len);
}

s32_t ping(struct ping_data *data)
{
	struct sockaddr_in ToAddr;
	struct sockaddr_in FromAddr;
	socklen_t          FromLen;
	int 	           iSockID,iStatus;
	fd_set ReadFds;
	struct timeval Timeout;

	u8_t *ping_buf, *reply_buf;
	u32_t  ping_size, reply_size;
	struct ip_hdr *iphdr;
	struct icmp_echo_hdr *pecho;
	u16_t ping_seq_num = 1;
	s32_t ping_pass = 0;
	u32_t i;
	u32_t TimeStart,TimeNow,TimeElapse;
	PING_IDs++;
	if (PING_IDs == 0x7FFF)
		PING_IDs = 0x1234;
	memset(&FromAddr, 0, sizeof(FromAddr));
	FromLen = sizeof(FromAddr);

	iSockID = lwip_socket(AF_INET, SOCK_RAW, IP_PROTO_ICMP);
	if (iSockID < 0) {
		printf("create socket fail.\n");
		return -1;
	}

	lwip_fcntl(iSockID, F_SETFL, O_NONBLOCK);  /* set noblocking */

	memset(&ToAddr, 0, sizeof(ToAddr));
	ToAddr.sin_len = sizeof(ToAddr);
	ToAddr.sin_family = AF_INET;
	//ToAddr.sin_port = data->port;
	ToAddr.sin_addr.s_addr = data->sin_addr.addr;

	ping_size = sizeof(struct icmp_echo_hdr) + PING_DATA_SIZE;
	ping_buf = (u8_t *)mem_malloc((mem_size_t)ping_size);
	if (!ping_buf) {
        	return -1;
	}
	reply_buf = (u8_t *)mem_malloc(PING_DATA_SIZE + 50);   /* reserve more buf */
	if (!reply_buf) {
		mem_free(ping_buf);
		return -1;
	}

	for (i = 0; i < data->count; i++) {
		generate_ping_echo(ping_buf, ping_size, ping_seq_num);
		OS_Sleep(1);
		lwip_sendto(iSockID, ping_buf, ping_size, 0, (struct sockaddr*)&ToAddr, sizeof(ToAddr));
		TimeStart = GET_TICKS();
		while (1) {
			FD_ZERO(&ReadFds);
			FD_SET(iSockID, &ReadFds);
			Timeout.tv_sec = 0;
			Timeout.tv_usec = 50*1000;   /* 50ms */
			iStatus = lwip_select(FD_SETSIZE, &ReadFds, NULL, NULL, &Timeout);
			if (iStatus > 0 && FD_ISSET(iSockID, &ReadFds)) {
			/* block mode can't be used, we wait here if receiving party has sended,
			 * but we can set select to timeout mode to lower cpu's utilization */
				reply_size = lwip_recvfrom(iSockID, reply_buf, (PING_DATA_SIZE + 50), 0,
				                           (struct sockaddr*)&FromAddr, &FromLen);
				if (reply_size >= (int)(sizeof(struct ip_hdr)+sizeof(struct icmp_echo_hdr))) {
					TimeNow = GET_TICKS();
					if (TimeNow >= TimeStart) {
						TimeElapse = TimeNow - TimeStart;
					} else {
						TimeElapse = 0xffffffffUL - TimeStart + TimeNow;
					}
					iphdr = (struct ip_hdr *)reply_buf;
					pecho = (struct icmp_echo_hdr *)(reply_buf + (IPH_HL(iphdr) * 4));
					if ((pecho->id == PING_IDs) && (pecho->seqno == htons(ping_seq_num))) {
						/* do some ping result processing */
						printf("%d bytes from %s: icmp_seq=%d	 time=%d ms\n",
						       (reply_size - sizeof(struct ip_hdr)), inet_ntoa(FromAddr.sin_addr),
						       htons(pecho->seqno), TimeElapse);
						ping_pass++;
						break;
					}
				}
			}

			TimeNow = GET_TICKS();
			if (TimeNow >= TimeStart) {
				TimeElapse=TimeNow - TimeStart;
			} else {
				TimeElapse=0xffffffffUL - TimeStart + TimeNow;
			}
			if (TimeElapse >= PING_TO) {  /* giveup this wait, if wait timeout */
				printf("Request timeout for icmp_seq=%d\n", ping_seq_num);
				break;
			}
		}
		ping_seq_num++;
	}

	mem_free(ping_buf);
	mem_free(reply_buf);
	lwip_close(iSockID);
	if (ping_pass > 0)
		return ping_pass;
	else
		return -1;
}
