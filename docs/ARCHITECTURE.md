# Firmware architecture

The V1 core is intentionally independent from GPIO details.

- IoT core: MQTT topics/payloads, reconnect, ACK, heartbeat, state, idempotence.
- HardwareAdapter: logical state and optional telemetry contract.
- KeyestudioAdapter: prototype-only GPIO mapping.
- IndustrialHardwareAdapter: future production implementation without changing IoT V1.

## Prototype -> industrial mapping

| Logical capability | Keyestudio prototype | Industrial target |
|---|---|---|
| switch output | configurable relay GPIO | certified switching stage |
| confirmed state | GPIO readback in lot 1 | dedicated feedback/current sensing |
| telemetry | unavailable => omitted | metering IC/sensors when present |
| network | ESP32 Wi-Fi | approved production radio/network module |

The prototype mapping is not an electrical design specification. Final GPIO, active level,
isolation, relay/contactor topology and metering circuitry require the validated industrial schematic.

## Runtime sequence

BOOT -> hardware safe OFF -> Wi-Fi -> MQTT -> heartbeat -> state(reconnect) -> command processing.

Loss of Wi-Fi/MQTT never invents cloud state. Reconnection republishes heartbeat then confirmed
local state. Duplicate command_id values ACK the existing result without replaying the physical action.

## Security

No production credential belongs in Git. Development credentials are injected with build flags or
local environment configuration. Production requires TLS, per-device/kit authentication and minimal
MQTT ACLs as mandated by IoT V1.
