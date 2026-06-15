#include "esphome_api.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#define sleep_ms(ms) Sleep(ms)
#else
#include <unistd.h>
#define sleep_ms(ms) usleep((ms) * 1000)
#endif

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s <IP> <PSK_BASE64>\n", argv[0]);
        printf("Connects to the ESPHome device, retrieves its device info and current entity states, then exits.\n");
        return -1;
    }

    const char *ip = argv[1];
    const char *psk = argv[2];
    uint16_t port = 6053;

    printf("Connecting to %s...\n", ip);
    esph_session_t *s = esph_connect(ip, port, psk);
    if (!s) {
        fprintf(stderr, "Failed to connect.\n");
        return -1;
    }

    // Request Device Info
    esph_check_device_info(s);

    // List entities
    esph_send_list_entities(s);
    esph_wait_list_entities_done(s);

    // Subscribe to states
    esph_subscribe_states(s);

    // Run the event loop for 2 seconds to allow state updates to stream in
    printf("\nFetching current states...\n");
    for (int i = 0; i < 40; i++) { // 40 * 50ms = 2000ms
        if (esph_run_step(s, 50) < 0) {
            break;
        }
    }

    printf("\nStatus check complete. Disconnecting...\n");
    esph_disconnect(s);
    return 0;
}
