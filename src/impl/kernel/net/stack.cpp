#include "../../../intf/net.h"
#include "../../../intf/kernel.h"
#include "../../../intf/timer.h"
#include "../../../intf/task.h"
#include "../../../intf/virtual.h"

// ---------------------------------------------------------------------------
// Mutable network configuration (updated by DHCP)
// ---------------------------------------------------------------------------
pt::uint32_t g_my_ip      = make_ip(10, 0, 2, 15); // default fallback
pt::uint32_t g_gateway_ip = make_ip(10, 0, 2,  2);
pt::uint32_t g_dns_ip     = make_ip(10, 0, 2,  3); // QEMU SLIRP DNS

// ---------------------------------------------------------------------------
// ARP cache (4-entry circular buffer)
// ---------------------------------------------------------------------------
static constexpr int ARP_CACHE_SIZE = 4;
static ArpEntry arp_cache[ARP_CACHE_SIZE] = {};
static int      arp_cache_next = 0;

static void arp_cache_update(pt::uint32_t ip, const pt::uint8_t mac[6]) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].ip == ip) {
            for (int j = 0; j < 6; j++) arp_cache[i].mac[j] = mac[j];
            return;
        }
    }
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
static volatile pt::uint16_t g_last_reply_seq       = 0;
static volatile pt::uint64_t g_last_reply_tick       = 0;
static volatile pt::uint16_t g_last_unreachable_seq  = 0;

pt::uint64_t icmp_last_reply_tick()    { return g_last_reply_tick; }
pt::uint16_t icmp_last_unreachable_seq() { return g_last_unreachable_seq; }

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void arp_send_request(pt::uint32_t target_ip);
static void ipv4_send_from(pt::uint32_t src_ip, pt::uint32_t dst_ip,
                            pt::uint8_t proto,
                            const pt::uint8_t* payload, pt::uint16_t payload_len);
static void ipv4_send(pt::uint32_t dst_ip, pt::uint8_t proto,
                      const pt::uint8_t* payload, pt::uint16_t payload_len);
static void dhcp_handle(const pt::uint8_t* data, pt::uint32_t len);
static void dns_handle(pt::uint32_t src_ip,
                       const pt::uint8_t* data, pt::uint32_t len);
void tcp_handle(pt::uint32_t src_ip, const pt::uint8_t* data, pt::uint32_t len);

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

    for (int i = 0; i < 6; i++) eth->dst[i] = 0xFF;
    for (int i = 0; i < 6; i++) eth->src[i] = my_mac[i];
    eth->ethertype = bswap16(0x0806);

    arp->htype = bswap16(1);
    arp->ptype = bswap16(0x0800);
    arp->hlen  = 6;
    arp->plen  = 4;
    arp->oper  = bswap16(1);  // Request

    for (int i = 0; i < 6; i++) arp->sha[i] = my_mac[i];
    arp->spa = g_my_ip;
    for (int i = 0; i < 6; i++) arp->tha[i] = 0x00;
    arp->tpa = target_ip;

    RTL8139::send(pkt, sizeof(pkt));
}

// ---------------------------------------------------------------------------
// ARP reply
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
    arp->oper  = bswap16(2);  // Reply

    for (int i = 0; i < 6; i++) arp->sha[i] = my_mac[i];
    arp->spa = g_my_ip;
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

    arp_cache_update(arp->spa, arp->sha);

    pt::uint16_t oper = bswap16(arp->oper);
    if (oper == 1 && arp->tpa == g_my_ip) {
        arp_send_reply(arp->sha, arp->spa);
    }
}

