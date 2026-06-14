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

    printf("Fetching device info...\n");
    esph_check_device_info(s);

    printf("Connected! Listing entities...\n");
    esph_send_list_entities(s);
    esph_wait_list_entities_done(s);

    printf("Subscribing to states...\n");
    if (esph_subscribe_states(s) != 0) {
        fprintf(stderr, "Failed to subscribe to states\n");
        return -1;
    }

    printf("\nEntering event loop. Waiting for state changes...\n");
    printf("Press Ctrl+C to exit.\n\n");
    fflush(stdout);
    while (1) {
        if (esph_run_step(s) != 0) {
            printf("\nDisconnected or error in run loop. Exiting.\n");
            break;
        }
        fflush(stdout);
    }

    printf("Disconnecting...\n");
    esph_disconnect(s);

    return 0;
}
