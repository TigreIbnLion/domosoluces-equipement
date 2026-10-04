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
16. Fragmented command: deliver one valid command payload through multiple MQTT data fragments with contiguous offsets, including continuation fragments with no repeated topic. Expect exactly one command execution after the final fragment and the normal state/ACK sequence.
17. Invalid fragment sequence: deliver a command with a gap, overlap, changed total length or out-of-order offset. Expect assembly reset, no physical transition and no partial command execution. Also disconnect MQTT mid-fragment, reconnect, then send a valid command; stale bytes must never be reused.
18. Oversized command: deliver a command whose MQTT payload exceeds 512 bytes. Expect the entire message to be discarded, no physical transition and no unbounded allocation. A later valid command must still be accepted normally.
19. MQTT stalled recovery: keep Wi-Fi connected while broker is unavailable for more than 60 seconds. Expect controlled client recycle; after broker recovery expect heartbeat then state reason=reconnect and normal commands.
20. Unconfigured hardware mapping: boot without DOMO_RELAY_PIN/DOMO_RELAY_ACTIVE_HIGH. Expect hardware unavailable, no output action, and valid addressed command rejected with hardware_not_ready.

## Not yet executable as a normative test
- schedule behavior: V1 names the topic but does not define its payload or execution semantics.
- detailed offline/local policy: not defined by the current V1 contract.
- production TLS/device credential provisioning: required by V1 for final production but credentials/certificate lifecycle are not yet specified.

Those items require a LEAD contract extension before implementation.


## Recette materielle non contractuelle KS5009

Le KS5009 est un banc de simulation. Avant de marquer une capacite comme validee dans `.project/EQUIPMENT_CAPABILITY_MATRIX.md`:

1. demarrer sans appui: un appui court sur le bouton partage ne doit pas entrer en provisioning;
2. maintenir le bouton de provisioning au moins 3 s au boot: le portail local doit demarrer;
3. configurer le Wi-Fi localement, redemarrer et verifier la reconnexion Wi-Fi/MQTT;
4. verifier chaque capteur/actionneur via le diagnostic serie sans publier de champ hors IOT_V1;
5. couper/reprendre Wi-Fi puis broker et verifier heartbeat/state apres reconnexion;
6. redemarrer l'ESP32 et verifier la politique de recovery configuree;
7. rejouer un command_id deja execute et verifier qu'aucune action physique n'est rejouee.

Les capteurs gaz et eau/pluie du prototype servent uniquement a simuler des evenements. Ce test ne constitue aucune validation de securite certifiee.
