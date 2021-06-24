#include <stdio.h>	//for printf
#include <string.h> //memset
#include <sys/socket.h>	//for socket ofcourse
#include <stdlib.h> //for exit(0);
#include <errno.h> //For errno - the error number
#include <netinet/tcp.h>	//Provides declarations for tcp header
#include <netinet/ip.h>	//Provides declarations for ip header
#include <arpa/inet.h> // inet_addr
#include <unistd.h> // sleep()
#include <netinet/ip_icmp.h>
#include <time.h>
#include <linux/in6.h>
#include <asm/byteorder.h>
#include <netdb.h>


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

void tcp_ping(int sock_tcp, int sock_icmp, struct sockaddr* target, time_t t_out=3, char ttl_max=64, char n_tries=3, unsigned long delay=1){

  struct timeval tv_out; // Receive time out for socket
	tv_out.tv_sec = t_out;
	tv_out.tv_usec = 0;
  if (setsockopt(sock_tcp, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_out, sizeof tv_out) < 0){
    perror("Error while setting TCP receive timeout");
		return;
  }
  if (setsockopt(sock_icmp, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_out, sizeof tv_out) < 0){
    perror("Error while setting ICMP receive timeout");
		return;
  }

  struct sockaddr_in ad0;
	bzero(&ad0, sizeof(ad0));
	ad0.sin_family = AF_INET;
	ad0.sin_port = 0;
	inet_pton(AF_INET, "0.0.0.0", &ad0.sin_addr);
  if (bind(sock_icmp, (struct sockaddr*) &ad0, sizeof(ad0)) < 0){
    perror("Failed to bind socket");
    return;
  }

  struct sockaddr_in received;
  socklen_t len = sizeof(received);
  int rcv_icmp_pkt_sz = 128;
	char* rcv_icmp_pkt = (char*) malloc(rcv_icmp_pkt_sz);
  char reached = 0;
  struct timespec start, end, process_start, process_end;
  unsigned long ping_delay = delay * 1000000000; // Time between consecutive packets sent in nanoseconds
  int n_received = 0, n_sent = 0;

  for (int ttl = 1; ttl <= ttl_max && !reached; ++ttl) {
    printf("\n %d)\t", ttl);

    if (setsockopt(sock_tcp, SOL_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
      printf("Error while setting TTL \n");
      return;
    }

    in_addr responding_address;
		memset(&responding_address, 0, sizeof(in_addr));
    for (int i = 0; i < n_tries; ++i) {
      clock_gettime(CLOCK_MONOTONIC, &start);
			unsigned long diff = (start.tv_sec - end.tv_sec) * 1000000000 + start.tv_nsec - end.tv_nsec;
			if (diff < ping_delay) {
				usleep((ping_delay - diff)/1000);
				clock_gettime(CLOCK_MONOTONIC, &start);
			}

      if (connect(sock_tcp, target, sizeof(*target)) == 0) {
        reached = 1;
      }
      ++n_sent;

      char response_received = 0;
			while (!response_received){
        int r_code = recvfrom(sock_icmp, rcv_icmp_pkt, rcv_icmp_pkt_sz, 0, (struct sockaddr*) &received, &len);
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
              if (&received.sin_addr == &dst->sin_addr){
                reached = 1;
              }

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
  }

  clock_gettime(CLOCK_MONOTONIC, &process_end);
	float s = n_sent/n_received;
	printf("Sent %d packets, received %d, success %.2f\n", n_sent, n_received, s);
	unsigned long diff = (process_end.tv_sec - process_start.tv_sec) * 1000 + (process_end.tv_nsec - process_start.tv_nsec) / 1000000;
	printf("Duration: %lu [msec]\n", diff);
  
}

void tcp_ping6(int sock_tcp, int sock_icmp, struct sockaddr_in6* target, time_t t_out=3, char ttl_max=64, char n_tries=3, unsigned long delay=1){

  struct timeval tv_out; // Receive time out for socket
	tv_out.tv_sec = t_out;
	tv_out.tv_usec = 0;
  if (setsockopt(sock_tcp, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_out, sizeof tv_out) < 0){
    perror("Error while setting TCP receive timeout");
		return;
  }
  if (setsockopt(sock_icmp, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_out, sizeof tv_out) < 0){
    printf("Error while setting ICMP receive timeout");
		return;
  }

  struct sockaddr_in6 ad0;
	bzero(&ad0, sizeof(ad0));
	ad0.sin6_family = AF_INET6;
	ad0.sin6_port = htons(7003);
	inet_pton(AF_INET6, "::", &ad0.sin6_addr);
  if (bind(sock_icmp, (struct sockaddr*) &ad0, sizeof(ad0)) < 0){
    perror("Failed to bind socket");
  }

  struct sockaddr_in6 received;
  int rcv_icmp_pkt_sz = 128;
	char* rcv_icmp_pkt = (char*) malloc(rcv_icmp_pkt_sz);
  char reached = 0;
  struct timespec start, end, process_start, process_end;
  unsigned long ping_delay = delay * 1000000000; // Time between consecutive packets sent in nanoseconds
  int n_received = 0, n_sent = 0;

  for (int ttl = 1; ttl <= ttl_max && !reached; ++ttl) {
    printf("\n %d)\t", ttl);

    if (setsockopt(sock_tcp, SOL_IPV6, IPV6_UNICAST_HOPS, &ttl, sizeof(ttl)) < 0) {
      perror("Error while setting TTL \n");
      return;
    }

    in6_addr responding_address;
    memset(&responding_address, 0, sizeof(in_addr));
    for (int i = 0; i < n_tries; ++i) {
      clock_gettime(CLOCK_MONOTONIC, &start);
			unsigned long diff = (start.tv_sec - end.tv_sec) * 1000000000 + start.tv_nsec - end.tv_nsec;
			if (diff < ping_delay) {
				usleep((ping_delay - diff)/1000);
				clock_gettime(CLOCK_MONOTONIC, &start);
			}

      if (connect(sock_tcp,  (struct sockaddr*) target, sizeof(*target)) == 0) {
        reached = 1;
      }
      ++n_sent;

      socklen_t len = sizeof(received);
      char response_received = 0;
			while (!response_received){
        int r_code = recvfrom(sock_icmp, rcv_icmp_pkt, rcv_icmp_pkt_sz, 0, (struct sockaddr*) &received, &len);
        if (r_code < 0){
          if (errno == EAGAIN || errno == EWOULDBLOCK) printf("*\t");
          else printf("Packet receive failed! %d - %s\n", errno, strerror(errno));
          break;
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
        if (r_code == 0) {printf("Packet empty"); }
        if (r_code > 0){
          ipv6hdr* received_ip_hdr = (ipv6hdr*) rcv_icmp_pkt;

          icmphdr* received_header = (icmphdr*) (rcv_icmp_pkt + sizeof(iphdr));
          sockaddr_in6* dst = (sockaddr_in6*) &target;
          // printf("header type %d", received_header->type );
          if (received_header->type == 9) {							
            response_received = 1;
            if (&received.sin6_addr == &dst->sin6_addr){
              reached = 1;
            }

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

  clock_gettime(CLOCK_MONOTONIC, &process_end);
	float s = n_sent/n_received;
	printf("Sent %d packets, received %d, success %.2f\n", n_sent, n_received, s);
	unsigned long diff = (process_end.tv_sec - process_start.tv_sec) * 1000 + (process_end.tv_nsec - process_start.tv_nsec) / 1000000;
	printf("Duration: %lu [msec]\n", diff);

}


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
	while((c = getopt(argc, argv, ":n:l:t:")) != -1) {
		switch(c) {
			case 'n':
				n_tries = atoi(optarg);
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

	char* ip_addr, * reverse_hostname;
	struct sockaddr_in addr;
	struct sockaddr_in6 addrv6;
	int addrlen = sizeof(addr);
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

  int sock1;
  int sock2;
	if (v6) {
		sock1 = socket (PF_INET6, SOCK_STREAM, 0);
    sock2 = socket (PF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
  }
	else {
		sock1 = socket (PF_INET, SOCK_STREAM, 0);
    sock2 = socket (PF_INET, SOCK_RAW, IPPROTO_ICMP);
}
  if(sock1 < 0)
	{
		//socket creation failed, may be because of non-root privileges
		perror("Failed to create socket");
		return -1;
	}
	if(sock2 < 0)
	{
		//socket creation failed, may be because of non-root privileges
		perror("Failed to create socket");
		return -1;
	}

	if (v6 == 0) tcp_ping(sock1, sock2, (struct sockaddr*) &addr, t_out, max_ttl, n_tries, delay);
	else tcp_ping6(sock1, sock2, &addrv6, t_out, max_ttl, n_tries, delay);

	return 0;
}