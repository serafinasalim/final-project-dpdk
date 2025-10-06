#include <rte_common.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024
#define NUM_MBUFS (8191*2)
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

static inline long long get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

int main(int argc, char *argv[]) {
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) rte_exit(EXIT_FAILURE, "Error with EAL init\n");

    uint16_t portid = 0;
    struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL",
                                NUM_MBUFS, MBUF_CACHE_SIZE, 0,
                                RTE_MBUF_DEFAULT_BUF_SIZE,
                                rte_socket_id());
    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    struct rte_eth_conf port_conf = {0};
    ret = rte_eth_dev_configure(portid, 1, 1, &port_conf);
    if (ret < 0) rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d\n", ret);

    ret = rte_eth_rx_queue_setup(portid, 0, RX_RING_SIZE,
                                 rte_eth_dev_socket_id(portid),
                                 NULL, mbuf_pool);
    if (ret < 0) rte_exit(EXIT_FAILURE, "RX queue setup failed\n");

    ret = rte_eth_tx_queue_setup(portid, 0, TX_RING_SIZE,
                                 rte_eth_dev_socket_id(portid),
                                 NULL);
    if (ret < 0) rte_exit(EXIT_FAILURE, "TX queue setup failed\n");

    ret = rte_eth_dev_start(portid);
    if (ret < 0) rte_exit(EXIT_FAILURE, "Device start failed\n");

    printf("DPDK handler started on port %u\n", portid);

    FILE *logfile = fopen("recv_log_dpdk.csv", "w");
    fprintf(logfile, "recv_time_us,packet_len\n");

    struct rte_mbuf *bufs[BURST_SIZE];

    while (1) {
        const uint16_t nb_rx = rte_eth_rx_burst(portid, 0, bufs, BURST_SIZE);
        if (nb_rx == 0) continue;

        for (uint16_t i = 0; i < nb_rx; i++) {
            struct rte_mbuf *mbuf = bufs[i];
            long long now = get_time_us();
            uint16_t len = rte_pktmbuf_pkt_len(mbuf);

            fprintf(logfile, "%lld,%u\n", now, len);
            rte_pktmbuf_free(mbuf);
        }
        fflush(logfile);
    }

    fclose(logfile);
    rte_eth_dev_stop(portid);
    rte_eth_dev_close(portid);
    rte_eal_cleanup();

    return 0;
}