// ---------------------------------------------------------------------------
// IPv4 send (core — sends from src_ip, handles broadcast MAC)
// ---------------------------------------------------------------------------
static void ipv4_send_from(pt::uint32_t src_ip, pt::uint32_t dst_ip,
                            pt::uint8_t proto,
                            const pt::uint8_t* payload, pt::uint16_t payload_len) {
    if (!RTL8139::is_present()) return;

    pt::uint8_t dst_mac[6];

    if (dst_ip == 0xFFFFFFFFu) {
        // Broadcast — use FF:FF:FF:FF:FF:FF directly (no ARP)
        for (int i = 0; i < 6; i++) dst_mac[i] = 0xFF;
    } else {
        // Resolve destination MAC; use gateway for out-of-subnet hosts
        // Subnet mask for 10.0.2.0/24 in make_ip layout = 0x00FFFFFF
        static constexpr pt::uint32_t SUBNET_MASK = make_ip(255, 255, 255, 0);
        pt::uint32_t next_hop = dst_ip;
        if ((dst_ip & SUBNET_MASK) != (g_my_ip & SUBNET_MASK))
            next_hop = g_gateway_ip;

        if (!arp_lookup(next_hop, dst_mac)) {
            klog("[NET] ARP miss for %x, sending request\n", next_hop);
            arp_send_request(next_hop);
            pt::uint64_t deadline = get_ticks() + 25; // ~500 ms at 50 Hz
            while (get_ticks() < deadline) {
                if (arp_lookup(next_hop, dst_mac)) break;
                TaskScheduler::task_yield();
            }
            if (!arp_lookup(next_hop, dst_mac)) {
                klog("[NET] ARP timeout for next hop %x\n", next_hop);
                return;
            }
        }
    }

    static pt::uint8_t pkt[sizeof(EthHdr) + sizeof(IPv4Hdr) + 1480];
    pt::uint8_t my_mac[6];
    RTL8139::get_mac(my_mac);

    if (payload_len > 1480) {
        klog("[NET] ipv4_send: payload too large (%d)\n", (int)payload_len);
        return;
    }

    EthHdr*  eth      = reinterpret_cast<EthHdr*>(pkt);
    IPv4Hdr* ip       = reinterpret_cast<IPv4Hdr*>(pkt + sizeof(EthHdr));
    pt::uint8_t* data_out = pkt + sizeof(EthHdr) + sizeof(IPv4Hdr);

    for (int i = 0; i < 6; i++) eth->dst[i] = dst_mac[i];
    for (int i = 0; i < 6; i++) eth->src[i] = my_mac[i];
    eth->ethertype = bswap16(0x0800);

    static pt::uint16_t ip_id = 0;
    pt::uint16_t total = (pt::uint16_t)(sizeof(IPv4Hdr) + payload_len);
    ip->ver_ihl    = 0x45;
    ip->dscp_ecn   = 0;
    ip->total_len  = bswap16(total);
    ip->id         = bswap16(ip_id++);
    ip->flags_frag = 0;
    ip->ttl        = 64;
    ip->proto      = proto;
    ip->checksum   = 0;
    ip->src_ip     = src_ip;
    ip->dst_ip     = dst_ip;
    ip->checksum   = inet_checksum(ip, sizeof(IPv4Hdr));

    for (pt::uint16_t i = 0; i < payload_len; i++) data_out[i] = payload[i];

    RTL8139::send(pkt, (pt::uint32_t)(sizeof(EthHdr) + total));
}

// Convenience wrapper: send from our IP
static void ipv4_send(pt::uint32_t dst_ip, pt::uint8_t proto,
                      const pt::uint8_t* payload, pt::uint16_t payload_len) {
    ipv4_send_from(g_my_ip, dst_ip, proto, payload, payload_len);
}

// ---------------------------------------------------------------------------
// UDP send (from g_my_ip)
// ---------------------------------------------------------------------------
static void udp_send(pt::uint16_t src_port, pt::uint32_t dst_ip,
                     pt::uint16_t dst_port,
                     const pt::uint8_t* payload, pt::uint16_t payload_len) {
    static pt::uint8_t buf[sizeof(UdpHdr) + 512];
    if (payload_len > 512) return;

    UdpHdr* udp = reinterpret_cast<UdpHdr*>(buf);
    udp->src_port = bswap16(src_port);
    udp->dst_port = bswap16(dst_port);
    udp->length   = bswap16((pt::uint16_t)(sizeof(UdpHdr) + payload_len));
    udp->checksum = 0;

    for (pt::uint16_t i = 0; i < payload_len; i++)
        buf[sizeof(UdpHdr) + i] = payload[i];

    ipv4_send_from(g_my_ip, dst_ip, 17, buf,
                   (pt::uint16_t)(sizeof(UdpHdr) + payload_len));
}

// ---------------------------------------------------------------------------
// ICMP echo reply
// ---------------------------------------------------------------------------
static void icmp_send_reply(pt::uint32_t dst_ip, pt::uint16_t id,
                             pt::uint16_t seq,
                             const pt::uint8_t* echo_data, pt::uint16_t echo_len) {
    static pt::uint8_t buf[sizeof(IcmpHdr) + 56];
    pt::uint16_t icmp_payload_len = (pt::uint16_t)(sizeof(IcmpHdr) + echo_len);
    if (icmp_payload_len > (pt::uint16_t)sizeof(buf)) return;

    IcmpHdr* icmp = reinterpret_cast<IcmpHdr*>(buf);
    pt::uint8_t* data_out = buf + sizeof(IcmpHdr);

    icmp->type     = 0;
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
        const pt::uint8_t* echo_data = data + sizeof(IcmpHdr);
        pt::uint16_t echo_len = (pt::uint16_t)(len - sizeof(IcmpHdr));
        icmp_send_reply(src_ip, icmp->id, icmp->seq, echo_data, echo_len);
    } else if (icmp->type == 0) {
        pt::uint16_t s = bswap16(icmp->seq);
        klog("[ICMP] echo reply from %x seq=%d\n", src_ip, (int)s);
        g_last_reply_seq  = s;
        g_last_reply_tick = get_ticks();
    } else if (icmp->type == 3) {
        // Destination Unreachable — extract original seq from embedded ICMP header.
        // Type-3 body: IcmpHdr(8) + original IP header(20) + first 8B of original payload.
        // Original ICMP seq is at byte offset 6-7 inside that 8-byte original ICMP header.
        if (len >= sizeof(IcmpHdr) + 20 + 8) {
            const pt::uint8_t* orig = data + sizeof(IcmpHdr) + 20;
            pt::uint16_t orig_seq = (pt::uint16_t)((orig[6] << 8) | orig[7]);
            klog("[ICMP] unreachable (code=%d) from %x seq=%d\n",
                 (int)icmp->code, src_ip, (int)orig_seq);
            g_last_unreachable_seq = orig_seq;
        } else {
            klog("[ICMP] unreachable (code=%d) from %x (no seq)\n",
                 (int)icmp->code, src_ip);
        }
    } else {
        klog("[ICMP] type=%d from %x (ignored)\n", (int)icmp->type, src_ip);
    }
}

