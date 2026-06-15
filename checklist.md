# ESPHome C API Client - Usage Toolset Checklist

This checklist outlines the remaining features required to transform the working protocol proof-of-concept into a fully robust, usable toolset.

## Core Connectivity & Stability
- [ ] **Implement Non-Blocking Event Loop**
  - Switch from blocking `recv()` calls to a `select()`-based network loop.
  - Allow the client to asynchronously listen for state changes without freezing the main thread.
- [ ] **Implement Ping/Pong Keep-Alive**
  - Add a timer to send a `PingRequest` (msg_type 7) to the ESPHome device every 15-30 seconds.
  - Track timestamps of received `PingResponse`s to detect disconnected/unresponsive devices and safely tear down the connection.

## Data Processing & Management
- [ ] **Implement String Parsing (Nanopb Callbacks)**
  - Write custom C callbacks to handle dynamic-length strings in Protocol Buffers (e.g., `TextSensorStateResponse` strings).
  - This resolves the issue where `TextSensor` states currently decode as blank fields.
- [ ] **Implement Entity ID Mapping**
  - During the `ListEntities` phase, save a mapping of the human-readable entity IDs (like `smart_plug_28_switch`) to their dynamically assigned 32-bit `key`.
  - This allows users of the C API to interact with devices using friendly names rather than raw numeric keys.

## Command & Control
- [ ] **Implement Command Sending**
  - Add API functions to actively send commands to the device (e.g., `esph_send_switch_command(s, "smart_plug_28_switch", true)`).
  - Construct and encrypt `SwitchCommandRequest`, `LightCommandRequest`, etc., and send them asynchronously over the open TCP socket.
