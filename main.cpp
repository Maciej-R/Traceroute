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
#include <linux/in6.h>
#include <asm/byteorder.h>

// Automatic port number
#define PORT_NO 0

struct ipv6hdr {
#if defined(__LITTLE_ENDIAN_BITFIELD)
        __u8                    priority:4, version:4;
#elif defined(__BIG_ENDIAN_BITFIELD)
        __u8                        version:4,
                                priority:4;
#else
#error        "Please fix <asm/byteorder.h>"
#endif
        __u8                        flow_lbl[3];

        __be16                        payload_len;
        __u8                        nexthdr;
        __u8                        hop_limit;

        struct        in6_addr        saddr;
        struct        in6_addr        daddr;
};

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
void send_ping(int ping_sockfd, struct sockaddr_in* target, time_t t_out=3, char ttl_max=64, char n_tries=3, unsigned long delay=1, int payload_len=0, char random_payload=1){

	struct sockaddr_in received; // Address structre for received packet
	struct timespec start, end, process_start, process_end; // Duration calculations (per packet - start, end - and total for all packets - process_start, process_end)
	bzero(&end, sizeof(end));
	unsigned long ping_delay = delay * 1000000000; // Time between consecutive packets sent in nanoseconds
	char reached = 0; // Flag indicating that response from target has been received
	int n_received = 0, n_sent = 0; // Count received, successfully sent packets
	struct timeval tv_out; // Receive time out for socket
	tv_out.tv_sec = t_out;
	tv_out.tv_usec = 0;

	size_t hdr_sz = sizeof(struct icmphdr);
	char* icmp_pkt = (char*) malloc(hdr_sz + payload_len); // Whole ICMP packet
	int rcv_icmp_pkt_sz = hdr_sz + payload_len + sizeof(iphdr); 
	char* rcv_icmp_pkt = (char*) malloc(rcv_icmp_pkt_sz); // Receive buffer
	char* icmp_payload = icmp_pkt + hdr_sz; // Preparing ICMP data
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
	
	clock_gettime(CLOCK_MONOTONIC, &process_start);

	for (int ttl = 1; ttl <= ttl_max && !reached; ++ttl) {

		printf("%d) ", ttl);

		// Set current TTL
		if (setsockopt(ping_sockfd, SOL_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
				printf("Error while setting TTL \"%s\"\n", strerror(errno));
				return;
		}

		in_addr responding_address;
		memset(&responding_address, 0, sizeof(in_addr));
		for (int i = 0; i < n_tries; ++i) {

			uint16_t id = (uint16_t)(rand() % (2 << 15));
			icmp->un.echo.id = id;
			icmp->un.echo.sequence = seq_start + i + ttl;
			bzero(&(icmp->checksum), 2); // Checksum has to be 0 to calculate it
			icmp->checksum = checksum(&icmp_pkt, sizeof(icmp_pkt)); 
			
			clock_gettime(CLOCK_MONOTONIC, &start);
			unsigned long diff = (start.tv_sec - end.tv_sec) * 1000000000 + start.tv_nsec - end.tv_nsec;
			// Wait given time not to flood with pings
			if (diff < ping_delay) {
				usleep((ping_delay - diff)/1000);
				clock_gettime(CLOCK_MONOTONIC, &start);
			}

			if (sendto(ping_sockfd, &icmp_pkt, sizeof(icmp_pkt), 0, (struct sockaddr*) target, sizeof(struct sockaddr)) < 0){
				printf("Packet couldn't have been sent %s\n", strerror(errno));
				return;
			}
			++n_sent;

			socklen_t len = sizeof(received);
			char response_received = 0;
			while (!response_received){ // Check in loop in case other packets are received in the meantime
				int r_code = recvfrom(ping_sockfd, rcv_icmp_pkt, rcv_icmp_pkt_sz, 0, (struct sockaddr*) &received, &len);
				if (r_code < 0){
					if (errno == EAGAIN || errno == EWOULDBLOCK) { printf("*\t"); fflush(stdout); } // Timeout
					else printf("Packet receive failed! %d - %s\n", errno, strerror(errno)); // Error
					break;
				}
				clock_gettime(CLOCK_MONOTONIC, &end);
				if (r_code > 0){

					iphdr* received_ip_hdr = (iphdr*) rcv_icmp_pkt;
					if (received_ip_hdr->protocol == IPPROTO_ICMP){

						icmphdr* received_header = (icmphdr*) (rcv_icmp_pkt + sizeof(iphdr)); // Extract ICMP
						sockaddr_in* dst = (sockaddr_in*) &target;
						if (received_header->type == ICMP_TIME_EXCEEDED || (received_header->type == ICMP_ECHOREPLY && memcmp(&received.sin_addr, &(dst->sin_addr), 4))) { // Timeout or target reached					
							
							response_received = 1;
							if (received_header->type == ICMP_ECHOREPLY) reached = 1;

							unsigned long duration = (end.tv_sec - start.tv_sec) * 1000000000 + end.tv_nsec - start.tv_nsec;
							duration /= 1000000; // To msec

							if (memcmp(&responding_address, &received.sin_addr, sizeof(responding_address))) {
								char* buf = (char*) malloc(200);
								inet_ntop(received.sin_family, &received.sin_addr, buf, 200);
								printf("%s\t%lu ms\t", buf, duration);
								fflush(stdout);
								free(buf);
							} else {
								printf("%lu ms\t", duration);
								fflush(stdout);
							}

							++n_received;
							responding_address = received.sin_addr;

						}

					}
				}
			}
		}

		printf("\n");

	}

	clock_gettime(CLOCK_MONOTONIC, &process_end);	
	float s = n_sent/n_received;
	printf("Sent %d packets, received %d, success %.2f\n", n_sent, n_received, s);
	unsigned long diff = (process_end.tv_sec - process_start.tv_sec) * 1000 + (process_end.tv_nsec - process_start.tv_nsec) / 1000000;
	printf("Duration: %lu [msec]\n", diff);

}

void send_pingv6(int ping_sockfd, struct sockaddr_in6* target, time_t t_out=3, char ttl_max=64, char n_tries=3, unsigned long delay=1, int payload_len=0, char random_payload=1){

	struct sockaddr_in6 received; // Address structre for received packet
	struct timespec start, end, process_start, process_end; // Duration calculations (per packet - start, end - and total for all packets - process_start, process_end)
	bzero(&end, sizeof(end));
	unsigned long ping_delay = delay * 1000000000; // Time between consecutive packets sent in nanoseconds
	char reached = 0; // Flag indicating that response from target has been received
	int n_received = 0, n_sent = 0; // Count received, successfully sent packets
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
	icmp->type = 128;
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
	
	clock_gettime(CLOCK_MONOTONIC, &process_start);

	for (int ttl = 1; ttl <= ttl_max && !reached; ++ttl) {

		printf("%d)\t", ttl);

		if (setsockopt(ping_sockfd, SOL_IPV6, IPV6_UNICAST_HOPS, &ttl, sizeof(ttl)) < 0) {
			printf("Error while setting TTL \"%s\"\n", strerror(errno));
			return;
		}

		in6_addr responding_address;
		memset(&responding_address, 0, sizeof(in_addr));
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
				return;
			}

			socklen_t len = sizeof(received);
			char response_received = 0;
			while (!response_received){
				int r_code = recvfrom(ping_sockfd, rcv_icmp_pkt, rcv_icmp_pkt_sz, 0, (struct sockaddr*) &received, &len);
				if (r_code < 0){
					if (errno == EAGAIN || errno == EWOULDBLOCK) { printf("*\t"); fflush(stdout); }
					else printf("Packet receive failed! %d - %s\n", errno, strerror(errno));
					break;
				}
				clock_gettime(CLOCK_MONOTONIC, &end);
				if (r_code > 0){

					ipv6hdr* received_ip_hdr = (ipv6hdr*) rcv_icmp_pkt;
					if (1){//received_ip_hdr == IPPROTO_ICMP){

						icmphdr* received_header = (icmphdr*) (rcv_icmp_pkt);
						sockaddr_in6* dst = (sockaddr_in6*) &target;
						if (received_header->type == 3 || (received_header->type == 129 && memcmp(&received.sin6_addr, &(dst->sin6_addr), 16))) {							
							
							response_received = 1;
							if (received_header->type == 129) reached = 1;

							unsigned long duration = (end.tv_sec - start.tv_sec) * 1000000000 + end.tv_nsec - start.tv_nsec;
							duration /= 1000000; // To msec

							if (memcmp(&responding_address, &received.sin6_addr, sizeof(responding_address))) {
								char* buf = (char*) malloc(200);
								inet_ntop(received.sin6_family, &received.sin6_addr, buf, 200);
								printf("%s\t%lu ms\t", buf, duration);
								fflush(stdout);
								free(buf);
							} else {
								printf("%lu ms\t", duration);
								fflush(stdout);
							}

							++n_received;
							responding_address = received.sin6_addr;

						}

					}
				}
			}
		}

		printf("\n");

	}

	clock_gettime(CLOCK_MONOTONIC, &process_end);
	float s = n_sent/n_received;
	printf("Sent %d packets, received %d, success %.2f\n", n_sent, n_received, s);
	unsigned long diff = (process_end.tv_sec - process_start.tv_sec) * 1000 + (process_end.tv_nsec - process_start.tv_nsec) / 1000000;
	printf("Duration: %lu [msec]\n", diff);

}

