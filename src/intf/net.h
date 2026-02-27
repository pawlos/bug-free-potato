#pragma once
#include "defs.h"

// ---------------------------------------------------------------------------
// Big-endian (network byte order) helpers
// ---------------------------------------------------------------------------
static inline pt::uint16_t bswap16(pt::uint16_t x) {
    return (pt::uint16_t)((x >> 8) | (x << 8));
}

static inline pt::uint32_t bswap32(pt::uint32_t x) {
    return ((x & 0xFF000000u) >> 24) |
           ((x & 0x00FF0000u) >>  8) |
           ((x & 0x0000FF00u) <<  8) |
           ((x & 0x000000FFu) << 24);
}

// Build a uint32_t from 4 octets stored in network byte order.
// On little-endian x86: byte 'a' (first/most-significant in IP notation) ends
// up at the lowest memory address, matching what arrives in the packet wire.
static constexpr pt::uint32_t make_ip(pt::uint32_t a, pt::uint32_t b,
                                       pt::uint32_t c, pt::uint32_t d) {
    return (d << 24) | (c << 16) | (b << 8) | a;
}

// ---------------------------------------------------------------------------
// Static network configuration (QEMU SLIRP user-mode networking)
// ---------------------------------------------------------------------------
static constexpr pt::uint32_t NET_MY_IP      = make_ip(10, 0, 2, 15); // 10.0.2.15
static constexpr pt::uint32_t NET_GATEWAY_IP = make_ip(10, 0, 2,  2); // 10.0.2.2
static constexpr pt::uint32_t NET_BROADCAST  = 0xFFFFFFFFu;           // 255.255.255.255

// ---------------------------------------------------------------------------
// Ethernet frame header
// ---------------------------------------------------------------------------
struct EthHdr {
    pt::uint8_t  dst[6];
    pt::uint8_t  src[6];
    pt::uint16_t ethertype;   // big-endian: 0x0806=ARP, 0x0800=IPv4
} __attribute__((packed));

// ---------------------------------------------------------------------------
// ARP packet (IPv4 over Ethernet, RFC 826)
// ---------------------------------------------------------------------------
struct ArpPkt {
    pt::uint16_t htype;       // hardware type: 1 = Ethernet
    pt::uint16_t ptype;       // protocol type: 0x0800 = IPv4
    pt::uint8_t  hlen;        // hardware address length: 6
    pt::uint8_t  plen;        // protocol address length: 4
    pt::uint16_t oper;        // 1 = request, 2 = reply
    pt::uint8_t  sha[6];      // sender hardware address
    pt::uint32_t spa;         // sender protocol (IP) address
    pt::uint8_t  tha[6];      // target hardware address
    pt::uint32_t tpa;         // target protocol (IP) address
} __attribute__((packed));

// ---------------------------------------------------------------------------
// IPv4 header (no options, RFC 791)
// ---------------------------------------------------------------------------
struct IPv4Hdr {
    pt::uint8_t  ver_ihl;     // version (4) + IHL (5 for no options)
    pt::uint8_t  dscp_ecn;
    pt::uint16_t total_len;   // big-endian, includes header + payload
    pt::uint16_t id;          // big-endian
    pt::uint16_t flags_frag;  // big-endian
    pt::uint8_t  ttl;
    pt::uint8_t  proto;       // 1=ICMP, 6=TCP, 17=UDP
    pt::uint16_t checksum;    // big-endian
    pt::uint32_t src_ip;      // network byte order
    pt::uint32_t dst_ip;      // network byte order
} __attribute__((packed));

// ---------------------------------------------------------------------------
// ICMP echo header (RFC 792)
// ---------------------------------------------------------------------------
struct IcmpHdr {
    pt::uint8_t  type;        // 8=echo request, 0=echo reply
    pt::uint8_t  code;
    pt::uint16_t checksum;    // big-endian
    pt::uint16_t id;          // big-endian
    pt::uint16_t seq;         // big-endian
} __attribute__((packed));

// ---------------------------------------------------------------------------
// ARP cache entry
// ---------------------------------------------------------------------------
struct ArpEntry {
    pt::uint32_t ip;          // network byte order; 0 = empty slot
    pt::uint8_t  mac[6];
};

// ---------------------------------------------------------------------------
// RTL8139 NIC driver (PCI NIC, I/O space mapped)
// ---------------------------------------------------------------------------
class RTL8139 {
public:
    static bool initialize();
    static void send(const pt::uint8_t* data, pt::uint32_t len);
    static bool is_present();
    static pt::uint16_t get_io_base();
    static void get_mac(pt::uint8_t out_mac[6]);
    static void handle_irq();   // called from irq11_handler

private:
    static bool         initialized;
    static pt::uint16_t io_base;
    static pt::uint8_t  mac[6];
    static pt::uint8_t* rx_buf;
    static pt::uint32_t rx_read;
    static pt::uint8_t* tx_buf[4];
    static pt::uint8_t  tx_slot;
    static pt::uint16_t pci_bus;
    static pt::uint8_t  pci_dev;

    static pt::uint32_t pci_read_dword(pt::uint8_t offset);
    static void         pci_write_dword(pt::uint8_t offset, pt::uint32_t value);
};

// ---------------------------------------------------------------------------
// Network stack — called from RTL8139 ISR and from shell commands
// ---------------------------------------------------------------------------

// Dispatch a received Ethernet frame (called from RTL8139 ISR)
void net_receive(pt::uint8_t* data, pt::uint32_t len);

// Internet checksum (RFC 1071)
pt::uint16_t inet_checksum(const void* data, int len);

// Send ICMP echo request; seq is host byte order
bool icmp_ping(pt::uint32_t dst_ip, pt::uint16_t seq);

// Spin-yield until a reply with matching seq arrives or timeout expires.
// Returns true on reply received.
bool icmp_wait_reply(pt::uint16_t seq, pt::uint64_t timeout_ticks);

// Tick at which the last ICMP reply arrived (for RTT calculation)
pt::uint64_t icmp_last_reply_tick();