// ---------------------------------------------------------------------------
// UDP handler — dispatch on dst port
// ---------------------------------------------------------------------------
static void udp_handle(pt::uint32_t src_ip,
                       const pt::uint8_t* data, pt::uint32_t len) {
    if (len < sizeof(UdpHdr)) return;
    const UdpHdr* udp = reinterpret_cast<const UdpHdr*>(data);

    const pt::uint8_t* payload     = data + sizeof(UdpHdr);
    pt::uint16_t       udp_len     = bswap16(udp->length);
    pt::uint16_t       payload_len = (udp_len >= (pt::uint16_t)sizeof(UdpHdr))
                                     ? (pt::uint16_t)(udp_len - sizeof(UdpHdr))
                                     : 0;
    if (payload_len > len - sizeof(UdpHdr))
        payload_len = (pt::uint16_t)(len - sizeof(UdpHdr));

    switch (bswap16(udp->dst_port)) {
        case 68:   dhcp_handle(payload, payload_len);         break;
        case 1024: dns_handle(src_ip, payload, payload_len);  break;
        default:   break;
    }
}

// ---------------------------------------------------------------------------
// IPv4 handler
// ---------------------------------------------------------------------------
static void ipv4_handle(const pt::uint8_t* eth_src_mac,
                        const pt::uint8_t* data, pt::uint32_t len) {
    if (len < sizeof(IPv4Hdr)) return;
    const IPv4Hdr* ip = reinterpret_cast<const IPv4Hdr*>(data);

    if ((ip->ver_ihl >> 4) != 4) return;
    pt::uint8_t ihl = (ip->ver_ihl & 0x0F) * 4;
    if (ihl < 20 || ihl > len) return;

    arp_cache_update(ip->src_ip, eth_src_mac);

    pt::uint32_t src_ip = ip->src_ip;
    const pt::uint8_t* payload = data + ihl;
    pt::uint32_t payload_len   = bswap16(ip->total_len);
    if (payload_len < ihl) return;
    payload_len -= ihl;

    switch (ip->proto) {
        case 1:  icmp_handle(src_ip, payload, payload_len); break;
        case 6:  tcp_handle(src_ip, payload, payload_len);  break;
        case 17: udp_handle(src_ip, payload, payload_len);  break;
        default: break;
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
        case 0x0806: arp_handle(payload, payload_len);              break;
        case 0x0800: ipv4_handle(eth->src, payload, payload_len);  break;
        default:     break;
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

    for (int i = 0; i < 56; i++) payload[i] = (pt::uint8_t)i;

    icmp->type     = 8;
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
    g_last_reply_seq      = (pt::uint16_t)(~seq); // sentinel: not-yet-received
    g_last_unreachable_seq = (pt::uint16_t)(~seq);

    pt::uint64_t deadline = get_ticks() + timeout_ticks;
    while (get_ticks() < deadline) {
        if (g_last_reply_seq      == seq) return true;
        if (g_last_unreachable_seq == seq) return false; // bail early, no point waiting
        TaskScheduler::task_yield();
    }
    return false;
}

// ===========================================================================
// DHCP client
// ===========================================================================

// DHCP magic cookie: bytes 0x63 0x82 0x53 0x63 in network order.
// make_ip(99,130,83,99): stored as [63][82][53][63] on little-endian x86.
static constexpr pt::uint32_t DHCP_XID   = 0x3903F326u;
static constexpr pt::uint32_t DHCP_MAGIC = make_ip(99, 130, 83, 99);

// State: 0=idle, 2=have OFFER, 4=have ACK
static volatile pt::uint8_t  g_dhcp_state      = 0;
static volatile pt::uint32_t g_dhcp_offered_ip = 0;
static volatile pt::uint32_t g_dhcp_server_ip  = 0;

static void dhcp_send_discover() {
    pt::uint8_t buf[sizeof(UdpHdr) + sizeof(DhcpHdr) + 16];
    for (pt::uint32_t i = 0; i < sizeof(buf); i++) buf[i] = 0;

    UdpHdr*  udp  = reinterpret_cast<UdpHdr*>(buf);
    DhcpHdr* dhcp = reinterpret_cast<DhcpHdr*>(buf + sizeof(UdpHdr));

    pt::uint8_t my_mac[6];
    RTL8139::get_mac(my_mac);

    dhcp->op    = 1;          // BOOTREQUEST
    dhcp->htype = 1;          // Ethernet
    dhcp->hlen  = 6;
    dhcp->hops  = 0;
    dhcp->xid   = DHCP_XID;
    dhcp->secs  = 0;
    dhcp->flags = bswap16(0x8000); // broadcast
    dhcp->ciaddr = 0;
    dhcp->yiaddr = 0;
    dhcp->siaddr = 0;
    dhcp->giaddr = 0;
    for (int i = 0; i < 6; i++) dhcp->chaddr[i] = my_mac[i];
    dhcp->magic = DHCP_MAGIC;

    // Options
    pt::uint8_t* opts = reinterpret_cast<pt::uint8_t*>(dhcp + 1);
    int oi = 0;
    opts[oi++] = 53; opts[oi++] = 1; opts[oi++] = 1;        // DHCP DISCOVER
    opts[oi++] = 55; opts[oi++] = 3;                          // param request list
    opts[oi++] = 1; opts[oi++] = 3; opts[oi++] = 6;          // subnet, router, DNS
    opts[oi++] = 0xFF;                                         // end

    pt::uint16_t dhcp_len = (pt::uint16_t)(sizeof(DhcpHdr) + oi);
    pt::uint16_t udp_total = (pt::uint16_t)(sizeof(UdpHdr) + dhcp_len);

    udp->src_port = bswap16(68);
    udp->dst_port = bswap16(67);
    udp->length   = bswap16(udp_total);
    udp->checksum = 0;

    // Send from 0.0.0.0 to 255.255.255.255 (per RFC 2131)
    ipv4_send_from(0, 0xFFFFFFFFu, 17, buf, udp_total);
}

static void dhcp_send_request(pt::uint32_t offered_ip, pt::uint32_t server_ip) {
    pt::uint8_t buf[sizeof(UdpHdr) + sizeof(DhcpHdr) + 20];
    for (pt::uint32_t i = 0; i < sizeof(buf); i++) buf[i] = 0;

    UdpHdr*  udp  = reinterpret_cast<UdpHdr*>(buf);
    DhcpHdr* dhcp = reinterpret_cast<DhcpHdr*>(buf + sizeof(UdpHdr));

    pt::uint8_t my_mac[6];
    RTL8139::get_mac(my_mac);

    dhcp->op    = 1;
    dhcp->htype = 1;
    dhcp->hlen  = 6;
    dhcp->hops  = 0;
    dhcp->xid   = DHCP_XID;
    dhcp->secs  = 0;
    dhcp->flags = bswap16(0x8000);
    dhcp->ciaddr = 0;
    dhcp->yiaddr = 0;
    dhcp->siaddr = 0;
    dhcp->giaddr = 0;
    for (int i = 0; i < 6; i++) dhcp->chaddr[i] = my_mac[i];
    dhcp->magic = DHCP_MAGIC;

    const pt::uint8_t* oip = reinterpret_cast<const pt::uint8_t*>(&offered_ip);
    const pt::uint8_t* sip = reinterpret_cast<const pt::uint8_t*>(&server_ip);

    pt::uint8_t* opts = reinterpret_cast<pt::uint8_t*>(dhcp + 1);
    int oi = 0;
    opts[oi++] = 53; opts[oi++] = 1; opts[oi++] = 3;          // DHCP REQUEST
    opts[oi++] = 50; opts[oi++] = 4;                            // requested IP
    opts[oi++] = oip[0]; opts[oi++] = oip[1];
    opts[oi++] = oip[2]; opts[oi++] = oip[3];
    opts[oi++] = 54; opts[oi++] = 4;                            // server identifier
    opts[oi++] = sip[0]; opts[oi++] = sip[1];
    opts[oi++] = sip[2]; opts[oi++] = sip[3];
    opts[oi++] = 0xFF;                                           // end

    pt::uint16_t dhcp_len = (pt::uint16_t)(sizeof(DhcpHdr) + oi);
    pt::uint16_t udp_total = (pt::uint16_t)(sizeof(UdpHdr) + dhcp_len);

    udp->src_port = bswap16(68);
    udp->dst_port = bswap16(67);
    udp->length   = bswap16(udp_total);
    udp->checksum = 0;

    ipv4_send_from(0, 0xFFFFFFFFu, 17, buf, udp_total);
}

// Walk DHCP options, call cb(type, data, len) for each. Returns false on parse error.
static void dhcp_handle(const pt::uint8_t* data, pt::uint32_t len) {
    if (len < sizeof(DhcpHdr)) return;
    const DhcpHdr* dhcp = reinterpret_cast<const DhcpHdr*>(data);

    if (dhcp->xid != DHCP_XID) return;
    if (dhcp->op != 2)         return; // must be BOOTREPLY

    // Walk options after the fixed header
    const pt::uint8_t* opts = data + sizeof(DhcpHdr);
    pt::uint32_t opts_len   = (len > sizeof(DhcpHdr)) ? (len - sizeof(DhcpHdr)) : 0;

    pt::uint8_t  msg_type   = 0;
    pt::uint32_t offered_ip = dhcp->yiaddr;
    pt::uint32_t server_ip  = 0;
    pt::uint32_t router_ip  = 0;
    pt::uint32_t dns_ip     = 0;

    for (pt::uint32_t i = 0; i < opts_len; ) {
        pt::uint8_t type = opts[i++];
        if (type == 0)   continue; // pad
        if (type == 255) break;    // end
        if (i >= opts_len) break;
        pt::uint8_t opt_len = opts[i++];
        if (i + opt_len > opts_len) break;

        switch (type) {
            case 53: // DHCP message type
                if (opt_len >= 1) msg_type = opts[i];
                break;
            case 54: // server identifier
                if (opt_len >= 4) {
                    server_ip = ((pt::uint32_t)opts[i+3] << 24) |
                                ((pt::uint32_t)opts[i+2] << 16) |
                                ((pt::uint32_t)opts[i+1] <<  8) |
                                 (pt::uint32_t)opts[i+0];
                }
                break;
            case 3: // router
                if (opt_len >= 4) {
                    router_ip = ((pt::uint32_t)opts[i+3] << 24) |
                                ((pt::uint32_t)opts[i+2] << 16) |
                                ((pt::uint32_t)opts[i+1] <<  8) |
                                 (pt::uint32_t)opts[i+0];
                }
                break;
            case 6: // DNS server
                if (opt_len >= 4) {
                    dns_ip = ((pt::uint32_t)opts[i+3] << 24) |
                             ((pt::uint32_t)opts[i+2] << 16) |
                             ((pt::uint32_t)opts[i+1] <<  8) |
                              (pt::uint32_t)opts[i+0];
                }
                break;
            default: break;
        }
        i += opt_len;
    }

    if (msg_type == 2) {
        // DHCP OFFER
        g_dhcp_offered_ip = offered_ip;
        g_dhcp_server_ip  = server_ip;
        g_dhcp_state      = 2;
        klog("[DHCP] OFFER received\n");
    } else if (msg_type == 5) {
        // DHCP ACK
        g_dhcp_offered_ip = offered_ip;
        if (router_ip) g_gateway_ip = router_ip;
        if (dns_ip)    g_dns_ip     = dns_ip;
        g_dhcp_state = 4;
        klog("[DHCP] ACK received\n");
    }
}

bool dhcp_acquire(pt::uint64_t timeout_ticks) {
    if (!RTL8139::is_present()) return false;

    g_dhcp_state      = 0;
    g_dhcp_offered_ip = 0;
    g_dhcp_server_ip  = 0;

    dhcp_send_discover();

    // Wait for OFFER
    pt::uint64_t deadline = get_ticks() + timeout_ticks;
    while (get_ticks() < deadline && g_dhcp_state < 2)
        TaskScheduler::task_yield();

    if (g_dhcp_state < 2) {
        klog("[DHCP] No OFFER received\n");
        return false;
    }

    dhcp_send_request(g_dhcp_offered_ip, g_dhcp_server_ip);

    // Wait for ACK
    deadline = get_ticks() + timeout_ticks;
    while (get_ticks() < deadline && g_dhcp_state < 4)
        TaskScheduler::task_yield();

    if (g_dhcp_state < 4) {
        klog("[DHCP] No ACK received\n");
        return false;
    }

    g_my_ip = g_dhcp_offered_ip;
    klog("[DHCP] IP assigned\n");
    return true;
}

// ===========================================================================
// DNS resolver (A record only)
// ===========================================================================

static volatile pt::uint32_t g_dns_reply_ip    = 0;
static volatile bool         g_dns_reply_got    = false;
static pt::uint16_t          g_dns_txid         = 1;
static pt::uint16_t          g_dns_pending_txid = 0;

// Encode "example.com" → \x07example\x03com\x00, return bytes written
static int dns_encode_name(pt::uint8_t* buf, const char* hostname) {
    int out = 0;
    while (*hostname) {
        // Find next label
        const char* dot = hostname;
        while (*dot && *dot != '.') dot++;
        int label_len = (int)(dot - hostname);
        buf[out++] = (pt::uint8_t)label_len;
        for (int i = 0; i < label_len; i++) buf[out++] = (pt::uint8_t)hostname[i];
        hostname = dot;
        if (*hostname == '.') hostname++;
    }
    buf[out++] = 0; // root label
    return out;
}

// Skip a DNS name in a packet (handles label compression pointers)
static int dns_skip_name(const pt::uint8_t* data, pt::uint32_t len, int offset) {
    while (offset < (int)len) {
        pt::uint8_t b = data[offset];
        if (b == 0) { offset++; break; }
        if ((b & 0xC0) == 0xC0) { offset += 2; break; } // pointer
        offset += 1 + b;
    }
    return offset;
}

static void dns_handle(pt::uint32_t /*src_ip*/,
                       const pt::uint8_t* data, pt::uint32_t len) {
    if (len < 12) return;

    pt::uint16_t txid    = (pt::uint16_t)((data[0] << 8) | data[1]);
    pt::uint16_t flags   = (pt::uint16_t)((data[2] << 8) | data[3]);
    pt::uint16_t qdcount = (pt::uint16_t)((data[4] << 8) | data[5]);
    pt::uint16_t ancount = (pt::uint16_t)((data[6] << 8) | data[7]);

    klog("[DNS] handle: rxid=%d pending=%d flags=%x ancount=%d\n",
         (int)txid, (int)g_dns_pending_txid, (unsigned)flags, (int)ancount);

    if (txid != g_dns_pending_txid) {
        klog("[DNS] txid mismatch, ignoring\n");
        return;
    }
    if (!(flags & 0x8000)) {
        klog("[DNS] not a response (flags=%x), ignoring\n", (unsigned)flags);
        return;
    }

    int offset = 12;

    // Skip questions
    for (int q = 0; q < qdcount && offset < (int)len; q++) {
        offset = dns_skip_name(data, len, offset);
        offset += 4; // qtype + qclass
    }

    // Walk answers
    for (int a = 0; a < ancount && offset < (int)len; a++) {
        offset = dns_skip_name(data, len, offset);
        if (offset + 10 > (int)len) break;

        pt::uint16_t rtype  = (pt::uint16_t)((data[offset] << 8) | data[offset+1]); offset += 2;
        /* rclass */                                                                   offset += 2;
        /* ttl    */                                                                   offset += 4;
        pt::uint16_t rdlen  = (pt::uint16_t)((data[offset] << 8) | data[offset+1]); offset += 2;

        if (rtype == 1 && rdlen == 4 && offset + 4 <= (int)len) {
            // A record
            g_dns_reply_ip = ((pt::uint32_t)data[offset+3] << 24) |
                             ((pt::uint32_t)data[offset+2] << 16) |
                             ((pt::uint32_t)data[offset+1] <<  8) |
                              (pt::uint32_t)data[offset+0];
            klog("[DNS] A record found: %x\n", g_dns_reply_ip);
            g_dns_reply_got = true;
            return;
        }
        klog("[DNS] answer rtype=%d rdlen=%d (skipping)\n", (int)rtype, (int)rdlen);
        offset += rdlen;
    }
    klog("[DNS] no A record in answer section\n");
}

// ===========================================================================
// TCP client implementation
// ===========================================================================

static TcpSocket g_tcp_sockets[TCP_MAX_SOCKETS];  // zero-init → state=CLOSED

// Send a raw TCP segment on the given socket.
// flags: TCP_SYN, TCP_ACK, TCP_PSH|TCP_ACK, TCP_FIN|TCP_ACK, etc.
// payload / dlen: optional data to append after the TCP header.
static void tcp_send_raw(TcpSocket* sock, pt::uint8_t flags,
                          const pt::uint8_t* payload, pt::uint32_t dlen) {
    if (dlen > (pt::uint32_t)TCP_MSS) dlen = TCP_MSS;

    static pt::uint8_t seg[sizeof(TcpHdr) + TCP_MSS];
    TcpHdr* hdr = reinterpret_cast<TcpHdr*>(seg);
    hdr->src_port = bswap16(sock->local_port);
    hdr->dst_port = bswap16(sock->remote_port);
    hdr->seq      = bswap32(sock->snd_nxt);
    hdr->ack_seq  = bswap32(sock->rcv_nxt);
    hdr->data_off = 0x50;   // 5 32-bit words = 20 bytes
    hdr->flags    = flags;
    hdr->window   = bswap16((pt::uint16_t)TCP_RX_BUF);
    hdr->checksum = 0;
    hdr->urgent   = 0;

    if (payload && dlen > 0) {
        for (pt::uint32_t i = 0; i < dlen; i++)
            seg[sizeof(TcpHdr) + i] = payload[i];
    }

    pt::uint16_t tcp_len = (pt::uint16_t)(sizeof(TcpHdr) + dlen);

    // Compute checksum over pseudo-header + TCP segment (RFC 793)
    // Pseudo-header: src_ip(4) | dst_ip(4) | 0x00(1) | 0x06(1) | tcp_len(2)
    static pt::uint8_t pseudo_buf[12 + sizeof(TcpHdr) + TCP_MSS];
    pt::uint8_t* p = pseudo_buf;
    __builtin_memcpy(p, &g_my_ip,         4); p += 4;
    __builtin_memcpy(p, &sock->remote_ip, 4); p += 4;
    *p++ = 0; *p++ = 6;  // zero, protocol=TCP
    *p++ = (pt::uint8_t)(tcp_len >> 8);
    *p++ = (pt::uint8_t)(tcp_len & 0xFF);
    for (int i = 0; i < tcp_len; i++) p[i] = seg[i];

    hdr->checksum = inet_checksum(pseudo_buf, (int)(12 + tcp_len));
    ipv4_send(sock->remote_ip, 6, seg, tcp_len);
}

// Find an active socket matching the incoming segment's addresses/ports.
static TcpSocket* tcp_find_socket(pt::uint32_t remote_ip,
                                   pt::uint16_t remote_port,
                                   pt::uint16_t local_port) {
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        TcpSocket* s = &g_tcp_sockets[i];
        if (s->state    != TcpState::CLOSED &&
            s->remote_ip   == remote_ip   &&
            s->remote_port == remote_port &&
            s->local_port  == local_port)
            return s;
    }
    return nullptr;
}

