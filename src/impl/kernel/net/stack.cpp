#include "../../../intf/net.h"
#include "../../../intf/kernel.h"
#include "../../../intf/timer.h"
#include "../../../intf/task.h"
#include "../../../intf/virtual.h"

// ---------------------------------------------------------------------------
// ARP cache (4-entry circular buffer)
// ---------------------------------------------------------------------------
static constexpr int ARP_CACHE_SIZE = 4;
static ArpEntry arp_cache[ARP_CACHE_SIZE] = {};
static int      arp_cache_next = 0;

static void arp_cache_update(pt::uint32_t ip, const pt::uint8_t mac[6]) {
    // Check if already cached; update MAC if so
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].ip == ip) {
            for (int j = 0; j < 6; j++) arp_cache[i].mac[j] = mac[j];
            return;
        }
    }
    // Add new entry (overwrite oldest)
    arp_cache[arp_cache_next].ip = ip;
    for (int j = 0; j < 6; j++) arp_cache[arp_cache_next].mac[j] = mac[j];
    arp_cache_next = (arp_cache_next + 1) % ARP_CACHE_SIZE;
}

static bool arp_lookup(pt::uint32_t ip, pt::uint8_t out_mac[6]) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].ip == ip) {
            for (int j = 0; j < 6; j++) out_mac[j] = arp_cache[i].mac[j];
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Internet checksum (RFC 1071) — one's complement sum of 16-bit words
// ---------------------------------------------------------------------------
pt::uint16_t inet_checksum(const void* data, int len) {
    const pt::uint16_t* ptr = static_cast<const pt::uint16_t*>(data);
    pt::uint32_t sum = 0;
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len == 1)
        sum += *reinterpret_cast<const pt::uint8_t*>(ptr);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return ~static_cast<pt::uint16_t>(sum);
}

// ---------------------------------------------------------------------------
// ICMP state — last received echo reply
// ---------------------------------------------------------------------------
static volatile pt::uint16_t g_last_reply_seq  = 0;
static volatile pt::uint64_t g_last_reply_tick  = 0;

pt::uint64_t icmp_last_reply_tick() { return g_last_reply_tick; }

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void arp_send_request(pt::uint32_t target_ip);
static void ipv4_send(pt::uint32_t dst_ip, pt::uint8_t proto,
                      const pt::uint8_t* payload, pt::uint16_t payload_len);

// ---------------------------------------------------------------------------
// ARP send request
// ---------------------------------------------------------------------------
static void arp_send_request(pt::uint32_t target_ip) {
    if (!RTL8139::is_present()) return;

    static pt::uint8_t pkt[sizeof(EthHdr) + sizeof(ArpPkt)];
    pt::uint8_t my_mac[6];
    RTL8139::get_mac(my_mac);

    EthHdr* eth = reinterpret_cast<EthHdr*>(pkt);
    ArpPkt* arp = reinterpret_cast<ArpPkt*>(pkt + sizeof(EthHdr));

    // Broadcast Ethernet
    for (int i = 0; i < 6; i++) eth->dst[i] = 0xFF;
    for (int i = 0; i < 6; i++) eth->src[i] = my_mac[i];
    eth->ethertype = bswap16(0x0806);

    arp->htype = bswap16(1);       // Ethernet
    arp->ptype = bswap16(0x0800);  // IPv4
    arp->hlen  = 6;
    arp->plen  = 4;
    arp->oper  = bswap16(1);       // Request

    for (int i = 0; i < 6; i++) arp->sha[i] = my_mac[i];
    arp->spa = NET_MY_IP;
    for (int i = 0; i < 6; i++) arp->tha[i] = 0x00;
    arp->tpa = target_ip;

    RTL8139::send(pkt, sizeof(pkt));
}

// ---------------------------------------------------------------------------
// ARP reply (in response to a request for our IP)
// ---------------------------------------------------------------------------
static void arp_send_reply(const pt::uint8_t req_src_mac[6],
                            pt::uint32_t req_src_ip) {
    if (!RTL8139::is_present()) return;

    static pt::uint8_t pkt[sizeof(EthHdr) + sizeof(ArpPkt)];
    pt::uint8_t my_mac[6];
    RTL8139::get_mac(my_mac);

    EthHdr* eth = reinterpret_cast<EthHdr*>(pkt);
    ArpPkt* arp = reinterpret_cast<ArpPkt*>(pkt + sizeof(EthHdr));

    for (int i = 0; i < 6; i++) eth->dst[i] = req_src_mac[i];
    for (int i = 0; i < 6; i++) eth->src[i] = my_mac[i];
    eth->ethertype = bswap16(0x0806);

    arp->htype = bswap16(1);
    arp->ptype = bswap16(0x0800);
    arp->hlen  = 6;
    arp->plen  = 4;
    arp->oper  = bswap16(2);       // Reply

    for (int i = 0; i < 6; i++) arp->sha[i] = my_mac[i];
    arp->spa = NET_MY_IP;
    for (int i = 0; i < 6; i++) arp->tha[i] = req_src_mac[i];
    arp->tpa = req_src_ip;

    RTL8139::send(pkt, sizeof(pkt));
}

