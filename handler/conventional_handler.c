#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

#define PORT 9000
#define BUF_SIZE 65535

long long get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}

int main() {
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    char buffer[BUF_SIZE];

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    int bufsize = 8 * 1024 * 1024; // 8 MB
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize)) < 0) {
        perror("setsockopt(SO_RCVBUF) failed");
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    FILE *logfile = fopen("recv_log.csv", "w");
    if (!logfile) {
        perror("cannot open log file");
        exit(EXIT_FAILURE);
    }
    fprintf(logfile, "recv_time_us,packet_len\n");

    socklen_t len = sizeof(cliaddr);

    printf("Listening on UDP port %d...\n", PORT);

    long packet_count = 0;
    while (1) {
        ssize_t n = recvfrom(sockfd, buffer, BUF_SIZE, 0, (struct sockaddr *)&cliaddr, &len);
        if (n < 0) {
            perror("recvfrom error");
            continue;
        }

        long long now = get_time_us();
        fprintf(logfile, "%lld,%zd\n", now, n);

        // Flush every 100 packets instead of every packet → reduce disk overhead
        if (++packet_count % 100 == 0)
            fflush(logfile);
    }

    close(sockfd);
    fclose(logfile);
    return 0;
}
