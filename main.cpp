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
void send_ping(int ping_sockfd, struct sockaddr_in* target, int family, time_t t_out=3, int payload_len=0, char random_payload=1, char ttl_max=64, char n_tries=3){

	struct sockaddr_in received;
	long double rtt_msec = 0, total_msec = 0;
	struct timespec start, end;
	bzero(&end, sizeof(end));
	unsigned long ping_delay = 1ul * 1000000000; // Time between consecutive packets sent in nanoseconds
	char reached = 0;
	int n_received = 0; 
	struct timeval tv_out; // Receive time out for socket
	tv_out.tv_sec = t_out;
	tv_out.tv_usec = 0;

	size_t hdr_sz = sizeof(struct icmphdr);
	char* icmp_pkt = (char*) malloc(hdr_sz + payload_len);
	int rcv_icmp_pkt_sz = hdr_sz + payload_len + sizeof(iphdr);
	char* rcv_icmp_pkt = (char*) malloc(rcv_icmp_pkt_sz);
	char* icmp_payload = icmp_pkt + hdr_sz;
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

	struct icmphdr* icmp = (icmphdr*) &icmp_pkt;
	icmp->type = ICMP_ECHO;
	icmp->code = 0;

	uint16_t seq_start;
	seq_start = ~(seq_start & 0); // Mask with all bits set to 1
	seq_start = (uint16_t)(rand() % seq_start);
	seq_start = 1;

	// Receive timeout
	if (setsockopt(ping_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_out, sizeof tv_out) < 0) {
		printf("Error while setting receive timeout");
		return;
	}
	
	for (int ttl = 1; ttl <= ttl_max && !reached; ++ttl) {

		printf("%d)\t", ttl);

		if (family == AF_INET6) {
			if (setsockopt(ping_sockfd, SOL_IPV6, IPV6_UNICAST_HOPS, &ttl, sizeof(ttl)) < 0) {
				printf("Error while setting TTL \"%s\"\n", strerror(errno));
				return;
			}
		} else {
			if (setsockopt(ping_sockfd, SOL_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
				printf("Error while setting TTL \"%s\"\n", strerror(errno));
				return;
			}
		}

		for (int i = 0; i < n_tries; ++i) {

			uint16_t id = (uint16_t)(rand() % (2 << 15));
			icmp->un.echo.id = id;
			icmp->un.echo.sequence = seq_start + i + ttl;
			bzero(&(icmp->checksum), 2);
			icmp->checksum = checksum(&icmp_pkt, sizeof(icmp_pkt)); 
			
			clock_gettime(CLOCK_MONOTONIC, &start);
			unsigned long diff = (start.tv_sec - end.tv_sec) * 1000000000 + start.tv_nsec - end.tv_nsec;
			if (diff < ping_delay) {
				usleep((ping_delay - diff)/1000);
				clock_gettime(CLOCK_MONOTONIC, &start);
			}

			if (sendto(ping_sockfd, &icmp_pkt, sizeof(icmp_pkt), 0, (struct sockaddr*) target, sizeof(struct sockaddr)) < 0){
				printf("Packet couldn't have been sent %s\n", strerror(errno));
				continue;
			}

			socklen_t len = sizeof(received);
			char response_received = 0;
			while (!response_received){
				int r_code = recvfrom(ping_sockfd, rcv_icmp_pkt, rcv_icmp_pkt_sz, 0, (struct sockaddr*) &received, &len);
				if (r_code < 0){
					if (errno == EAGAIN || errno == EWOULDBLOCK) printf("*\t");
					else printf("Packet receive failed! %d - %s\n", errno, strerror(errno));
					break;
				}
				clock_gettime(CLOCK_MONOTONIC, &end);
				if (r_code > 0){

					iphdr* received_ip_hdr = (iphdr*) rcv_icmp_pkt;
					if (received_ip_hdr->protocol == IPPROTO_ICMP){

						icmphdr* received_header = (icmphdr*) (rcv_icmp_pkt + sizeof(iphdr));
						sockaddr_in* dst = (sockaddr_in*) &target;
						if (received_header->type == ICMP_TIME_EXCEEDED || (received_header->type == ICMP_ECHOREPLY && memcmp(&received.sin_addr, &(dst->sin_addr), 4))) {							
							
							response_received = 1;
							if (received_header->type == ICMP_ECHOREPLY) reached = 1;

							unsigned long duration = (end.tv_sec - start.tv_sec) * 1000000000 + end.tv_nsec - start.tv_nsec;
							duration /= 1000000; // To msec

							char* buf = (char*) malloc(200);
							inet_ntop(received.sin_family, &received.sin_addr.s_addr, buf, 200);
							printf("%d ms %s\t", duration, buf);
							fflush(stdout);
							free(buf);

							++n_received;

						}

					}
				}
			}
		}

		printf("\n");

	}

}

void send_pingv6(int ping_sockfd, struct sockaddr_in6* target, int family, time_t t_out=3, int payload_len=0, char random_payload=1, char ttl_max=64, char n_tries=3){

	struct sockaddr_in6 received;
	long double rtt_msec = 0, total_msec = 0;
	struct timespec start, end;
	bzero(&end, sizeof(end));
	unsigned long ping_delay = 1ul * 1000000000; // Time between consecutive packets sent in nanoseconds
	char reached = 0;
	int n_received = 0; 
	struct timeval tv_out; // Receive time out for socket
	tv_out.tv_sec = t_out;
	tv_out.tv_usec = 0;

	size_t hdr_sz = sizeof(struct icmphdr);
	char* icmp_pkt = (char*) malloc(hdr_sz + payload_len);
	int rcv_icmp_pkt_sz = hdr_sz + payload_len + sizeof(iphdr);
	char* rcv_icmp_pkt = (char*) malloc(rcv_icmp_pkt_sz);
	char* icmp_payload = icmp_pkt + hdr_sz;
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

	struct icmphdr* icmp = (icmphdr*) &icmp_pkt;
	icmp->type = ICMP_ECHO;
	icmp->code = 0;

	uint16_t seq_start;
	seq_start = ~(seq_start & 0); // Mask with all bits set to 1
	seq_start = (uint16_t)(rand() % seq_start);
	seq_start = 1;

	// Receive timeout
	if (setsockopt(ping_sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_out, sizeof tv_out) < 0) {
		printf("Error while setting receive timeout");
		return;
	}
	
	for (int ttl = 1; ttl <= ttl_max && !reached; ++ttl) {

		printf("%d)\t", ttl);

		if (family == AF_INET6) {
			if (setsockopt(ping_sockfd, SOL_IPV6, IPV6_UNICAST_HOPS, &ttl, sizeof(ttl)) < 0) {
				printf("Error while setting TTL \"%s\"\n", strerror(errno));
				return;
			}
		} else {
			if (setsockopt(ping_sockfd, SOL_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
				printf("Error while setting TTL \"%s\"\n", strerror(errno));
				return;
			}
		}

		for (int i = 0; i < n_tries; ++i) {

			uint16_t id = (uint16_t)(rand() % (2 << 15));
			icmp->un.echo.id = id;
			icmp->un.echo.sequence = seq_start + i + ttl;
			bzero(&(icmp->checksum), 2);
			icmp->checksum = checksum(&icmp_pkt, sizeof(icmp_pkt)); 
			
			clock_gettime(CLOCK_MONOTONIC, &start);
			unsigned long diff = (start.tv_sec - end.tv_sec) * 1000000000 + start.tv_nsec - end.tv_nsec;
			if (diff < ping_delay) {
				usleep((ping_delay - diff)/1000);
				clock_gettime(CLOCK_MONOTONIC, &start);
			}

			if (sendto(ping_sockfd, &icmp_pkt, sizeof(icmp_pkt), 0, (struct sockaddr*) target, sizeof(struct sockaddr_in6)) < 0){
				printf("Packet couldn't have been sent %s\n", strerror(errno));
				continue;
			}

			socklen_t len = sizeof(received);
			char response_received = 0;
			while (!response_received){
				int r_code = recvfrom(ping_sockfd, rcv_icmp_pkt, rcv_icmp_pkt_sz, 0, (struct sockaddr*) &received, &len);
				if (r_code < 0){
					if (errno == EAGAIN || errno == EWOULDBLOCK) printf("*\t");
					else printf("Packet receive failed! %d - %s\n", errno, strerror(errno));
					break;
				}
				clock_gettime(CLOCK_MONOTONIC, &end);
				if (r_code > 0){

					iphdr* received_ip_hdr = (iphdr*) rcv_icmp_pkt;
					if (received_ip_hdr->protocol == IPPROTO_ICMP){

						icmphdr* received_header = (icmphdr*) (rcv_icmp_pkt + sizeof(iphdr));
						sockaddr_in6* dst = (sockaddr_in6*) &target;
						if (received_header->type == ICMP_TIME_EXCEEDED || (received_header->type == ICMP_ECHOREPLY && memcmp(&received.sin6_addr, &(dst->sin6_addr), 16))) {							
							
							response_received = 1;
							if (received_header->type == ICMP_ECHOREPLY) reached = 1;

							unsigned long duration = (end.tv_sec - start.tv_sec) * 1000000000 + end.tv_nsec - start.tv_nsec;
							duration /= 1000000; // To msec

							char* buf = (char*) malloc(200);
							inet_ntop(received.sin6_family, &received.sin6_addr, buf, 200);
							printf("%d ms %s\t", duration, buf);
							fflush(stdout);
							free(buf);

							++n_received;

						}

					}
				}
			}
		}

		printf("\n");

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
	struct sockaddr_in addr;
	struct sockaddr_in6 addrv6;
	int addrlen = sizeof(addr);
	char net_buf[NI_MAXHOST];
	bzero(&addr, sizeof(addr));
	bzero(&addrv6, sizeof(addrv6));
	char v6 = 0;

	// if (argc != 2)
	// {
	// 	printf("Format %s <address>\n", argv[0]);
	// 	return 0;
	// }

	char* test = "2a00:1450:401b:804::2004";
	//char* test = "8.8.8.8";

	if (inet_pton(AF_INET, test, &addr.sin_addr) > 0) {
		addr.sin_family = AF_INET;
	}
	else if (inet_pton(AF_INET6, test, &addrv6.sin6_addr) > 0) {
		addrv6.sin6_family = AF_INET6;
		v6 = 1;
	}
	else {
		// struct addrinfo* ainfo;
		// if (getaddrinfo(test, 0, 0, &ainfo) < 0) {
		// 	printf("Couldn't resolve hostname");
		// 	exit(1);
		// }
		// addr = *(ainfo->ai_addr);
		// if (ainfo->ai_family == AF_INET6) v6 = 1;
	}
	
	//addr.sa_family = AF_INET;

	// struct sockaddr_in a;
	// bzero(&a, sizeof(a));
	// a.sin_family = AF_INET;
	// a.sin_port = 0;
	// inet_pton(AF_INET, "8.8.8.8", &a.sin_addr);

	//printf("socket()\n");
	if (v6)
		fd = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6); 
	else 
		fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP); 
	//printf("%d\n", fd);
	if (fd < 0){
		printf("socket() error\n");
		fflush(stdout);
		printf("%s\n", strerror(errno)) ;
		return 0;
	}

	struct sockaddr* ptr;
	if (v6 == 0) ptr = (struct sockaddr*) &addr;
	else ptr = (struct sockaddr*) &addrv6;

	if (v6 == 0) send_ping(fd, &addr, AF_INET);
	else send_pingv6(fd, &addrv6, AF_INET6);

	return 0;
}