// ---------------------------------------------------------------------------
// ARP handler
// ---------------------------------------------------------------------------
static void arp_handle(const pt::uint8_t* data, pt::uint32_t len) {
    if (len < sizeof(ArpPkt)) return;
    const ArpPkt* arp = reinterpret_cast<const ArpPkt*>(data);

    // Update ARP cache from sender info
    arp_cache_update(arp->spa, arp->sha);

    pt::uint16_t oper = bswap16(arp->oper);
    if (oper == 1 && arp->tpa == NET_MY_IP) {
        // ARP request for our IP: send reply
        arp_send_reply(arp->sha, arp->spa);
    }
}

// ---------------------------------------------------------------------------
// IPv4 send
// ---------------------------------------------------------------------------
static void ipv4_send(pt::uint32_t dst_ip, pt::uint8_t proto,
                      const pt::uint8_t* payload, pt::uint16_t payload_len) {
    if (!RTL8139::is_present()) return;

    // Resolve destination MAC (use gateway if not on same subnet)
    pt::uint32_t next_hop = dst_ip;
    if ((dst_ip & NET_BROADCAST) != (NET_MY_IP & NET_BROADCAST))
        next_hop = NET_GATEWAY_IP;

    pt::uint8_t dst_mac[6];
    if (!arp_lookup(next_hop, dst_mac)) {
        // Send ARP request and busy-wait up to ~500 ms (500 × 1 ms ≈ 50 ticks at 50 Hz)
        arp_send_request(next_hop);
        pt::uint64_t deadline = get_ticks() + 25;  // ~500 ms at 50 Hz
        while (get_ticks() < deadline) {
            if (arp_lookup(next_hop, dst_mac)) break;
            TaskScheduler::task_yield();
        }
        if (!arp_lookup(next_hop, dst_mac)) {
            klog("[NET] ARP timeout for next hop\n");
            return;
        }
    }

    // Build Ethernet + IPv4 packet in a local buffer
    static pt::uint8_t pkt[sizeof(EthHdr) + sizeof(IPv4Hdr) + 1480];
    pt::uint8_t my_mac[6];
    RTL8139::get_mac(my_mac);

    if (payload_len > 1480) {
        klog("[NET] ipv4_send: payload too large (%d)\n", (int)payload_len);
        return;
    }

    EthHdr*  eth = reinterpret_cast<EthHdr*>(pkt);
    IPv4Hdr* ip  = reinterpret_cast<IPv4Hdr*>(pkt + sizeof(EthHdr));
    pt::uint8_t* data_out = pkt + sizeof(EthHdr) + sizeof(IPv4Hdr);

    // Ethernet header
    for (int i = 0; i < 6; i++) eth->dst[i] = dst_mac[i];
    for (int i = 0; i < 6; i++) eth->src[i] = my_mac[i];
    eth->ethertype = bswap16(0x0800);

    // IPv4 header
    static pt::uint16_t ip_id = 0;
    pt::uint16_t total = (pt::uint16_t)(sizeof(IPv4Hdr) + payload_len);
    ip->ver_ihl    = 0x45;  // version 4, IHL = 5 (20 bytes, no options)
    ip->dscp_ecn   = 0;
    ip->total_len  = bswap16(total);
    ip->id         = bswap16(ip_id++);
    ip->flags_frag = 0;
    ip->ttl        = 64;
    ip->proto      = proto;
    ip->checksum   = 0;
    ip->src_ip     = NET_MY_IP;
    ip->dst_ip     = dst_ip;
    ip->checksum   = inet_checksum(ip, sizeof(IPv4Hdr));

    // Payload
    for (pt::uint16_t i = 0; i < payload_len; i++) data_out[i] = payload[i];

    RTL8139::send(pkt, (pt::uint32_t)(sizeof(EthHdr) + total));
}

// ---------------------------------------------------------------------------
// ICMP echo reply (response to echo request)
// ---------------------------------------------------------------------------
static void icmp_send_reply(pt::uint32_t dst_ip, pt::uint16_t id,
                             pt::uint16_t seq,
                             const pt::uint8_t* echo_data, pt::uint16_t echo_len) {
    static pt::uint8_t buf[sizeof(IcmpHdr) + 56];
    pt::uint16_t icmp_payload_len = (pt::uint16_t)(sizeof(IcmpHdr) + echo_len);
    if (icmp_payload_len > (pt::uint16_t)sizeof(buf)) return;

    IcmpHdr* icmp = reinterpret_cast<IcmpHdr*>(buf);
    pt::uint8_t* data_out = buf + sizeof(IcmpHdr);

    icmp->type     = 0;  // echo reply
    icmp->code     = 0;
    icmp->checksum = 0;
    icmp->id       = id;
    icmp->seq      = seq;
    for (pt::uint16_t i = 0; i < echo_len && i < 56; i++) data_out[i] = echo_data[i];
    icmp->checksum = inet_checksum(buf, icmp_payload_len);

    ipv4_send(dst_ip, 1, buf, icmp_payload_len);
}

