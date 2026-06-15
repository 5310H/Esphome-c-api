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

    int err = getaddrinfo(host, port_str, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "[TRANSPORT] getaddrinfo failed: %s\n", gai_strerror(err));
        return -1;
    }

    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        perror("[TRANSPORT] socket");
        freeaddrinfo(res);
        return -1;
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        perror("[TRANSPORT] connect");
        close(sock);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return sock;
}

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

