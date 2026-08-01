# Fork Changelog

This document tracks changes made to this fork relative to the upstream Meshtastic firmware repository. It includes custom features, bug fixes, configuration changes, hardware support additions, and integrations specific to this fork.

## Unreleased

### Changed

- **`MeshService::sendToMesh` API** - Changed return type from `void` to `ErrorCode` to surface TX queue and routing failures to callers
  - All 35 call sites across 26 files updated to check for success via the new `sendToMeshSucceeded()` helper
  - `TraceRouteModule` log messages now accurately reflect send success vs. failure
  - Related files: `src/mesh/MeshService.{h,cpp}`, and all module/telemetry/MQTT callers
- **Agent coding guidance** - Documented `sendToMesh` / `sendToMeshSucceeded` usage pattern and module exclusion guard convention
  - Updated: `AGENTS.md`, `.github/copilot-instructions.md`

### Added

- **`MeshService::sendToMeshSucceeded()`** - New `static inline` helper in `src/mesh/MeshService.h` that treats both `ERRNO_OK` and `ERRNO_SHOULD_RELEASE` as a successful send outcome, preventing spurious `LOG_WARN` at call sites that previously compared against `ERRNO_OK` alone

- **GitHub MCP Server** - Enabled by default for enhanced GitHub integration in development
  - Provides AI assistant access to GitHub operations (issues, PRs, code search, etc.)
  - Configuration: `.vscode/settings.json`
- **VS Code Extensions** - Comprehensive recommended extension list for development
  - Python, Protocol Buffers, YAML, Bash, Markdown, Docker, GitLens, spell checking
  - Configuration: `.vscode/extensions.json`
- **BitChatBridge Module** - New module for bridging Meshtastic mesh network with BitChat protocol via BLE
  - Supports bidirectional message translation between protocols
  - Custom BLE service UUID: `F47B5E2D-4A9E-4C5A-9B3F-8E1D2C3A4B5C`
  - Files: `src/modules/BitChatBridgeModule.{cpp,h}`
- **Build & Development Scripts**
  - `bin/pio-build-upload.sh` - Interactive script for selecting and building PlatformIO environments
  - `bin/setup_start9.sh` - Start9 service setup script
- **Hardware Runtime Container** - Dedicated `Dockerfile.hardware` for mapped serial/GPIO/I2C/SPI device deployments
  - Keeps production `Dockerfile` and `alpine.Dockerfile` running as the non-root `meshtasticd` user
- **Comprehensive Documentation Suite**
  - `.github/copilot-instructions.md` - Guidelines for AI-assisted development and automation
  - `CHANGELOG_FORK.md` - Fork-specific changelog tracking
  - `docs/ARCHITECTURE.md` - Deep dive into firmware architecture
  - `docs/uml/README.md` - UML diagram documentation
  - `docs/uml/bitchat-bridge-sequence.puml` - BitChat bridge sequence diagram
  - `docs/uml/component-overview.puml` - System component diagram
  - `docs/uml/module-class-diagram.puml` - Module class relationships
- **Local Validation Documentation**
  - Documented that this fork intentionally keeps GitHub Actions disabled
  - `.github/FORK_WORKFLOW_UPDATES.md` - Fork workflow policy and local validation guidance
- **Localization Support**
  - Ukrainian character support enabled in display system
- **Submodule documentation in README**
- **Fork management guidance** - Git workflow instructions for keeping fork synchronized

### Changed

- **Agent Tool Preference** - Broadened instruction guidance to prefer available MCP tools and IDE-integrated connectors over terminal commands when practical
  - Applies to repo MCP, GitHub MCP, and other agent-side connectors
- **Devcontainer PlatformIO Workflow** - Improved containerized build/test setup for firmware development
  - Added persistent PlatformIO cache volume and explicit `PLATFORMIO_CORE_DIR` in `.devcontainer/devcontainer.json`
  - Updated `.devcontainer/setup.sh` to initialize recursive submodules, install CLI tools deterministically, and pre-install `env:native` packages
  - Exported `$HOME/.local/bin` in `.devcontainer/setup.sh` before invoking `pio` to avoid `postCreateCommand` PATH issues
- **GitHub Actions Disabled** - Disabled all GitHub Actions workflows for this repository
  - Renamed all workflow files from `*.yml` to `*.yml.disabled` and moved them to `.github/workflows/disabled/` to prevent automatic execution
- **Trunk Lint Configuration** - Excluded disabled workflow directory from `actionlint`
  - Added `.github/workflows/disabled/**` ignore path to keep `trunk check --all` green with workflows intentionally disabled
- **Disabled Workflow Archive** - Preserved upstream workflow definitions under `.github/workflows/disabled/`
  - Keeps workflow files available for reference while preventing automatic GitHub Actions execution
  - Requires an explicit move back to `.github/workflows/<name>.yml` before any workflow can run