// ---------------------------------------------------------------------------
// ICMP handler
// ---------------------------------------------------------------------------
static void icmp_handle(pt::uint32_t src_ip,
                        const pt::uint8_t* data, pt::uint32_t len) {
    if (len < sizeof(IcmpHdr)) return;
    const IcmpHdr* icmp = reinterpret_cast<const IcmpHdr*>(data);

    if (icmp->type == 8) {
        // Echo request → send reply
        const pt::uint8_t* echo_data = data + sizeof(IcmpHdr);
        pt::uint16_t echo_len = (pt::uint16_t)(len - sizeof(IcmpHdr));
        icmp_send_reply(src_ip, icmp->id, icmp->seq, echo_data, echo_len);
    } else if (icmp->type == 0) {
        // Echo reply → record for icmp_wait_reply()
        g_last_reply_seq  = bswap16(icmp->seq);
        g_last_reply_tick = get_ticks();
    }
}

// ---------------------------------------------------------------------------
// IPv4 handler
// ---------------------------------------------------------------------------
static void ipv4_handle(const pt::uint8_t* eth_src_mac,
                        const pt::uint8_t* data, pt::uint32_t len) {
    if (len < sizeof(IPv4Hdr)) return;
    const IPv4Hdr* ip = reinterpret_cast<const IPv4Hdr*>(data);

    // Verify it's a plain IPv4 packet (no options)
    if ((ip->ver_ihl >> 4) != 4) return;
    pt::uint8_t ihl = (ip->ver_ihl & 0x0F) * 4;
    if (ihl < 20 || ihl > len) return;

    // Update ARP cache from source
    arp_cache_update(ip->src_ip, eth_src_mac);

    pt::uint32_t src_ip = ip->src_ip;
    const pt::uint8_t* payload = data + ihl;
    pt::uint32_t payload_len   = bswap16(ip->total_len);
    if (payload_len < ihl) return;
    payload_len -= ihl;

    switch (ip->proto) {
        case 1:  // ICMP
            icmp_handle(src_ip, payload, payload_len);
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// net_receive — entry point from RTL8139 ISR
// ---------------------------------------------------------------------------
void net_receive(pt::uint8_t* data, pt::uint32_t len) {
    if (len < sizeof(EthHdr)) return;

    EthHdr* eth = reinterpret_cast<EthHdr*>(data);
    pt::uint16_t etype = bswap16(eth->ethertype);

    const pt::uint8_t* payload = data + sizeof(EthHdr);
    pt::uint32_t payload_len   = len - (pt::uint32_t)sizeof(EthHdr);

    switch (etype) {
        case 0x0806:
            arp_handle(payload, payload_len);
            break;
        case 0x0800:
            ipv4_handle(eth->src, payload, payload_len);
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// icmp_ping — send an ICMP echo request
// ---------------------------------------------------------------------------
bool icmp_ping(pt::uint32_t dst_ip, pt::uint16_t seq) {
    if (!RTL8139::is_present()) return false;

    static pt::uint8_t buf[sizeof(IcmpHdr) + 56];
    IcmpHdr* icmp = reinterpret_cast<IcmpHdr*>(buf);
    pt::uint8_t* payload = buf + sizeof(IcmpHdr);

    // Fill payload with a simple pattern
    for (int i = 0; i < 56; i++) payload[i] = (pt::uint8_t)i;

    icmp->type     = 8;  // echo request
    icmp->code     = 0;
    icmp->checksum = 0;
    icmp->id       = bswap16(0x1234);
    icmp->seq      = bswap16(seq);
    icmp->checksum = inet_checksum(buf, sizeof(buf));

    ipv4_send(dst_ip, 1, buf, (pt::uint16_t)sizeof(buf));
    return true;
}

// ---------------------------------------------------------------------------
// icmp_wait_reply — spin-yield until echo reply arrives or timeout
// ---------------------------------------------------------------------------
bool icmp_wait_reply(pt::uint16_t seq, pt::uint64_t timeout_ticks) {
    // Clear any stale reply state so a leftover seq value doesn't fire immediately
    g_last_reply_seq = (pt::uint16_t)(~seq);

    pt::uint64_t deadline = get_ticks() + timeout_ticks;
    while (get_ticks() < deadline) {
        if (g_last_reply_seq == seq) return true;
        TaskScheduler::task_yield();
    }
    return false;
}
