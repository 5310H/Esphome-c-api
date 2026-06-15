#ifndef ESPHOME_API_H
#define ESPHOME_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration of session context
typedef struct esph_session esph_session_t;

/**
 * Connect to an ESPHome device using the Noise PSK protocol.
 *
 * This performs:
 *   1. TCP connect
 *   2. Noise NNpsk0 handshake
 *   3. Session object returned on success
 *
 * @param host  Hostname or IP of the ESPHome device
 * @param port  TCP port (default 6053)
 * @param psk   Pre-shared key (string form)
 * @return session pointer on success, NULL on failure
 */
esph_session_t *esph_connect(const char *host, uint16_t port, const char *psk);

/**
 * Disconnect and free the session.
 *
 * @param s Active session
 * @return 0 on success, <0 on failure
 */
int esph_disconnect(esph_session_t *s);

/**
 * Send a request for device information (HelloRequest + DeviceInfoRequest).
 *
 * @param s Active session
 * @return 0 on success, <0 on failure
 */
int esph_check_device_info(esph_session_t *s);

/**
 * Send a request to list all entities on the device.
 *
 * @param s Active session
 * @return 0 on success, <0 on failure
 */
int esph_send_list_entities(esph_session_t *s);

/**
 * Wait until all entities have been listed.
 * This runs the internal event loop until the ListEntitiesDone response is received.
 *
 * @param s Active session
 * @return 0 on success, <0 on failure
 */
int esph_wait_list_entities_done(esph_session_t *s);

/**
 * Subscribe to state changes for all entities.
 *
 * @param s Active session
 * @return 0 on success, <0 on failure
 */
int esph_subscribe_states(esph_session_t *s);

/**
 * Send a switch command to a specific entity.
 * This function resolves the entity string ID to its numeric key before sending.
 *
 * @param s          Active session
 * @param entity_id  ESPHome entity name (e.g. "Smart Plug 28 Switch")
 * @param state      1 = ON, 0 = OFF
 * @return 0 on success, <0 on failure
 */
int esph_set_switch(esph_session_t *s, const char *entity_id, int state);

/**
 * Send a Ping request to keep the connection alive.
 * Must be sent periodically if the connection is idle.
 *
 * @param s Active session
 * @return 0 on success, <0 on failure
 */
int esph_send_ping_request(esph_session_t *s);

/**
 * Run a single iteration of the event loop to process incoming packets (blocking).
 * It will wait up to timeout_ms for incoming data before returning.
 *
 * @param s          Active session
 * @param timeout_ms Timeout in milliseconds (0 for non-blocking, <0 for infinite)
 * @return 0 on success/timeout, <0 on disconnect or error.
 */
int esph_run_step(esph_session_t *s, int timeout_ms);

/**
 * Print a cleanly formatted table of all discovered entities and their most recently received states.
 */
void esph_print_all_entities(void);

#ifdef __cplusplus
}
#endif

#endif // ESPHOME_API_H
