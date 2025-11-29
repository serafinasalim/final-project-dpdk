#include <rte_common.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#define RX_RING_SIZE   512
#define TX_RING_SIZE   512
#define NUM_MBUFS      (8191*2)
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE     32
#define FLUSH_INTERVAL 1000

#pragma pack(push,1)
typedef struct {
    uint32_t seq;
    uint32_t spare;
    uint64_t send_ns; 
} packet_t;
#pragma pack(pop)

// Timestamp host (REALTIME, match sender)
static inline uint64_t nsec_now() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static inline const void* get_udp_payload(const struct rte_mbuf *mbuf, uint16_t *out_len) {
    const uint8_t *data = rte_pktmbuf_mtod(mbuf, const uint8_t*);
    uint16_t pkt_len = rte_pktmbuf_pkt_len(mbuf);
    if (pkt_len < sizeof(struct rte_ether_hdr)) return NULL;

    const struct rte_ether_hdr *eth = (const struct rte_ether_hdr*)data;
    uint16_t ether_type = rte_be_to_cpu_16(eth->ether_type);
    if (ether_type != RTE_ETHER_TYPE_IPV4) return NULL;

    const uint8_t *l3 = data + sizeof(struct rte_ether_hdr);
    if (l3 + sizeof(struct rte_ipv4_hdr) > data + pkt_len) return NULL;

    const struct rte_ipv4_hdr *ip4 = (const struct rte_ipv4_hdr*)l3;
    if (ip4->next_proto_id != IPPROTO_UDP) return NULL;

    uint16_t ihl_bytes = (ip4->version_ihl & 0x0F) * 4;
    const uint8_t *l4 = l3 + ihl_bytes;
    if (l4 + sizeof(struct rte_udp_hdr) > data + pkt_len) return NULL;

    const struct rte_udp_hdr *udp = (const struct rte_udp_hdr*)l4;
    (void)udp;

    const uint8_t *payload = l4 + sizeof(struct rte_udp_hdr);
    if (payload > data + pkt_len) return NULL;

    *out_len = (uint16_t)((data + pkt_len) - payload);
    return payload;
}

// ======== runtime parameter override =========
static int g_burst = BURST_SIZE;
static int g_rxring = RX_RING_SIZE;
static int g_cache = MBUF_CACHE_SIZE;
static char g_logname[256] = "recv_log_dpdk.csv";

static void parse_app_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (sscanf(argv[i], "--burst=%d", &g_burst) == 1) continue;
        if (sscanf(argv[i], "--rxring=%d", &g_rxring) == 1) continue;
        if (sscanf(argv[i], "--cache=%d", &g_cache) == 1) continue;
        if (strncmp(argv[i], "--log=", 6) == 0) {
            snprintf(g_logname, sizeof(g_logname), "%s", argv[i] + 6);
            continue;
        }
    }
}
// =============================================

int main(int argc, char *argv[]) {
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) rte_exit(EXIT_FAILURE, "EAL init failed\n");

    argc -= ret;
    argv += ret;
    parse_app_args(argc, argv);

    uint16_t portid = 0;

    struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create(
        "MBUF_POOL", NUM_MBUFS, g_cache, 0,
        RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (!mbuf_pool) rte_exit(EXIT_FAILURE, "mbuf pool create failed\n");

    struct rte_eth_conf port_conf = {
        .rxmode = { .mq_mode = RTE_ETH_MQ_RX_NONE, .offloads = 0 },
        .txmode = { .mq_mode = RTE_ETH_MQ_TX_NONE },
    };

    ret = rte_eth_dev_configure(portid, 1, 1, &port_conf);
    if (ret < 0) rte_exit(EXIT_FAILURE, "dev configure failed: %d\n", ret);

    ret = rte_eth_rx_queue_setup(portid, 0, g_rxring,
                                 rte_eth_dev_socket_id(portid),
                                 NULL, mbuf_pool);
    if (ret < 0) rte_exit(EXIT_FAILURE, "rx q setup failed\n");

    ret = rte_eth_tx_queue_setup(portid, 0, TX_RING_SIZE,
                                 rte_eth_dev_socket_id(portid),
                                 NULL);
    if (ret < 0) rte_exit(EXIT_FAILURE, "tx q setup failed\n");

    ret = rte_eth_dev_start(portid);
    if (ret < 0) rte_exit(EXIT_FAILURE, "dev start failed\n");

    printf("DPDK handler started on port %u\n", portid);
    printf("Config: BURST=%d RX_RING=%d CACHE=%d\n",
           g_burst, g_rxring, g_cache);
    printf("Logging to: %s\n", g_logname);

    FILE *logfile = fopen(g_logname, "w");
    if (!logfile) rte_exit(EXIT_FAILURE, "open log failed\n");
    fprintf(logfile, "recv_time_us,packet_len,seq,latency_us\n");
    setvbuf(logfile, NULL, _IOFBF, 1<<20);

    struct rte_mbuf *bufs[g_burst];
    uint64_t pkt_count = 0;

    while (1) {
        const uint16_t nb_rx = rte_eth_rx_burst(portid, 0, bufs, g_burst);
        if (nb_rx == 0) continue;

        for (uint16_t i = 0; i < nb_rx; i++) {
            struct rte_mbuf *mbuf = bufs[i];
            uint16_t wire_len = rte_pktmbuf_pkt_len(mbuf);

            uint64_t now_ns = nsec_now();
            uint64_t now_us = now_ns / 1000ULL;

            uint32_t seq = 0xffffffffu;
            double latency_us = -1.0;

            uint16_t payload_len = 0;
            const void* payload = get_udp_payload(mbuf, &payload_len);
            if (payload && payload_len >= sizeof(packet_t)) {
                packet_t p;
                memcpy(&p, payload, sizeof(packet_t));

                seq = p.seq;
                if (p.send_ns > 0) {
                    long long diff_ns = (long long)now_ns - (long long)p.send_ns;
                    if (diff_ns >= -100000)
                        latency_us = diff_ns / 1000.0;
                }
            }

            fprintf(logfile, "%llu,%u,%u,%.3f\n",
                    (unsigned long long)now_us, wire_len, seq, latency_us);

            rte_pktmbuf_free(mbuf);
            if (++pkt_count % FLUSH_INTERVAL == 0) fflush(logfile);
        }
    }

    fclose(logfile);
    rte_eth_dev_stop(portid);
    rte_eth_dev_close(portid);
    rte_eal_cleanup();
    return 0;
}
