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
// Network configuration — mutable globals (updated by DHCP)
// ---------------------------------------------------------------------------
static constexpr pt::uint32_t NET_BROADCAST  = 0xFFFFFFFFu;           // 255.255.255.255

extern pt::uint32_t g_my_ip;      // our IP  (default 10.0.2.15)
extern pt::uint32_t g_gateway_ip; // gateway  (default 10.0.2.2)
extern pt::uint32_t g_dns_ip;     // DNS server (default 10.0.2.3)

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
// UDP header (RFC 768)
// ---------------------------------------------------------------------------
struct UdpHdr {
    pt::uint16_t src_port;
    pt::uint16_t dst_port;
    pt::uint16_t length;   // sizeof(UdpHdr) + payload, big-endian
    pt::uint16_t checksum; // 0 = disabled (valid for IPv4 UDP)
} __attribute__((packed));

// ---------------------------------------------------------------------------
// DHCP / BOOTP header (RFC 2131)
// ---------------------------------------------------------------------------
struct DhcpHdr {
    pt::uint8_t  op;         // 1=BOOTREQUEST, 2=BOOTREPLY
    pt::uint8_t  htype;      // 1=Ethernet
    pt::uint8_t  hlen;       // 6
    pt::uint8_t  hops;       // 0
    pt::uint32_t xid;        // transaction ID
    pt::uint16_t secs;       // 0
    pt::uint16_t flags;      // 0x8000 = broadcast flag (big-endian)
    pt::uint32_t ciaddr;     // client IP (0 during DISCOVER/REQUEST)
    pt::uint32_t yiaddr;     // offered IP from server
    pt::uint32_t siaddr;     // server IP
    pt::uint32_t giaddr;     // relay agent IP (0)
    pt::uint8_t  chaddr[16]; // client HW addr: MAC in [0..5], rest 0
    pt::uint8_t  sname[64];  // server hostname (zeros)
    pt::uint8_t  file[128];  // boot filename (zeros)
    pt::uint32_t magic;      // DHCP magic cookie 0x63825363 (see stack.cpp)
    // Variable-length TLV options follow
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
// TCP header (RFC 793, 20 bytes, no options)
// ---------------------------------------------------------------------------
struct __attribute__((packed)) TcpHdr {
    pt::uint16_t src_port;
    pt::uint16_t dst_port;
    pt::uint32_t seq;
    pt::uint32_t ack_seq;
    pt::uint8_t  data_off;   // upper nibble = header length in 32-bit words (5 = 20 bytes)
    pt::uint8_t  flags;      // TCP_SYN / TCP_ACK / TCP_PSH / TCP_FIN / TCP_RST
    pt::uint16_t window;
    pt::uint16_t checksum;
    pt::uint16_t urgent;
};

constexpr pt::uint8_t TCP_FIN = 0x01;
constexpr pt::uint8_t TCP_SYN = 0x02;
constexpr pt::uint8_t TCP_RST = 0x04;
constexpr pt::uint8_t TCP_PSH = 0x08;
constexpr pt::uint8_t TCP_ACK = 0x10;

enum class TcpState : pt::uint8_t {
    CLOSED = 0, SYN_SENT, ESTABLISHED,
    FIN_WAIT_1, FIN_WAIT_2, CLOSE_WAIT, LAST_ACK, TIME_WAIT,
};

constexpr int TCP_MAX_SOCKETS = 4;
constexpr int TCP_RX_BUF      = 4096;
constexpr int TCP_MSS         = 1024;   // conservative segment size

struct TcpSocket {
    TcpState     state;
    pt::uint32_t remote_ip;
    pt::uint16_t local_port;
    pt::uint16_t remote_port;
    pt::uint32_t snd_una;    // oldest unacknowledged seq
    pt::uint32_t snd_nxt;    // next seq to send
    pt::uint32_t rcv_nxt;    // next seq expected from peer
    pt::uint16_t snd_wnd;    // peer's advertised receive window

    // Receive ring buffer (4 KB)
    pt::uint8_t  rx_buf[TCP_RX_BUF];
    pt::uint32_t rx_head;    // read  position (monotonically increasing)
    pt::uint32_t rx_tail;    // write position (monotonically increasing)
    bool         rx_eof;     // FIN received from peer
};

// Store/load a TcpSocket* inside File::fs_data via memcpy (avoids aliasing issues)
static inline TcpSocket* tcp_sock_get(const pt::uint8_t* d) {
    TcpSocket* p; __builtin_memcpy(&p, d, sizeof(p)); return p;
}
static inline void tcp_sock_set(pt::uint8_t* d, TcpSocket* p) {
    __builtin_memcpy(d, &p, sizeof(p));
}

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

// Seq of the last ICMP type-3 (destination unreachable) received.
// Matches the seq that icmp_wait_reply was waiting for when it returned false.
pt::uint16_t icmp_last_unreachable_seq();

// Run DHCP DISCOVER→OFFER→REQUEST→ACK exchange.
// Blocks (spin+yield) up to timeout_ticks. Updates g_my_ip on success.
// Returns true if an ACK was received and g_my_ip updated.
bool dhcp_acquire(pt::uint64_t timeout_ticks);

// Resolve hostname to IPv4 via DNS (A record query to g_dns_ip).
// Blocks (spin+yield) up to timeout_ticks. Returns true on success.
bool dns_resolve(const char* hostname, pt::uint64_t timeout_ticks,
                 pt::uint32_t& out_ip);

// ---------------------------------------------------------------------------
// TCP client API
// ---------------------------------------------------------------------------

// Handle an incoming TCP segment (called from ipv4_handle for proto==6)
void tcp_handle(pt::uint32_t src_ip, const pt::uint8_t* data, pt::uint32_t len);

// Active connect: SYN handshake to dst_ip:dst_port; returns socket or nullptr on timeout/error
TcpSocket* tcp_connect(pt::uint32_t dst_ip, pt::uint16_t dst_port,
                        pt::uint64_t timeout_ticks);

// Send data on an ESTABLISHED socket; returns bytes sent or -1 on error
int tcp_write(TcpSocket* s, const pt::uint8_t* data, pt::uint32_t len);

// Read data from socket; blocks up to timeout_ticks; returns bytes read (0 = EOF)
int tcp_read(TcpSocket* s, pt::uint8_t* buf, pt::uint32_t len,
             pt::uint64_t timeout_ticks);

// Active close (FIN handshake); frees the socket slot
void tcp_close(TcpSocket* s);
