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



void tcp_ping(int sock_tcp, int sock_icmp, struct sockaddr* target, char ttl_max=64, char n_tries=3){

  struct timeval tv_out; // Receive time out for socket
	tv_out.tv_sec = 3;
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
  struct timespec start, end;
  unsigned long ping_delay = 1ul * 1000000000; // Time between consecutive packets sent in nanoseconds

  for (int ttl = 1; ttl <= ttl_max && !reached; ++ttl) {
    printf("\n %d)\t", ttl);

    if (setsockopt(sock_tcp, SOL_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
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

      if (connect(sock_tcp, target, sizeof(*target)) == 0) {
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

              char* buf = (char*) malloc(200);
              inet_ntop(received.sin_family, &received.sin_addr.s_addr, buf, 200);
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
  int sock1 = socket (PF_INET, SOCK_STREAM, 0);
  if(sock1 < 0)
	{
		//socket creation failed, may be because of non-root privileges
		perror("Failed to create socket");
		return -1;
	}
  
  int sock2 = socket (PF_INET, SOCK_RAW, IPPROTO_ICMP);
  if(sock2 < 0)
	{
		//socket creation failed, may be because of non-root privileges
		perror("Failed to create socket");
		return -1;
	}

  struct sockaddr_in a;
	bzero(&a, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = 0;
	inet_pton(AF_INET, "8.8.8.8", &a.sin_addr);

  tcp_ping(sock1, sock2, (struct sockaddr*) &a);
	
}