- **README.md** - Added fork management section with git sync commands and submodule documentation
- **Test Command Documentation** - Added explicit PlatformIO test instructions for `t-echo-inkhud` and `tbeam-s3-core` alongside `native`
  - Updated: `README.md`, `AGENTS.md`, `.github/copilot-instructions.md`, `docs/ARCHITECTURE.md`, `.github/prompts/new-module.prompt.md`
- **PlatformIO helper script** - Migrated `bin/pio-build-upload.sh` to run clean/build/upload through the devcontainer runtime instead of host `pio`
  - Builds the `.devcontainer` image and runs each invocation in a fresh isolated container
  - Supports optional USB/device passthrough via `DEVCONTAINER_DOCKER_RUN_ARGS` or `DEVCONTAINER_UPLOAD_DEVICE`
  - Fixed port auto-detection to filter out `/dev/cu.debug-console` and other non-upload pseudo-ports (Bluetooth, wlan) that appear before real modem ports on macOS
  - Simplified macOS upload path to consistently use host `pio run -t upload` for all supported environments
  - Added isolated runtime mode: each script invocation now creates a fresh per-run container, performs the selected env workflow, and removes the container automatically on exit
  - Added explicit docker-daemon reachability check (with Colima start guidance) before build/run steps
  - Added fail-fast validation for macOS uploads when host `pio` is missing, with actionable guidance
  - Added `--no-upload`/`--build-only` mode for devcontainer-only clean/build runs that intentionally skip host upload
  - Kept `docker build` output visible for easier troubleshooting of image build failures
- **Region Preset Fine-tuning** - Improved LoRa region configuration defaults
- **T-Echo Configuration** - Updated display graphics configuration (`variants/nrf52840/t-echo/nicheGraphics.h`)
- **T-Beam Supreme Configuration** - Platform configuration updates (`variants/esp32s3/tbeam-s3-core/platformio.ini`)
- **Trunk runtime config** - Bumped Rust runtime to `1.85.0` in `.trunk/trunk.yaml` to resolve `nixpkgs-fmt` install failures on `edition2024` crates

### Fixed

- **UA Region Table Cleanup** - Removed a duplicate `UA_433` entry from the radio region definitions
  - Keeps `getRegion()` behavior unambiguous in `src/mesh/RadioInterface.cpp`
- **BitChat Bridge Loop Suppression** - Changed BitChat duplicate detection to hash stable message identity instead of TTL
  - Prevents hop-count changes from reclassifying the same message as new in `src/modules/BitChatBridgeModule.cpp`
  - Added native unit tests covering TTL stability, recipient/signature flag hashing, mesh relay size limits, and identity-change behavior in `test/test_bitchat_bridge/test_main.cpp`
- **BitChat Bridge Mesh Relay Bounds** - Rejects BitChat BLE writes that cannot fit inside Meshtastic mesh packets
  - Prevents valid BLE frames near the BitChat payload limit from being silently dropped during mesh packet creation
  - Adds recipient ID test helper support in `src/modules/BitChatBridgeModule.h`
- **BitChat Bridge Scheduling and Target Gating** - Schedules BLE write processing immediately and only instantiates the bridge on supported ESP32 BLE targets
  - Prevents avoidable BLE queue latency and avoids no-op bridge threads on unsupported targets
- **BitChat Bridge BLE Security and Discovery** - Matched BitChat BLE characteristic permissions to Meshtastic pairing mode
  - Requires encrypted/authenticated reads and writes when Bluetooth pairing is enabled
  - Adds the BitChat service UUID to BLE advertising data after service registration and refreshes advertising then
- **BitChat Bridge TTL and Advertising Retry** - Preserves incoming BitChat TTL values when relaying to mesh and retries advertising until BitChat discovery is restored
  - Prevents single-hop BitChat frames from being dropped and avoids permanently suppressing discovery after a failed advertising restart
  - Marks the BitChat service ready before refreshing advertising so the BitChat scan response is rebuilt correctly
  - Propagates advertising-start failures on legacy NimBLE builds so retries continue instead of being treated as success
  - Clears BitChat ready state on disconnect when advertising restart fails so the bridge keeps retrying
- **BitChat Bridge Delivery and Helper Fixes** - Restored BitChat characteristic sizing, duplicate tracking, and Linux upload device passthrough
  - Sets an explicit max length on the BitChat BLE characteristic so larger frames are not truncated by the NimBLE default
  - Moves duplicate tracking until after BLE-to-mesh and mesh-to-BLE send attempts complete so transient relay failures can retry
  - Prompts for and mounts the chosen Linux upload port before the devcontainer starts so `pio run -t upload` can access the device
- **Device Report Redaction** - Removed sensitive network and node identifiers from `/json/report`
  - Omits SSIDs, MAC addresses, and node identifiers from the JSON payload in `src/mesh/http/ContentHandler.cpp`
  - Added unit tests for sensitive/non-sensitive classification and null/unknown input safety in `test/test_http_content_handler/test_main.cpp`
