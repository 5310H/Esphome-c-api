#include "esphome_transport.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#if defined(ESP_PLATFORM) || defined(ESP8266)
// ESP-IDF / ESP8266 RTOS SDK using LwIP
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <unistd.h>
#elif defined(_WIN32)
// Windows
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
#else
// Standard POSIX (Linux, macOS, etc.)
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

// ---------------------------------------------------------------------------
// TCP Socket Connection Abstraction
// ---------------------------------------------------------------------------
/**
 * Opens a raw TCP socket connection to the target ESPHome node.
 * This function abstracts away the platform-specific socket initialization
 * (e.g., WSAStartup on Windows) and performs DNS resolution via getaddrinfo.
 *
 * @param host IP address or hostname of the device
 * @param port TCP port (typically 6053 for ESPHome Native API)
 * @return The active socket descriptor, or -1 on error
 */
int esph_transport_connect(const char *host, uint16_t port) {
#ifdef _WIN32
    static int wsa_init = 0;
    if (!wsa_init) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            fprintf(stderr, "[TRANSPORT] WSAStartup failed\n");
            return -1;
        }
        wsa_init = 1;
    }
#endif
    int sock = -1;
    struct addrinfo hints;
    struct addrinfo *res = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", port);

    char clean_host[128] = {0};
    const char *p = host ? host : "";
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "https://", 8) == 0) p += 8;
    strncpy(clean_host, p, sizeof(clean_host) - 1);
    char *colon = strchr(clean_host, ':');
    if (colon) *colon = '\0';
    char *slash = strchr(clean_host, '/');
    if (slash) *slash = '\0';

    int err = getaddrinfo(clean_host, port_str, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "[TRANSPORT] getaddrinfo failed for '%s': %d\n", clean_host, err);
        return -1;
    }

    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        perror("[TRANSPORT] socket");
        freeaddrinfo(res);
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        perror("[TRANSPORT] connect");
        close(sock);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return sock;
}

// ---------------------------------------------------------------------------
// TCP Blocking Send
// ---------------------------------------------------------------------------
/**
 * Synchronously sends an exact number of bytes over the active socket.
 * If the OS socket buffer fills up, this will loop and block until all 
 * `len` bytes have been pushed to the network stack.
 *
 * @param sock The active socket descriptor
 * @param buf  Pointer to the data to send
 * @param len  Exact number of bytes to send
 * @return The total bytes sent (equal to len), or -1 on network error
 */
int esph_transport_send(int sock, const uint8_t *buf, int len) {
    int total_sent = 0;
    while (total_sent < len) {
        int sent = send(sock, (const char *)(buf + total_sent), len - total_sent, 0);
        if (sent <= 0) {
            perror("[TRANSPORT] send");
            return -1;
        }
        total_sent += sent;
    }
    return total_sent;
}

// ---------------------------------------------------------------------------
// TCP Blocking Receive
// ---------------------------------------------------------------------------
/**
 * Synchronously receives an exact number of bytes from the socket.
 * This will loop and block until exactly `maxlen` bytes have been 
 * received. This is critical because ESPHome sends the 3-byte frame header,
 * and we MUST read exactly the ciphertext length specified in that header.
 *
 * @param sock   The active socket descriptor
 * @param buf    Pointer to the destination buffer
 * @param maxlen Exact number of bytes to read
 * @return The total bytes received (equal to maxlen), or -1 on network error/disconnect
 */
int esph_transport_recv(int sock, uint8_t *buf, int maxlen) {
    int total_recvd = 0;
    while (total_recvd < maxlen) {
        int r = recv(sock, (char *)(buf + total_recvd), maxlen - total_recvd, 0);
        if (r <= 0) {
            perror("[TRANSPORT] recv");
            return -1;
        }
        total_recvd += r;
    }
    return total_recvd;
}

// ---------------------------------------------------------------------------
// Socket Polling
// ---------------------------------------------------------------------------
/**
 * Polls the socket to see if there is any data waiting to be read.
 * This allows the main `esph_run_step` loop to be non-blocking (or block
 * for a specific timeout) so the application can do other work (like sending
 * pings or updating UI) while waiting for network packets.
 *
 * @param sock       The active socket descriptor
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return 1 if data is available, 0 if timeout expired, -1 on error
 */
int esph_transport_wait_readable(int sock, int timeout_ms) {
    if (sock < 0) return -1;
    
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);
    
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    int ret = select(sock + 1, &read_fds, NULL, NULL, &tv);
    if (ret < 0) {
        perror("[TRANSPORT] select");
        return -1;
    } else if (ret == 0) {
        return 0; // timeout
    }
    
    if (FD_ISSET(sock, &read_fds)) {
        return 1; // readable
    }
    return 0;
}

