# HydroDozerPump MQTT Contract v1

Topic namespace base: `hydrodozerpump/<node>/`

`<node>` is the saved device name after MQTT-safe normalization. On a newly flashed device, the firmware generates a name such as `hydrodozerpump-ab12`; the corresponding namespace is `hydrodozerpump/hydrodozerpump-ab12/`.

## 1. Command input topic
- Topic: `hydrodozerpump/<node>/cmd`
- Retain: `false`

### JSON command examples
```json
{"cmd":"dose","pump":1,"ml":5}
{"cmd":"calibrate","pump":1}
{"cmd":"calibrate","pump":1,"measured_ml":54.2}

{"action":"start","pump":1,"duration":5000}
{"action":"stop","pump":1}
{"action":"stop_all"}
{"action":"reboot"}
{"action":"factory_reset"}

{"bottle":1,"set_name":"Nutrient A"}
{"bottle":1,"set_capacity":1500}
{"bottle":1,"refill":true}

{"pump":1,"set_name":"Pump A"}
{"pump":1,"set_bottle_id":2}
{"pump":1,"set_flow_60s":60}
```

### Legacy string commands (compatibility)
- `pump1_on`
- `pump1_off`
- `pump2_on`
- `pump2_off`
- `stop_all`
- `reboot`
- `factory_reset`

## 2. Command ACK/NACK
- Topic: `hydrodozerpump/<node>/cmd/ack`
- Retain: `false`

Example payload:
```json
{"ok":true,"type":"dose","detail":"started","uptime_s":842}
```

Common `type` values:
- `json`
- `bottle_cfg`
- `pump_cfg`
- `cmd`
- `dose`
- `calibrate`
- `action_start`
- `action_stop`
- `action_stop_all`
- `action_reboot`
- `action_factory_reset`
- `legacy_*`

Common `detail` values:
- success: `started`, `stopped`, `updated`, `applied`, `run_started`, `ok`
- errors: `invalid_json`, `unknown_cmd`, `unknown_action`, `invalid_payload`, `invalid_pump_range`, `no_pending_run`, `flow_out_of_range`, `boot_guard`

## 3. Permission topics
- Pump permission:
  - `hydrodozerpump/<node>/pump/1/permission`
  - `hydrodozerpump/<node>/pump/2/permission`
- Bottle permission:
  - `hydrodozerpump/<node>/bottle/1/permission`
  - `hydrodozerpump/<node>/bottle/2/permission`

Permission payload example:
```json
{"pump":1,"bottle":1,"ok":false,"requested_ml":5.0,"remaining_ml":2.1,"percent":21.0,"reason":"not_enough_liquid"}
```

Permission reasons:
- `ok`
- `invalid_pump`
- `invalid_ml`
- `invalid_flow`
- `duration_limit`
- `no_bottle`
- `not_enough_liquid`

## 4. State and telemetry topics
- Availability: `hydrodozerpump/<node>/status`
  - values: `online` / `offline`
  - retained: `true`
- Health: `hydrodozerpump/<node>/health`
  - retained: `true`
  - example:
  ```json
  {"heap":33240,"min_heap":31888,"rssi":-61,"mqtt_reconnects":3}
  ```
- Automatic state: `hydrodozerpump/<node>/auto/state`
  - retained: `true`
  - contains pump state, bottle metrics, scheduler settings, alarms, and network diagnostics
- System state: `hydrodozerpump/<node>/system/state`
  - retained: `true`
  - contains static network, MQTT, and OTA configuration metadata without password values
- Bottle state:
  - `hydrodozerpump/<node>/bottle/1/state`
  - `hydrodozerpump/<node>/bottle/2/state`
  - retained: `true`

## 5. Notes
- Calibration is 2-step:
  1. Start run: `{"cmd":"calibrate","pump":1}`
  2. Submit measured volume after run: `{"cmd":"calibrate","pump":1,"measured_ml":54.2}`
- `action.start` duration is in milliseconds.
- Never publish retained commands on `hydrodozerpump/<node>/cmd`.
- MQTT dangerous commands (`reboot`, `factory_reset`) are blocked during first 60s after boot and ACK with `detail=boot_guard`.
- MQTT `factory_reset` (JSON or legacy string) erases WiFi credentials and formats LittleFS.
- Hardware emergency reset: short `D5` to `GND` at boot for ~3s.
  - This also erases WiFi credentials and formats LittleFS.
- Both reset paths remove `/config.json`, `/pump_runs.csv`, and the LittleFS-hosted Web UI files. Re-upload the LittleFS image before returning to the Web UI.
- Boot-loop troubleshooting guide: `docs/mqtt-boot-loop-troubleshooting.md`
