# ESPHome C API Client

A lightweight, C-based client for communicating with ESPHome devices over the ESPHome Native API. This project implements the full protocol stack required to securely connect to, discover entities from, and receive real-time state updates from an ESPHome node.

## Status: Working Proof-of-Concept

This project is currently a working command-line proof of concept. We have successfully reverse-engineered and implemented the core ESPHome Native API protocol in C.

### What has been accomplished:

1. **Project Infrastructure & Tooling**
   - Built a custom `Makefile` for compiling under MinGW/GCC on Windows.
   - Integrated **Nanopb** for lightweight Protocol Buffer encoding and decoding.
   - Designed for cross-platform execution (ESP32/ESP8266 LwIP, Linux POSIX Sockets, Windows Winsock2).

2. **Encryption & Security (Noise Protocol)**
   - Successfully integrated the **Noise-C** library.
   - Implemented the `Noise_NNpsk0_25519_ChaChaPoly_SHA256` handshake sequence which ESPHome Native API >= 1.14+ requires.
   - Built the framing and encryption/decryption layers to handle ESPHome's custom payload structures (preamble `0x01`, 2-byte header, ciphertext payload).

3. **API Protocol Implementation**
   - **Modern Protocol Update:** Fully adopted API 1.14+ specifications, removing deprecated `ConnectRequest` phases.
   - **Device Info:** Requesting and decoding `DeviceInfoResponse` (name, MAC address, ESPHome version, compilation time, model, etc.).
   - **Entity Discovery & Registry:** Implemented dynamic tracking of devices during the `ListEntities` phase. Human-readable entity names are stored alongside their dynamically assigned 32-bit keys. Fallback logic automatically captures the `name` field if `object_id` is empty.
   - **State Subscriptions:** Implemented Nanopb string callbacks (`decode_string_cb`) to properly decode and capture string values in state updates (e.g., `TextSensorStateResponse`).
   - **Robust Async Receive Loop:** Implemented non-blocking `select()` based reads within the `esph_run_step` event loop.
   - **Ping/Pong Keep-Alive:** Implemented automated periodic `PingRequest` dispatching to maintain persistent, long-running connections.
   - **Command Sending:** Implemented full command sending routines like `esph_set_switch(session, "Switch Name", state)` that seamlessly resolve the string ID and securely dispatch a `SwitchCommandRequest` over the wire.

## Dependencies

- **Nanopb** (included in source)
- **Noise-C** (requires local compilation)
- **mbedTLS** (used as the crypto backend for Noise-C)
- **Winsock2** (for Windows networking) or **LwIP** (for ESP platforms)

## Usage

Using the C API is straightforward. See `examples/linux_client.c` for a complete example.

```c
#include "esphome_api.h"

int main() {
    // 1. Connect and automatically perform the Noise handshake
    esph_session_t *s = esph_connect("192.168.69.136", 6053, "base64_encoded_psk_key_here=");
    if (!s) return -1;

    // 2. Fetch basic device info
    esph_check_device_info(s);

    // 3. Start Entity Discovery and wait for it to complete
    esph_send_list_entities(s);
    esph_wait_list_entities_done(s);

    // 4. Subscribe to state changes (receives real-time sensor updates)
    esph_subscribe_states(s);

    // 5. Send Commands using Human-Readable Names
    esph_set_switch(s, "Smart Plug 28 Switch", 1); // Turn ON

    // 6. Enter the Event Loop
    while (1) {
        // Blocks for up to 50ms processing incoming state updates or pings
        if (esph_run_step(s, 50) < 0) {
            break; // Disconnected or Error
        }
        
        // (Application Logic Here: e.g. ping keep-alives)
    }

    esph_disconnect(s);
    return 0;
}
```

## Building and Running (CLI Client)

Ensure you have the `noise-c` library built and accessible to the compiler (update the `LDFLAGS` in `Makefile` if necessary to point to its location).

### Windows (MinGW)
Ensure you have MinGW installed.
```powershell
mingw32-make
.\get_status.exe <IP_ADDRESS> <PSK_BASE64>
.\real_esphome_client.exe <IP_ADDRESS> <PSK_BASE64> [PORT]
```

### Linux / macOS
Ensure you have `gcc` and `make` installed.
```bash
make
./get_status <IP_ADDRESS> <PSK_BASE64>
./real_esphome_client <IP_ADDRESS> <PSK_BASE64> [PORT]
```

### Examples
One-shot connection to fetch and display current status (great for scripts):
```bash
./get_status 192.168.1.100 "w4200oZ5WWe2T9aR3v+2K7A7P0q/Lw8E0Jb/c1fN/hQ="
```

Continuous run to test the event loop and stream real-time updates:
```bash
./real_esphome_client 192.168.1.100 "w4200oZ5WWe2T9aR3v+2K7A7P0q/Lw8E0Jb/c1fN/hQ=" 6053
```