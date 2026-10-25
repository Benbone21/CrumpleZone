// The few socket calls the multiplayer code uses, the same on POSIX and Winsock (net.cpp, the
// join-code backend rendezvous.cpp, its tool). Header-only: each file gets its own copies.
#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600  // (Vista and later: the adapters' prefix lengths)
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
typedef int socklen_t;
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
static inline void closeSock(int fd) { closesocket((SOCKET)fd); }
static inline bool wouldBlock() {
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
static inline bool addrInUse() { return WSAGetLastError() == WSAEADDRINUSE; }
static inline void nonBlocking(int fd) {
    u_long one = 1;
    ioctlsocket((SOCKET)fd, FIONBIO, &one);
}
static inline int pollFds(pollfd* p, int n, int ms) { return WSAPoll(p, (ULONG)n, ms); }
static inline void netInit() {
    static bool done = false;
    if (done) return;
    WSADATA d;
    WSAStartup(MAKEWORD(2, 2), &d);
    done = true;
}
static inline int newSocket(int type) { return (int)socket(AF_INET, type, 0); }
// A UDP socket must not report "connection reset" after sending to a port nobody listens on
// (Windows turns that ICMP into an error on the next recvfrom, which would look like no data).
static inline void udpQuiet(int fd) {
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
    BOOL off = FALSE;
    DWORD got = 0;
    WSAIoctl((SOCKET)fd, SIO_UDP_CONNRESET, &off, sizeof off, nullptr, 0, &got, nullptr, nullptr);
}
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0  // macOS uses the per-socket SO_NOSIGPIPE option instead.
#endif
static inline void closeSock(int fd) { ::close(fd); }
static inline bool addrInUse() { return errno == EADDRINUSE; }
static inline bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }
static inline void nonBlocking(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
    // A disconnected peer must return an error, including in tools without main's signal handler.
    const int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
}
static inline int pollFds(pollfd* p, int n, int ms) { return poll(p, (nfds_t)n, ms); }
static inline void netInit() {}
static inline int newSocket(int type) { return socket(AF_INET, type, 0); }
static inline void udpQuiet(int) {}
#endif

#include <cstdint>
#include <cstring>

#include "netbase.h"

// An IPv4 address (host byte order) and port as a socket address.
static inline sockaddr_in sockAddr(uint32_t ip, uint16_t port) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(ip);
    a.sin_port = htons(port);
    return a;
}

// UDP out and in, through the test network (net::udpHook) when one is installed.
static inline int udpSend(int fd, const void* p, size_t n, uint32_t ip, uint16_t port) {
    if (net::udpHook && net::udpHook->send(fd, (const uint8_t*)p, n, ip, port)) return (int)n;
    const sockaddr_in a = sockAddr(ip, port);
    return (int)sendto(fd, (const char*)p, (int)n, 0, (const sockaddr*)&a, sizeof a);
}
// One datagram into `buf` (its size), and where from; <= 0: none waiting.
static inline int udpRecv(int fd, void* buf, size_t cap, uint32_t& ip, uint16_t& port) {
    if (net::udpHook) {
        const int r = net::udpHook->recv(fd, (uint8_t*)buf, cap, ip, port);
        if (r != 0) return r;
    }
    sockaddr_in from{};
    socklen_t fl = sizeof from;
    const int n = (int)recvfrom(fd, (char*)buf, (int)cap, 0, (sockaddr*)&from, &fl);
    if (n > 0) ip = ntohl(from.sin_addr.s_addr), port = ntohs(from.sin_port);
    return n;
}
// A UDP socket on `ip:port` (0: any), non-blocking. -1 if it can't be had.
static inline int udpOpen(uint32_t ip, uint16_t port) {
    netInit();
    const int fd = newSocket(SOCK_DGRAM);
    if (fd < 0) return -1;
    const sockaddr_in a = sockAddr(ip, port);
    if (bind(fd, (const sockaddr*)&a, sizeof a) < 0) {
        closeSock(fd);
        return -1;
    }
    nonBlocking(fd);
    udpQuiet(fd);
    return fd;
}
// The port a socket ended up on (0 if unknown).
static inline uint16_t boundPort(int fd) {
    sockaddr_in a{};
    socklen_t al = sizeof a;
    if (getsockname(fd, (sockaddr*)&a, &al) < 0) return 0;
    return ntohs(a.sin_port);
}
// "203.0.113.5" or a name -> IPv4 (host byte order). False if it can't be found.
static inline bool resolveIPv4(const std::string& host, uint32_t& ip) {
    netInit();
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (host.empty() || getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    ip = ntohl(((sockaddr_in*)res->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(res);
    return true;
}
