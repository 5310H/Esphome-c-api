#include "esphome_messages.h"
#include <string.h>

/*
 * Payload encoding/decoding is performed by Nanopb in api/proto_helpers.c
 * and api/esphome_api.c.  This file provides the small public primitive from
 * esphome_messages.h for callers that need protobuf varints.
 */
int esph_encode_varint(uint32_t value, uint8_t *out, int maxlen) {
    if (!out || maxlen <= 0) return -1;

    int written = 0;
    do {
        if (written >= maxlen) return -1;
        uint8_t byte = (uint8_t)(value & 0x7Fu);
        value >>= 7;
        out[written++] = value ? (uint8_t)(byte | 0x80u) : byte;
    } while (value != 0);
    return written;
}

int esph_decode_varint(const uint8_t *buf, int len, uint32_t *value) {
    if (!buf || !value || len <= 0) return -1;

    uint32_t result = 0;
    for (int i = 0; i < len && i < 5; i++) {
        uint8_t byte = buf[i];
        if (i == 4 && (byte & 0xF0u) != 0) return -1;  // uint32 overflow
        result |= (uint32_t)(byte & 0x7Fu) << (i * 7);
        if ((byte & 0x80u) == 0) {
            *value = result;
            return i + 1;
        }
    }
    return -1;
}
