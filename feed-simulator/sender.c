#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>

#define PAYLOAD_SIZE 128      
#define DEFAULT_RATE 100000   
#define DEFAULT_LOOPS 1000000 

#pragma pack(push,1)
typedef struct {
    uint32_t seq;
    uint32_t spare;
    uint64_t send_ns;   // CLOCK_REALTIME timestamp
    char pad[PAYLOAD_SIZE - 16];
} packet_t;
#pragma pack(pop)

// current time in nanoseconds
static inline uint64_t nsec_now() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <target_ip> <port> [rate_pps] [loops]\n", argv[0]);
        printf("Example: %s 172.31.34.106 9000 100000 1000000\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    long rate  = (argc > 3) ? atol(argv[3]) : DEFAULT_RATE;
    long loops = (argc > 4) ? atol(argv[4]) : DEFAULT_LOOPS;
    long long interval = 1000000000LL / rate; // ns per packet

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    printf("Sending to %s:%d @ %ld pps for %ld packets (interval %lld ns)\n",
           ip, port, rate, loops, interval);

    // pin this sender to high performance mode
    struct timespec ts;
    long long start = nsec_now();
    long long next  = start;
    uint64_t last_report = start;

    packet_t pkt;
    memset(&pkt, 'A', sizeof(pkt)); // isi dummy supaya ukuran payload fix

    for (long i = 0; i < loops; i++) {
        pkt.seq = (uint32_t)i;
        pkt.spare = 0;
        pkt.send_ns = nsec_now();

        if (i < 5) {
            printf("DEBUG: seq=%u send_ns=%llu\n",
                pkt.seq, (unsigned long long)pkt.send_ns);
            fflush(stdout);
        }

        ssize_t sent = sendto(sock, &pkt, sizeof(pkt), 0,
                              (struct sockaddr *)&addr, sizeof(addr));
        if (sent < 0) {
            perror("sendto");
            break;
        }

        next += interval;
        ts.tv_sec  = next / 1000000000LL;
        ts.tv_nsec = next % 1000000000LL;
        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);

        if (i % 100000 == 0 && i > 0) {
            uint64_t now = nsec_now();
            double elapsed = (now - start) / 1e9;
            printf("%ld packets in %.6f s (%.1f pps)\n", i, elapsed, i / elapsed);
        }
    }

    uint64_t end = nsec_now();
    double dur_s = (end - start) / 1e9;
    double real_pps = loops / dur_s;

    printf("Done. Sent %ld packets in %.3f s (%.1f pps)\n", loops, dur_s, real_pps);

    close(sock);
    return 0;
}