// Receive-path state machine — called from ipv4_handle when proto==6.
void tcp_handle(pt::uint32_t src_ip, const pt::uint8_t* data, pt::uint32_t len) {
    if (len < sizeof(TcpHdr)) return;
    const TcpHdr* hdr = reinterpret_cast<const TcpHdr*>(data);

    pt::uint16_t sport    = bswap16(hdr->src_port);
    pt::uint16_t dport    = bswap16(hdr->dst_port);
    pt::uint32_t seq      = bswap32(hdr->seq);
    pt::uint32_t ack_seq  = bswap32(hdr->ack_seq);
    pt::uint8_t  flags    = hdr->flags;
    pt::uint32_t tcp_hlen = (pt::uint32_t)(hdr->data_off >> 4) * 4;

    TcpSocket* sock = tcp_find_socket(src_ip, sport, dport);
    if (!sock) return;  // no matching socket, silently discard

    const pt::uint8_t* payload     = (tcp_hlen <= len) ? (data + tcp_hlen) : nullptr;
    pt::uint32_t       payload_len = (tcp_hlen <= len) ? (len - tcp_hlen)  : 0;

    switch (sock->state) {

        case TcpState::SYN_SENT:
            if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
                sock->rcv_nxt = seq + 1;
                sock->snd_una = ack_seq;
                sock->snd_wnd = bswap16(hdr->window);
                sock->state   = TcpState::ESTABLISHED;
                tcp_send_raw(sock, TCP_ACK, nullptr, 0);
            }
            break;

        case TcpState::ESTABLISHED:
            if (flags & TCP_RST) { sock->state = TcpState::CLOSED; return; }
            if (flags & TCP_ACK) { sock->snd_una = ack_seq; }
            if (payload && payload_len > 0) {
                pt::uint32_t copied = 0;
                for (pt::uint32_t i = 0; i < payload_len; i++) {
                    pt::uint32_t used = sock->rx_tail - sock->rx_head;
                    if (used >= (pt::uint32_t)TCP_RX_BUF) break;  // drop if full
                    sock->rx_buf[sock->rx_tail % TCP_RX_BUF] = payload[i];
                    sock->rx_tail++;
                    copied++;
                }
                sock->rcv_nxt += copied;
                tcp_send_raw(sock, TCP_ACK, nullptr, 0);
            }
            if (flags & TCP_FIN) {
                sock->rcv_nxt++;
                sock->rx_eof = true;
                sock->state  = TcpState::CLOSE_WAIT;
                tcp_send_raw(sock, TCP_ACK, nullptr, 0);
            }
            break;

        case TcpState::FIN_WAIT_1:
            if (flags & TCP_ACK) {
                if (ack_seq == sock->snd_nxt)
                    sock->state = TcpState::FIN_WAIT_2;
            }
            if (flags & TCP_FIN) {
                sock->rcv_nxt++;
                sock->rx_eof = true;
                sock->state  = TcpState::TIME_WAIT;
                tcp_send_raw(sock, TCP_ACK, nullptr, 0);
            }
            break;

        case TcpState::FIN_WAIT_2:
            if (flags & TCP_FIN) {
                sock->rcv_nxt++;
                sock->rx_eof = true;
                sock->state  = TcpState::TIME_WAIT;
                tcp_send_raw(sock, TCP_ACK, nullptr, 0);
            }
            break;

        case TcpState::LAST_ACK:
            if (flags & TCP_ACK) { sock->state = TcpState::CLOSED; }
            break;

        case TcpState::TIME_WAIT:
            sock->state = TcpState::CLOSED;
            break;

        default:
            break;
    }
}

