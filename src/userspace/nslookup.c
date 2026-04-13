/* nslookup — resolve a hostname to an IPv4 address via DNS over UDP.
 *
 * Usage:  nslookup <hostname> [dns_server]
 *   dns_server defaults to 10.0.2.3 (QEMU SLIRP DNS).
 *
 * Builds a standard RFC 1035 A-record query, sends it over an ephemeral UDP
 * socket, waits up to ~3 seconds for the reply, parses the answer section.
 */
#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/syscall.h"

/* IPs in this OS are stored with byte 'a' at the lowest address, so
 * make_ip(a,b,c,d) = a | (b<<8) | (c<<16) | (d<<24). */
static unsigned int make_ip(unsigned char a, unsigned char b,
                            unsigned char c, unsigned char d)
{
    return (unsigned int)a
         | ((unsigned int)b << 8)
         | ((unsigned int)c << 16)
         | ((unsigned int)d << 24);
}

/* Parse "A.B.C.D" into an IP. Returns 0 on failure. */
static int parse_ip(const char* s, unsigned int* out)
{
    unsigned int parts[4] = {0};
    int pi = 0;
    int have_digit = 0;
    for (const char* p = s; ; p++) {
        char c = *p;
        if (c >= '0' && c <= '9') {
            parts[pi] = parts[pi] * 10 + (unsigned int)(c - '0');
            if (parts[pi] > 255) return 0;
            have_digit = 1;
        } else if (c == '.' || c == 0) {
            if (!have_digit) return 0;
            have_digit = 0;
            if (c == 0) {
                if (pi != 3) return 0;
                *out = make_ip((unsigned char)parts[0], (unsigned char)parts[1],
                               (unsigned char)parts[2], (unsigned char)parts[3]);
                return 1;
            }
            pi++;
            if (pi >= 4) return 0;
        } else {
            return 0;
        }
    }
}

/* Encode hostname into DNS label format: "www.google.com" -> "\3www\6google\3com\0".
 * Returns number of bytes written. */
static int dns_encode_name(unsigned char* buf, const char* host)
{
    int out = 0;
    const char* s = host;
    while (*s) {
        const char* label = s;
        while (*s && *s != '.') s++;
        int len = (int)(s - label);
        if (len == 0 || len > 63) return -1;
        buf[out++] = (unsigned char)len;
        for (int i = 0; i < len; i++) buf[out++] = (unsigned char)label[i];
        if (*s == '.') s++;
    }
    buf[out++] = 0;
    return out;
}

/* Skip a (possibly-compressed) DNS name, return new offset. */
static int dns_skip_name(const unsigned char* d, int len, int off)
{
    while (off < len) {
        unsigned char c = d[off];
        if (c == 0) return off + 1;
        if ((c & 0xC0) == 0xC0) return off + 2;     /* compression pointer */
        off += 1 + c;
    }
    return off;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        puts("usage: nslookup <hostname> [dns_server_ip]");
        return 1;
    }
    const char* host = argv[1];

    unsigned int dns_ip = make_ip(10, 0, 2, 3);     /* QEMU SLIRP default */
    if (argc >= 3) {
        if (!parse_ip(argv[2], &dns_ip)) {
            printf("bad dns server ip: %s\n", argv[2]);
            return 1;
        }
    }

    int fd = sys_udp_open(0);
    if (fd < 0) { puts("udp_open failed"); return 1; }

    /* Build query */
    unsigned char pkt[300];
    unsigned short txid = (unsigned short)(sys_get_ticks() & 0xFFFF) ^ 0xBEEF;
    pkt[0] = (unsigned char)(txid >> 8);
    pkt[1] = (unsigned char)(txid & 0xFF);
    pkt[2] = 0x01; pkt[3] = 0x00;   /* flags: standard query, RD=1 */
    pkt[4] = 0x00; pkt[5] = 0x01;   /* qdcount=1 */
    pkt[6] = 0x00; pkt[7] = 0x00;   /* ancount=0 */
    pkt[8] = 0x00; pkt[9] = 0x00;   /* nscount=0 */
    pkt[10] = 0x00; pkt[11] = 0x00; /* arcount=0 */
    int off = 12;
    int enc = dns_encode_name(pkt + off, host);
    if (enc < 0) { puts("bad hostname"); sys_close(fd); return 1; }
    off += enc;
    pkt[off++] = 0x00; pkt[off++] = 0x01;   /* qtype=A  */
    pkt[off++] = 0x00; pkt[off++] = 0x01;   /* qclass=IN */

    printf("querying %s via %u.%u.%u.%u:53\n", host,
           dns_ip & 0xFF, (dns_ip >> 8) & 0xFF,
           (dns_ip >> 16) & 0xFF, (dns_ip >> 24) & 0xFF);

    if (sys_udp_sendto(fd, pkt, off, dns_ip, 53) < 0) {
        puts("sendto failed");
        sys_close(fd);
        return 1;
    }

    /* Receive — timeout ~3s at 50 Hz scheduler tick */
    unsigned char reply[512];
    unsigned long long peer = 0;
    long n = sys_udp_recvfrom(fd, reply, sizeof(reply), &peer, 150);
    sys_close(fd);

    if (n == 0) { puts("timeout"); return 1; }
    if (n < 0)  { puts("recvfrom error"); return 1; }
    if (n < 12) { puts("short reply"); return 1; }

    unsigned short rxid = ((unsigned short)reply[0] << 8) | reply[1];
    if (rxid != txid) { printf("txid mismatch (got %x expected %x)\n", rxid, txid); return 1; }
    unsigned short flags = ((unsigned short)reply[2] << 8) | reply[3];
    if (!(flags & 0x8000)) { puts("not a response"); return 1; }
    int rcode = flags & 0x0F;
    if (rcode != 0) { printf("dns error rcode=%d\n", rcode); return 1; }

    int qdcount = ((int)reply[4] << 8) | reply[5];
    int ancount = ((int)reply[6] << 8) | reply[7];
    int p = 12;
    for (int q = 0; q < qdcount && p < (int)n; q++) {
        p = dns_skip_name(reply, (int)n, p);
        p += 4;
    }
    int found = 0;
    for (int a = 0; a < ancount && p < (int)n; a++) {
        p = dns_skip_name(reply, (int)n, p);
        if (p + 10 > (int)n) break;
        unsigned short rtype = ((unsigned short)reply[p] << 8) | reply[p+1]; p += 2;
        p += 2;                      /* rclass */
        p += 4;                      /* ttl   */
        unsigned short rdlen = ((unsigned short)reply[p] << 8) | reply[p+1]; p += 2;
        if (rtype == 1 && rdlen == 4 && p + 4 <= (int)n) {
            printf("%s  ->  %u.%u.%u.%u\n", host,
                   reply[p+0], reply[p+1], reply[p+2], reply[p+3]);
            found = 1;
        }
        p += rdlen;
    }
    if (!found) { puts("no A record in answer"); return 1; }
    return 0;
}
