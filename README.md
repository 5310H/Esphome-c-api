# ESPHome C API Client

A lightweight, C-based client for communicating with ESPHome devices over the ESPHome Native API. This project implements the full protocol stack required to securely connect to, discover entities from, and receive real-time state updates from an ESPHome node.

## Status: Working Proof-of-Concept

This project is currently a working command-line proof of concept. We have successfully reverse-engineered and implemented the core ESPHome Native API protocol in C.

### What has been accomplished so far:

1. **Project Infrastructure & Tooling**
   - Built a custom `Makefile` for compiling under MinGW/GCC on Windows.
   - Integrated **Nanopb** for lightweight Protocol Buffer encoding and decoding.
   - Generated C structures for the ESPHome `api.proto` definitions, applying custom `.options` to handle variable-length string fields gracefully.

2. **Encryption & Security (Noise Protocol)**
   - Successfully integrated the **Noise-C** library.
   - Implemented the `Noise_NNpsk0_25519_ChaChaPoly_SHA256` handshake sequence which ESPHome uses to secure the Native API connection.
   - Built the framing and encryption/decryption layers to handle ESPHome's custom payload structures (preamble `0x01`, 2-byte header, ciphertext payload).

3. **API Protocol Implementation**
   - **Handshake Phase:** Sending the prologue and exchanging Noise `e` messages to establish the symmetric session keys.
   - **Hello & Connect Phase:** Sending `HelloRequest` and `ConnectRequest`, and parsing the device's `HelloResponse` and `ConnectResponse`.
   - **Device Info:** Requesting and decoding `DeviceInfoResponse` (name, MAC address, ESPHome version, compilation time, model, etc.).
   - **Entity Discovery:** Sending `ListEntitiesRequest` and successfully parsing the stream of `ListEntities*Response` messages to identify all configured sensors, switches, binary sensors, etc., on the device.
   - **State Subscriptions:** Sending `SubscribeStatesRequest` and successfully decoding real-time state updates.
   - **Bug Fixes:** Resolved complex decoding bugs, such as nanopb "wrong wire type" errors caused by mapping `fixed32` keys incorrectly.

### Next Steps & Pending Work:

- **Robust Async Receive Loop:** Currently, the event loop uses blocking socket `recv()` calls. We need to implement `select()` or non-blocking reads to handle asynchronous network events gracefully.
- **Ping/Pong Keep-Alive:** The client must send a `PingRequest` every 15-30 seconds to prevent the ESPHome device from timing out and closing the connection.
- **String Callbacks:** Implement Nanopb callbacks to properly decode and capture string values in state updates (e.g., `TextSensorStateResponse`).
- **Command Sending:** Implement the API calls to actively toggle switches, change light colors, etc. (e.g., `SwitchCommandRequest`).

## Dependencies

- **Nanopb** (included in source)
- **Noise-C** (requires local compilation)
- **mbedTLS** (used as the crypto backend for Noise-C)
- **Winsock2** (for Windows networking)

## Building and Running

Ensure you have MinGW installed and the `noise-c` library built and accessible to the compiler.

```bash
mingw32-make
./real_esphome_client.exe <IP_ADDRESS> <PSK_BASE64> [PORT]
```

Example:
```bash
./real_esphome_client.exe 192.168.1.100 "base64_encoded_psk_key_here=" 6053
```