TcpSocket* tcp_connect(pt::uint32_t dst_ip, pt::uint16_t dst_port,
                        pt::uint64_t timeout_ticks) {
    // Find a free socket slot
    int slot = -1;
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (g_tcp_sockets[i].state == TcpState::CLOSED) { slot = i; break; }
    }
    if (slot == -1) return nullptr;

    TcpSocket* sock = &g_tcp_sockets[slot];
    sock->remote_ip   = dst_ip;
    sock->remote_port = dst_port;
    sock->local_port  = (pt::uint16_t)(49152 + slot);
    sock->snd_nxt     = 0xABC12300u + (pt::uint32_t)(slot * 0x1000);
    sock->snd_una     = sock->snd_nxt;
    sock->rcv_nxt     = 0;
    sock->snd_wnd     = 0;
    sock->rx_head     = 0;
    sock->rx_tail     = 0;
    sock->rx_eof      = false;
    sock->state       = TcpState::SYN_SENT;

    tcp_send_raw(sock, TCP_SYN, nullptr, 0);
    sock->snd_nxt++;  // SYN consumes one sequence number

    pt::uint64_t t0 = get_ticks();
    while (get_ticks() - t0 < timeout_ticks) {
        if (sock->state == TcpState::ESTABLISHED) return sock;
        if (sock->state == TcpState::CLOSED)      return nullptr;
        TaskScheduler::task_yield();
    }
    sock->state = TcpState::CLOSED;
    return nullptr;
}

