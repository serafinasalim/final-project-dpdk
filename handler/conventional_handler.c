#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>

#define PORT 9000
#define BUF_SIZE 2048
#define FLUSH_INTERVAL 1000

#pragma pack(push,1)
typedef struct {
    uint32_t seq;
    uint32_t spare;
    uint64_t send_ns;          
} packet_t;
#pragma pack(pop)

static inline uint64_t nsec_now() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int main(int argc, char *argv[]) {
    const char *log_filename = "recv_log_socket.csv";  

    if (argc > 1) {
        log_filename = argv[1];
    }

    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    char buffer[BUF_SIZE];

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    // perbesar kernel recv buf agar no-drop saat burst
    int bufsize = 8 * 1024 * 1024; // 8MB
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind"); exit(EXIT_FAILURE);
    }

    FILE *logfile = fopen(log_filename, "w");
    if (!logfile) { perror("fopen"); exit(EXIT_FAILURE); }

    fprintf(logfile, "recv_time_us,packet_len,seq,latency_us\n");
    printf("Socket handler listening on UDP %d…\n", PORT);
    printf("Logging to: %s\n", log_filename);

    socklen_t len = sizeof(cliaddr);
    long pkt_count = 0;
    while (1) {
        ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                             (struct sockaddr *)&cliaddr, &len);
        if (n < 0) { perror("recvfrom"); continue; }

        uint64_t now_ns = nsec_now();
        uint64_t now_us = now_ns / 1000ULL;

        double latency_us = -1.0;
        uint32_t seq = 0xffffffffu;

        if ((size_t)n >= sizeof(packet_t)) {
            packet_t *p = (packet_t *)buffer;
            if (pkt_count < 5) {
                printf("DEBUG RECV: seq=%u send_ns=%llu now_ns=%llu\n",
                       p->seq,
                       (unsigned long long)p->send_ns,
                       (unsigned long long)now_ns);
                fflush(stdout);
            }
            seq = p->seq;
            if (p->send_ns > 0) {
                long long diff_ns = (long long)now_ns - (long long)p->send_ns;
                if (diff_ns >= -100000)
                    latency_us = diff_ns / 1000.0;
            }
        }

        fprintf(logfile, "%llu,%zd,%u,%.3f\n",
                (unsigned long long)now_us, n, seq, latency_us);

        if (++pkt_count % FLUSH_INTERVAL == 0) fflush(logfile);
    }

    fclose(logfile);
    close(sockfd);
    return 0;
}
