#ifndef ESPHOME_MESSAGES_H
#define ESPHOME_MESSAGES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Message types used by the ESPHome API.
// These correspond to the message tags in the ESPHome API protocol.
// ---------------------------------------------------------------------------
typedef enum {
    ESPH_MSG_HELLO_REQUEST           = 1,
    ESPH_MSG_HELLO_RESPONSE          = 2,
    ESPH_MSG_CONNECT_REQUEST         = 3,
    ESPH_MSG_CONNECT_RESPONSE        = 4,
    ESPH_MSG_DISCONNECT_REQUEST      = 5,
    ESPH_MSG_DISCONNECT_RESPONSE     = 6,
    ESPH_MSG_PING_REQUEST            = 7,
    ESPH_MSG_PING_RESPONSE           = 8,
    ESPH_MSG_DEVICE_INFO_REQUEST     = 9,
    ESPH_MSG_DEVICE_INFO_RESPONSE    = 10,
    ESPH_MSG_SUBSCRIBE_STATES_REQUEST = 20,
    ESPH_MSG_SWITCH_COMMAND_REQUEST  = 26,
} esph_msg_type_t;

/**
 * Encode a message type varint into a buffer.
 *
 * @param type     Message type
 * @param out      Output buffer
 * @param maxlen   Maximum bytes available in output buffer
 * @return number of bytes written, <0 on failure
 */
int esph_encode_varint(uint32_t value, uint8_t *out, int maxlen);

/**
 * Decode a message type varint from a buffer.
 *
 * @param buf      Input buffer
 * @param len      Length of input buffer
 * @param value    Pointer to store the decoded value
 * @return number of bytes read, <0 on failure
 */
int esph_decode_varint(const uint8_t *buf, int len, uint32_t *value);

#ifdef __cplusplus
}
#endif

#endif // ESPHOME_MESSAGES_H