int tcp_write(TcpSocket* s, const pt::uint8_t* data, pt::uint32_t len) {
    if (s->state != TcpState::ESTABLISHED) return -1;
    pt::uint32_t sent = 0;
    while (sent < len) {
        pt::uint32_t chunk = len - sent;
        if (chunk > (pt::uint32_t)TCP_MSS) chunk = TCP_MSS;
        tcp_send_raw(s, TCP_PSH | TCP_ACK, data + sent, chunk);
        s->snd_nxt += chunk;
        sent += chunk;
    }
    return (int)sent;
}

int tcp_read(TcpSocket* s, pt::uint8_t* buf, pt::uint32_t len,
             pt::uint64_t timeout_ticks) {
    pt::uint64_t t0 = get_ticks();
    while (s->rx_tail == s->rx_head && !s->rx_eof) {
        if (get_ticks() - t0 >= timeout_ticks) break;
        TaskScheduler::task_yield();
    }
    pt::uint32_t avail   = s->rx_tail - s->rx_head;
    pt::uint32_t to_copy = (len < avail) ? len : avail;
    for (pt::uint32_t i = 0; i < to_copy; i++) {
        buf[i] = s->rx_buf[s->rx_head % TCP_RX_BUF];
        s->rx_head++;
    }
    return (int)to_copy;
}