- **Build Warning Cleanup (`tbeam-s3-core`)** - Resolved multiple compiler warnings without behavior changes
  - Added explicit `[[fallthrough]]` in `src/detect/ScanI2CTwoWire.cpp`
  - Removed unused local in `src/graphics/draw/UIRenderer.cpp`
  - Simplified volatile casts in `src/mesh/aes-ccm.cpp`
  - Scoped E-Ink-only variable usage in `src/modules/CannedMessageModule.cpp`
  - Guarded `ledState` declaration in `src/modules/StatusLEDModule.cpp` so boards without direct LED GPIO macros do not emit an unused-variable warning
  - Guarded `NO_DATA` macro around HTTPS includes in `src/mesh/http/{WebServer.cpp,ContentHandler.cpp}` to avoid macro redefinition warnings
- **T-Echo InkHUD Build Failure** - Resolved C++ `<chrono>` compilation errors in `t-echo-inkhud`
  - Root cause: `Arduino.h` macros (`abs`, `round`) collided with `<chrono>` symbols when `BitChatBridgeModule` pulled in `<mutex>`
  - Fix: Wrapped `<mutex>` include in macro push/undef/pop guards in `src/modules/BitChatBridgeModule.h`
- **Trunk lint/security findings cleanup**
  - Disabled workflow archive lint findings addressed under `.github/workflows/disabled/`
  - USB-oriented `docker-compose.yml` now uses `Dockerfile.hardware` for mapped device access while production Docker images remain non-root
  - Builder-only Docker suppressions corrected in `.clusterfuzzlite/Dockerfile` for checkov/trivy rules
  - Removed high-entropy sample secret from `userPrefs.jsonc` and fixed unused import handling in `extra_scripts/esp32_extra.py`
- **Disabled Workflow Archive Cleanup**
  - GPG key import conditionals fixed in archived reusable workflow definitions
  - Workflow conditional logic for self-hosted runners clarified in archived workflow definitions
  - PR test automation remains disabled in this fork; local validation commands are documented instead
- **Helper Script Portability** - Replaced Bash 4-only `mapfile` usage in `bin/pio-build-upload.sh`
  - Keeps the macOS-aware helper compatible with the system Bash 3.2 shipped on macOS
- **PlatformIO Helper Port Detection** - Fixed no-device auto-detection handling in `bin/pio-build-upload.sh`
  - Returns an empty detected port without tripping `set -e`, so the manual port prompt can run
- **Start9 Setup Helper** - Removed references to unshipped Lightning gateway files
  - The script now generates the Start9 config without installing missing `lightning_gateway_*` files
- **Start9 Setup Helper Target Names** - Corrected manual flashing instructions to use existing PlatformIO environments
  - Uses `tbeam-s3-core` and `t-echo` in `bin/setup_start9.sh`
- **BitChat Bridge Hop Limit** - Caps relayed BitChat mesh packets by the configured Meshtastic hop limit
  - Prevents BitChat TTL from expanding mesh propagation beyond local LoRa configuration
- **Device Report Redaction** - Treats `device.node_num` as a node identifier and omits it from redacted `/json/report`
  - Keeps numeric node IDs covered by the same report redaction policy as formatted node IDs
- **Device Report Name Redaction** - Omits autogenerated `device_name` from `/json/report`
  - Prevents the default node-derived display name from leaking identity through the report payload
- **Device Report Uptime** - Keeps `device.uptime_ms` unsigned in `/json/report`
  - Prevents negative uptime values on nodes running longer than signed 32-bit millisecond range
- **HTTP Content Handler** - Bug fixes in `src/mesh/http/ContentHandler.cpp`
- **Radio Interface** - Improvements in `src/mesh/RadioInterface.cpp`
- **MQTT Test** - Test updates in `test/test_mqtt/MQTT.cpp`

### Hardware Support

- Enhanced support for T-Echo (nRF52840 with e-ink display)
- Enhanced support for T-Beam Supreme (ESP32-S3 variant)
- Build optimizations for both platforms

### Documentation

- Created comprehensive developer documentation for AI agents and human contributors
- Added architecture diagrams and UML documentation
- Documented local validation expectations and disabled workflow policy
- Restored MCP hardware-test and encryption/key-management guidance in `.github/copilot-instructions.md`
- Moved canonical agent guidance into `.github/copilot-instructions.md` while keeping `AGENTS.md` as the short repository entry point

### Integration

- BitChat protocol bridge for external app integration
- Start9 service integration support

## Past Releases

### [0.1.0] - 2025-11-03

**Commit:** `8d710ba7b14c35318d9f48da43039abb7b5e2bfe`

**Added**

- BitChatBridge module for protocol bridging
- Build automation scripts (pio-build-upload.sh, setup_start9.sh)
- Fork docs (AGENTS.md, workflow policy, UML diagrams)
- Ukrainian character support
- Fork changelog tracking

**Changed**

- README fork sync guidance
- LoRa region preset fine-tuning
- Disabled upstream GitHub Actions (`.yml` → `.yml.disabled`)

**Fixed**

- Workflow GPG key import issues in archived definitions
- RadioInterface and ContentHandler bug fixes

**Hardware Support**

- T-Echo (nRF52840) and T-Beam Supreme (ESP32-S3) configuration updates
