# MQTT integration test plan — IoT V1

This plan validates the firmware against the normative .project/contracts/IOT_V1.md contract.

## Preconditions
- Development broker and credentials injected locally; no production secret in Git.
- Unique kit_serial and device_uid configured in firmware and backend.
- Physical prototype connected so relay state can be observed independently.
- MQTT QoS 1 used by the test publisher/subscriber.

## Core scenarios

1. Boot: connect Wi-Fi then MQTT. Expect heartbeat followed by state with reason=boot.
2. Valid command: publish set_state with UUID command_id, matching identities and ISO-8601 sent_at. Expect one physical transition, state reason=command and executed ACK.
3. Duplicate: republish the same command_id. Expect no second physical transition and an executed ACK carrying the original result.
4. Reboot duplicate: reboot ESP32, reconnect, then republish the same command_id. Expect no physical replay; NVS history must still identify it.
5. Identity mismatch: valid envelope but wrong kit_serial/device_uid. Expect failed ACK with identity_mismatch and no physical transition.
6. Invalid UUID: expect failed ACK invalid_command_id and no physical transition.
7. Missing/malformed sent_at: expect failed ACK invalid_sent_at and no physical transition.
8. Invalid action/state: expect failed ACK invalid_command and no physical transition.
9. Broker interruption: disconnect/reconnect broker. Expect heartbeat followed by state reason=reconnect and continued command processing.
10. Wi-Fi interruption: disconnect/reconnect Wi-Fi. Expect MQTT recovery, heartbeat, state reason=reconnect and continued command processing.
11. Telemetry unsupported: Keyestudio adapter must not publish fabricated electrical measurements.
12. Long run: operate for at least several heartbeat periods and verify uptime_ms increases and no command is replayed spontaneously.
13. Idempotence storage unavailable: simulate/force NVS initialization failure. A valid command for this device must fail with idempotence_storage_unavailable and no physical transition.
14. Validation precedence: while NVS/hardware is unavailable, send malformed UUID, malformed sent_at and wrong device identity. Expect invalid_command_id, invalid_sent_at and identity_mismatch respectively; internal hardware/NVS health must not be exposed before envelope/identity validation.
15. Idempotence persistence failure: force an NVS write/readback failure after a physical set_state. Expect failed ACK idempotence_persist_failed, confirmed state when known, and subsequent valid remote commands blocked by idempotence_storage_unavailable until recovery/reboot.

## Not yet executable as a normative test
- schedule behavior: V1 names the topic but does not define its payload or execution semantics.
- detailed offline/local policy: not defined by the current V1 contract.
- production TLS/device credential provisioning: required by V1 for final production but credentials/certificate lifecycle are not yet specified.

Those items require a LEAD contract extension before implementation.