void tcp_close(TcpSocket* s) {
    if (s->state == TcpState::ESTABLISHED || s->state == TcpState::CLOSE_WAIT) {
        tcp_send_raw(s, TCP_FIN | TCP_ACK, nullptr, 0);
        s->snd_nxt++;
        s->state = (s->state == TcpState::ESTABLISHED)
                   ? TcpState::FIN_WAIT_1 : TcpState::LAST_ACK;
    }
    // Wait for graceful close (max 250 ticks ≈ 5 s at 50 Hz)
    pt::uint64_t t0 = get_ticks();
    while (s->state != TcpState::CLOSED && s->state != TcpState::TIME_WAIT) {
        if (get_ticks() - t0 >= 250) break;
        TaskScheduler::task_yield();
    }
    s->state = TcpState::CLOSED;  // free the slot
}

bool dns_resolve(const char* hostname, pt::uint64_t timeout_ticks,
                 pt::uint32_t& out_ip) {
    if (!RTL8139::is_present()) return false;

    // Build DNS query packet
    pt::uint8_t pkt[256];
    for (int i = 0; i < 256; i++) pkt[i] = 0;

    pt::uint16_t txid = g_dns_txid++;
    pkt[0] = (pt::uint8_t)(txid >> 8);
    pkt[1] = (pt::uint8_t)(txid & 0xFF);
    pkt[2] = 0x01; pkt[3] = 0x00; // flags: standard query + RD
    pkt[4] = 0x00; pkt[5] = 0x01; // qdcount = 1
    // ancount, nscount, arcount = 0

    int off = 12;
    off += dns_encode_name(pkt + off, hostname);

    pkt[off++] = 0x00; pkt[off++] = 0x01; // qtype = A
    pkt[off++] = 0x00; pkt[off++] = 0x01; // qclass = IN

    g_dns_pending_txid = txid;
    g_dns_reply_got    = false;
    g_dns_reply_ip     = 0;

    klog("[DNS] resolve: txid=%d query len=%d -> dns=%x\n",
         (int)txid, off, g_dns_ip);
    udp_send(1024, g_dns_ip, 53, pkt, (pt::uint16_t)off);

    pt::uint64_t deadline = get_ticks() + timeout_ticks;
    while (get_ticks() < deadline) {
        if (g_dns_reply_got) {
            klog("[DNS] resolve: got reply ip=%x\n", g_dns_reply_ip);
            out_ip = g_dns_reply_ip;
            return true;
        }
        TaskScheduler::task_yield();
    }
    klog("[DNS] resolve: timeout (txid=%d)\n", (int)txid);
    return false;
}
