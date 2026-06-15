#ifndef ESPHOME_TRANSPORT_H
#define ESPHOME_TRANSPORT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Open a TCP connection to the given host and port.
 *
 * @param host  Null-terminated hostname or IP (e.g. "192.168.1.10")
 * @param port  TCP port (ESPHome default is 6053)
 * @return socket fd (>=0) on success, <0 on failure
 */
int esph_transport_connect(const char *host, uint16_t port);

/**
 * Send data over an established TCP connection.
 *
 * @param sock  Socket fd returned by esph_transport_connect()
 * @param buf   Data buffer
 * @param len   Number of bytes to send
 * @return number of bytes sent, <0 on failure
 */
int esph_transport_send(int sock, const uint8_t *buf, int len);

/**
 * Receive data from an established TCP connection.
 *
 * @param sock    Socket fd
 * @param buf     Destination buffer
 * @param maxlen  Maximum bytes to read
 * @return number of bytes received, <0 on failure
 */
int esph_transport_recv(int sock, uint8_t *buf, int maxlen);

// Waits for the socket to become readable. Returns 1 if readable, 0 on timeout, -1 on error.
int esph_transport_wait_readable(int sock, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // ESPHOME_TRANSPORT_H
