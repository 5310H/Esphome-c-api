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

int esph_disconnect(esph_session_t *s);
int esph_check_device_info(esph_session_t *s);
int esph_send_list_entities(esph_session_t *s);
int esph_wait_list_entities_done(esph_session_t *s);
int esph_subscribe_states(esph_session_t *s);
int esph_send_list_entities(esph_session_t *s);

/**
 * Send a switch command to an entity.
 *
 * @param s          Active session
 * @param entity_id  ESPHome entity ID (e.g. "switch.lamp")
 * @param state      1 = ON, 0 = OFF
 * @return 0 on success, <0 on failure
 */
int esph_set_switch(esph_session_t *s, const char *entity_id, int state);

// Run a single iteration of the receive loop to process incoming packets (blocking)
// Returns 0 on success, <0 on disconnect or error.
int esph_send_ping_request(esph_session_t *s);
int esph_run_step(esph_session_t *s, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // ESPHOME_API_H
