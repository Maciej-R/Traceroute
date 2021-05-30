#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/ip_icmp.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

// Automatic port number
#define PORT_NO 0


// Calculating the Check Sum
unsigned short checksum(void* b, int len)
{
	unsigned short* buf = (unsigned short*) b;
	unsigned int sum = 0;
	unsigned short result;

	for (sum = 0; len > 1; len -= 2)
		sum += *buf++;
	if (len == 1)
		sum += *(unsigned char*)buf;
	sum = (sum >> 16) + (sum & 0xFFFF);
	sum += (sum >> 16);
	result = ~sum;
	return result;
}

// make a ping request
void send_ping(int ping_sockfd, struct sockaddr* target, time_t t_out=3, int payload_len=20, char random_payload=1, char ttl_max=64, char n_tries=3){

	struct sockaddr received;
	long double rtt_msec = 0, total_msec = 0;
	struct timespec start, end;
	bzero(&end, sizeof(end));
	unsigned long ping_delay = 3 * 1000000000; // Time between consecutive packets sent in nanoseconds
	char reached = 0;
	int n_received = 0; 
	struct timeval tv_out; // Receive time out for socket
	tv_out.tv_sec = t_out;
	tv_out.tv_usec = 0;

	size_t hdr_sz = sizeof(struct icmphdr);
	char* icmp_pkt = (char*) malloc(hdr_sz + payload_len);
	char* rcv_icmp_pkt = (char*)malloc(hdr_sz + payload_len);
	char* icmp_payload = icmp_payload + hdr_sz;
	if (random_payload) {
		bzero(icmp_pkt, hdr_sz);
		// Fill payload with random payload
		for (int n = 0; n < payload_len; n += 4) {
			int r = rand();
			if (n + 4 <= payload_len) memcpy(icmp_payload + n, &r, 4);
			else memcpy(icmp_payload + n, &r, payload_len - n);
		}
	}
	else bzero(icmp_pkt, hdr_sz + payload_len);

	struct icmphdr* icmp = (icmphdr*) icmp_pkt;
	icmp -> type = ICMP_ECHO;

	uint16_t seq_start;
	seq_start = ~(seq_start & 0); // Mask with all bits set to 1
	seq_start = (uint16_t)(rand() % seq_start);

	// Receive timeout
	setsockopt(ping_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_out, sizeof tv_out);
	
	for (int ttl = 1; ttl <= ttl_max && !reached; ++ttl) {

		printf("%d)\t", ttl);

		if (setsockopt(ping_sockfd, SOL_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
			printf("Error while setting TTL \n");
			return;
		}

		for (int i = 0; i < n_tries; ++i) {

			uint16_t id = (uint16_t)(rand() % (2 << 15));
			icmp->un.echo.id = id;
			icmp->un.echo.sequence = seq_start + i + ttl;
			icmp->checksum = checksum(&icmp_pkt, sizeof(icmp_pkt));
			
			clock_gettime(CLOCK_MONOTONIC, &start);
			unsigned long diff = (start.tv_sec - end.tv_sec) * 1000000000 + start.tv_nsec - end.tv_nsec;
			if (diff < ping_delay) {
				usleep(ping_delay - diff);
				clock_gettime(CLOCK_MONOTONIC, &start);
			}
						
			if (sendto(ping_sockfd, &icmp_pkt, sizeof(icmp_pkt), 0,	(struct sockaddr*) target, sizeof(*target)) < 0){
				printf("Packet couldn't have been sent\n");
				continue;
			}

			socklen_t len = sizeof(received);
			int r_code = recvfrom(ping_sockfd, &rcv_icmp_pkt, sizeof(rcv_icmp_pkt), 0, (struct sockaddr*) &received, &len);
			if (r_code < 0){
				if (errno == EAGAIN || errno == EWOULDBLOCK) printf("Timeout \n");
				else printf("Packet receive failed!\n");
				continue;
			}
			clock_gettime(CLOCK_MONOTONIC, &end);
			if (r_code > 0){

				unsigned long duration = (start.tv_sec - end.tv_sec) * 1000000000 + start.tv_nsec - end.tv_nsec;
				duration /= 1000000; // To msec

				char* buf = (char*) malloc(200);
				inet_ntop(received.sa_family, &received.sa_data, buf, sizeof(*buf));
				printf("%d ms %s", duration, buf);
				free(buf);

				++n_received;

				if (memcmp(&received.sa_data, &(target->sa_data), received.sa_family == AF_INET ? 4 : 16)) reached = 1;
					
			}
		}

	}

}

// Driver Code
int main(int argc, char* argv[]){
	
	// Parse parameters - TODO
	unsigned char n_tries = 3;
	unsigned short payload_len = 20;
	time_t t_out = 2;

	if (n_tries > 5 || n_tries < 1) n_tries = 3; // Restore to default if wrong number given
	// Check payload len and MTU - TODO
	if (t_out > 10 || t_out < 1) t_out = 2;

	int fd;
	char* ip_addr, * reverse_hostname;
	struct sockaddr addr;
	int addrlen = sizeof(addr);
	char net_buf[NI_MAXHOST];

	if (argc != 2)
	{
		printf("Format %s <address>\n", argv[0]);
		return 0;
	}

	if (inet_pton(AF_INET, argv[1], &addr.sa_data) > 0) {
		addr.sa_family = AF_INET;
	}
	else if (inet_pton(AF_INET, argv[1], &addr.sa_data) > 0) {
		addr.sa_family = AF_INET6;
	}
	else {
		struct addrinfo* ainfo;
		getaddrinfo(argv[1], 0, 0, &ainfo);
		addr = *(ainfo->ai_addr);
	}

	printf("socket()\n");
	if ((fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0){
		printf("socket() error\n");
		printf("%s\n", strerror(errno));
		return 0;
	}

	printf("Strting process");
	send_ping(fd, &addr);

	char* tmp = (char*)malloc(20);
	fgets(tmp, 10, stdin);

	return 0;
}
