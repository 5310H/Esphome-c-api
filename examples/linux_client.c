#include "esphome_api.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#endif


int main(int argc, char *argv[]) {
    printf("ESPHome C API - CLI Client\n");

    if (argc < 3) {
        printf("Usage: %s <IP> <PSK> [port]\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    const char *psk = argv[2];
    uint16_t port = 6053;
    if (argc > 3) {
        port = (uint16_t)atoi(argv[3]);
    }

    printf("Connecting to %s:%u with PSK %s...\n", host, port, psk);
    esph_session_t *s = esph_connect(host, port, psk);

    if (!s) {
        printf("Failed to connect to ESPHome device\n");
        return 1;
    }

    printf("Connected! Subscribing to states...\n");
    esph_subscribe_states(s);

    // Turn on first switch
    printf("Turning on switch...\n");
    esph_set_switch(s, "switch.relay_1", 1);

    // Wait a bit to see updates or let commands go through
    #ifdef _WIN32
    Sleep(2000);
    #else
    sleep(2);
    #endif

    printf("Disconnecting...\n");
    esph_disconnect(s);

    return 0;
}
