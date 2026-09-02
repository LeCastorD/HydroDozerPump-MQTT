# HydroDozerPump Dev Release Notes

Date: 2026-02-16

## Included changes
- JSON command handling hardened with strict type/range validation.
- `set_name` is string-only validation.
- `/stored` page improved with parsed sections and pretty JSON view.
- Calibration implemented as 2-step workflow:
  1. fixed 60s run start
  2. measured volume submit
- System Settings page added:
  - networking hostname
  - DHCP/manual network mode with IP/gateway/netmask/DNS
  - MQTT broker/port/client/user/pass
  - OTA user/pass
  - WiFiManager trigger link
- Runtime config persistence extended in `/config.json` under `system`.
- Setup order fixed so config loads before WiFi/MQTT/OTA init.
- Permission workflow hardened with centralized checks.
- Pump-scoped permission topic added:
  - `hydrodozerpump/pump/%u/permission`
- Command ACK/NACK topic added:
  - `hydrodozerpump/cmd/ack`
- Health and heartbeat telemetry expanded:
  - `min_heap`
  - `mqtt_reconnects`
- Hardware emergency factory reset added:
  - Boot-time jumper on `D5` to `GND` held ~3s
  - Erases WiFi credentials and formats LittleFS
- Factory reset paths unified through shared helper:
  - MQTT and Web UI factory-reset actions format LittleFS
  - The separate WiFiManager reset action keeps its WiFi-only behavior
- MQTT reboot-loop hardening:
  - Dangerous MQTT commands (`reboot`, `factory_reset`) blocked for first 60s after boot
  - ACK detail `boot_guard` returned when blocked
  - Added troubleshooting doc: `docs/mqtt-boot-loop-troubleshooting.md`
- MQTT + Home Assistant auto-publish now functional:
  - New retained state topic: `hydrodozerpump/auto/state` (30s refresh + publish on reconnect)
  - Auto-published fields: all pumps stopped, bottle capacity/percent, network mode, WiFi RSSI, IP, mDNS
  - Home Assistant MQTT discovery configs are now published automatically on MQTT connect

## Compatibility
- Legacy plain-text commands remain supported.
- Existing bottle permission topics remain published when bottle mapping exists.

## Operational notes
- Hostname and manual network changes apply after reboot.
- Invalid manual network values fall back to DHCP at boot.
