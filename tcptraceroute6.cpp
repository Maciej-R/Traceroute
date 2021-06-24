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

void tcp_ping(int sock_tcp, int sock_icmp, struct sockaddr_in6* target, char ttl_max=64, char n_tries=3){

  struct timeval tv_out; // Receive time out for socket
	tv_out.tv_sec = 3;
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
  socklen_t len = sizeof(received);
  int rcv_icmp_pkt_sz = 128;
	char* rcv_icmp_pkt = (char*) malloc(rcv_icmp_pkt_sz);
  char reached = 0;
  struct timespec start, end;
  unsigned long ping_delay = 1ul * 1000000000; // Time between consecutive packets sent in nanoseconds

  for (int ttl = 1; ttl <= ttl_max && !reached; ++ttl) {
    printf("\n %d)\t", ttl);

    if (setsockopt(sock_tcp, SOL_IPV6, IPV6_UNICAST_HOPS, &ttl, sizeof(ttl)) < 0) {
      printf("Error while setting TTL \n");
      return;
    }
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
          if (true){

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

              char* buf = (char*) malloc(200);
              inet_ntop(received.sin6_family, &received.sin6_addr, buf, 200);
              printf("%lu ms %s\t", duration, buf);
              fflush(stdout);
              free(buf);

            }
          }
        }
      }
    }
  }
}

int main() {
  int sock1 = socket (PF_INET6, SOCK_STREAM, 0);
  if(sock1 < 0)
	{
		//socket creation failed, may be because of non-root privileges
		perror("Failed to create socket");
		return -1;
	}
  
  int sock2 = socket (PF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
  if(sock1 < 0)
	{
		//socket creation failed, may be because of non-root privileges
		perror("Failed to create socket");
		return -1;
	}

  struct sockaddr_in6 a;
	bzero(&a, sizeof(a));
	a.sin6_family = AF_INET6;
	a.sin6_port = htons(7003);
	inet_pton(AF_INET6, "2001:4860:4860::8888", &a.sin6_addr);

  tcp_ping(sock1, sock2, (struct sockaddr_in6*) &a);
	
}