// Driver Code
int main(int argc, char* argv[]){
	
	// if (argc < 2)
	// {
	// 	printf("Format %s <address>\n", argv[0]);
	// 	return 0;
	// }

	char* address = argv[1];

	unsigned char n_tries = 3;
	unsigned short payload_len = 20;
	time_t t_out = 2;
	char max_ttl = 64;
	unsigned long delay;
	// address = (char*)"www.google.com";

	// Parser program arguments

	int c;
	while((c = getopt(argc, argv, ":n:l:t:d:m:")) != -1) {
		switch(c) {
			case 'n':
				n_tries = atoi(optarg);
				break;
			case 'l':
				payload_len = atoi(optarg);
				break;
			case 't':
				t_out = atoi(optarg);
				break;
			case 'd':
				delay = atoi(optarg);
				break;
			case 'm':
				max_ttl = atoi(optarg);
				break;
			case ':':
				printf("Option %c requires value\n", optopt);
				exit(1);
			case '?':
				printf("Unknown option %c\nPossible options are:\n\t-l for setting payload length for ICMP echo\n\t-n for number of tries for each TTL\n\t-t for setting receive timeout\n", optopt);
				exit(2);
    	}
	}

	if (n_tries > 5 || n_tries < 1) n_tries = 3; // Restore to default if wrong number given
	if (payload_len > 1000 || payload_len < 0) payload_len = 20;
	if (t_out > 10 || t_out < 1) t_out = 2;
	if (delay > 5) delay = 1;
	if (max_ttl > 255 || max_ttl < 1) max_ttl = 64;

	int fd;
	char* ip_addr, * reverse_hostname;
	struct sockaddr_in addr;
	struct sockaddr_in6 addrv6;
	int addrlen = sizeof(addr);
	char net_buf[NI_MAXHOST];
	bzero(&addr, sizeof(addr));
	bzero(&addrv6, sizeof(addrv6));
	char v6 = 0;

	if (inet_pton(AF_INET, address, &addr.sin_addr) > 0) {
		addr.sin_family = AF_INET;
	}
	else if (inet_pton(AF_INET6, address, &addrv6.sin6_addr) > 0) {
		addrv6.sin6_family = AF_INET6;
		v6 = 1;
	}
	else {
		struct hostent* hent = gethostbyname(address);
		if (hent == NULL) {
			printf("Couldn't resolve hostname\n");
			exit(1);
		}
		if (hent->h_addrtype == AF_INET6) {
			v6 = 1;
			addrv6.sin6_addr = *((in6_addr*)(hent->h_addr_list[0]));
			addrv6.sin6_family = AF_INET6;
		}
		else {
			addr.sin_addr = *(struct in_addr*)hent->h_addr;
			addr.sin_family = AF_INET;
		}
	}

	if (v6)
		fd = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6); 
	else 
		fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP); 

	if (fd < 0){
		printf("socket() error\n");
		printf("%s\n", strerror(errno));
		fflush(stdout);
		return 0;
	}

	if (v6 == 0) send_ping(fd, &addr, t_out, max_ttl, n_tries, delay, payload_len);
	else send_pingv6(fd, &addrv6, t_out, max_ttl, n_tries, delay, payload_len);

	return 0;
}
