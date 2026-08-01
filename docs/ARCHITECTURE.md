# Meshtastic Firmware Architecture (Deep Dive)

This document describes the firmware architecture in this repository, where key logic lives, and how data moves through the system.

## 1) Repository layout and responsibilities

| Path            | Responsibility                                                                   |
| --------------- | -------------------------------------------------------------------------------- |
| `src/main.cpp`  | Firmware bootstrap, platform setup, and startup orchestration                    |
| `src/mesh/`     | Core mesh stack (`MeshService`, `Router`, `NodeDB`, channels, packet handling)   |
| `src/modules/`  | Feature modules implemented on top of mesh packet ports                          |
| `src/platform/` | Platform-specific integrations (ESP32, nRF52, RP2xxx, STM32WL, portduino/native) |
| `variants/`     | Hardware variant pinouts and capability definitions                              |
| `protobufs/`    | Protocol definitions used for generated message bindings                         |
| `test/`         | Native test suites and behavior-focused unit/integration tests                   |
| `bin/`          | Build helpers and target matrix generation scripts                               |

## 2) Runtime architecture

At runtime, firmware behavior is driven by:

1. **Platform bootstrap** (`main.cpp`) initializes hardware, configuration, and services.
2. **Node and mesh state** are loaded/managed by `NodeDB`.
3. **Modules are instantiated** in `src/modules/Modules.cpp`.
4. **Packet dispatch** is coordinated by `MeshService` + `Router`.
5. **Periodic work** is run via `concurrency::OSThread`-based module threads.

The system is event-driven, with packet ingress/egress plus scheduled thread intervals.

## 3) Packet flow (high level)

### Incoming packet path

1. Radio/BLE/serial ingress reaches mesh routing/service layers.
2. Packet validation, addressing, and routing checks occur in `Router`/`MeshService`.
3. Matching modules are selected by `MeshModule::wantPacket()` (often by `portnum`).
4. Module logic processes payloads (`handleReceived`).

### Outgoing packet path

1. Module allocates packet (`router->allocForSending()` or helper wrappers).
2. Module encodes payload (protobuf or raw bytes), sets destination/hop metadata.
3. `MeshService` transmits packet to mesh transport with routing policy.

## 4) Module system and extension points

Core abstractions:

- `MeshModule`: base class for packet-aware features.
- `SinglePortModule`: helper base for modules bound to one `portnum`.
- `ProtobufModule<T>`: typed payload handling for protobuf messages.

Common extension pattern:

1. Add new module class under `src/modules/`.
2. Implement packet matching and receive/periodic handlers.
3. Instantiate in `setupModules()` in `src/modules/Modules.cpp`.
4. Gate by compile-time flags and config checks where needed.

## 5) Platform and hardware abstraction

- **Platform-specific BLE/radio/OS details** are isolated in `src/platform/*`.
- **Variant definitions** in `variants/*` declare capabilities (radio chip, pins, peripherals).
- **Architecture headers** and configuration toggles unify behavior across target families.

This separation keeps module/business logic portable across supported hardware.

## 6) External connectivity surfaces

### BLE phone integration

- BLE services are exposed through platform implementations (ESP32 NimBLE, nRF52 Bluefruit).
- Phone interaction paths integrate with core packet APIs and message queues.

### MQTT bridge

- MQTT integration lives in `src/mqtt/`.
- Mesh packets can be uplinked/downlinked based on channel/module settings.

### BitChat bridge (experimental)

- Implemented in `src/modules/BitChatBridgeModule.h/.cpp`.
- Encapsulates BitChat frames inside Meshtastic `PRIVATE_APP` packets using `BCHT` magic.
- Handles BLE to mesh relay, mesh to BLE broadcast, duplicate suppression, and TTL handling.
- Announcement nickname generation uses UTF-8-safe truncation so multibyte characters (including Ukrainian text) are not split.

## 7) Build and test model

- Build system: PlatformIO (`platformio.ini` + environment-specific includes).
- Native tests: `pio test -e native` (`test/` suite).
- Target-specific checks: `pio test -e t-echo-inkhud` and `pio test -e tbeam-s3-core`.
- Target matrix generation: `bin/generate_ci_matrix.py`.
- This fork intentionally disables GitHub Actions; workflow definitions are archived for reference under `.github/workflows/disabled/*.yml.disabled`.

## 8) UML diagrams

PlantUML diagram sources:

- `docs/uml/component-overview.puml`
- `docs/uml/module-class-diagram.puml`
- `docs/uml/bitchat-bridge-sequence.puml`

See `docs/uml/README.md` for rendering notes.
