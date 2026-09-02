# HydroDozerPump MQTT Boot Loop Troubleshooting

Date: 2026-02-16

## Symptom
- Device repeatedly reboots shortly after connecting to MQTT.
- Serial may show reboot/factory-reset command handling right before restart.

## Root cause (most common)
- A dangerous command is present as a retained MQTT message on `hydrodozerpump/<node>/cmd`.
- After each reconnect, the broker re-delivers the retained command, causing another reboot/reset.

## Firmware protections in place
- Hardware reset jumper check is skipped on software restarts to avoid false factory-reset loops.
- MQTT dangerous command boot guard:
  - For first 60s after boot, MQTT commands `reboot` and `factory_reset` are rejected.
  - Applies to both JSON actions and legacy string commands.
  - ACK detail returned: `boot_guard`.

## Broker-side prevention rules (required)
- Never publish retained commands to `hydrodozerpump/<node>/cmd`.
- In Home Assistant/service automations, explicitly set retain to `false`.
- If retained command was previously sent, clear it once:
  - Publish empty payload with retain=true on `hydrodozerpump/<node>/cmd`.

## Recovery checklist
1. Power-cycle device and watch serial output.
2. Clear retained payload on command topic.
3. Confirm command publishers use retain=false.
4. Send a safe test command (`{"action":"stop_all"}`) and verify ACK.
5. Test `{"action":"reboot"}` only after uptime > 60s.

## Expected ACK behavior
- During first 60s:
  - `{"action":"reboot"}` -> `{"ok":false,"type":"action_reboot","detail":"boot_guard",...}`
  - `{"action":"factory_reset"}` -> `{"ok":false,"type":"action_factory_reset","detail":"boot_guard",...}`
- After 60s:
  - Commands are accepted normally.
