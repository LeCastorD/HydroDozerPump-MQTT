# HydroDozerPump Commissioning Checklist

## 0. Default credentials and connection values
- These are public initial setup values, not secure deployment credentials. Change both the Web UI/OTA and MQTT credentials before connecting the controller to a trusted network or broker. See [Security and Credentials](security.md).
- Web UI login:
  - Username: `admin`
  - Password: `adminpass`
- OTA update login:
  - Username: `admin`
  - Password: `adminpass`
- MQTT defaults in firmware:
  - Broker host: `127.0.0.1`
  - Port: `1883`
  - Username: `homeassistant`
  - Password: `homeassistantpass`

## 1. Change credentials
- In **Settings** > **OTA**, replace the Web UI/OTA username and password with unique credentials.
- In **Settings** > **MQTT**, replace the MQTT username and password with a dedicated broker account.
- Verify the Web UI and `/update` page accept the new Web UI/OTA credentials.
- Verify the controller reconnects to MQTT using the new MQTT credentials.

## 2. Power and wiring
- Verify common ground between ESP8266 and ULN2003.
- Verify pump channels match firmware pins (`D1`, `D2`).
- Power device with pumps disconnected first.

## 3. Network bring-up
- Connect to WiFi via WiFiManager portal.
- Open root page and verify IP, RSSI, and heap visible.
- Confirm hostname resolves (mDNS) if used.

## 4. MQTT connectivity
- Confirm `hydrodozerpump/<node>/status` publishes `online`, using the current device node.
- Confirm command subscription by sending `stop_all`.
- Confirm ACK messages on `hydrodozerpump/<node>/cmd/ack`.

## 5. Bottle and pump config
- Set bottle names and capacities.
- Set pump-to-bottle mapping.
- Set initial flow with `set_flow_60s` if needed.
- Confirm bottle states publish correctly.

## 6. Calibration (per pump)
- Prime tubing to remove air.
- Use production liquid (same viscosity as real use).
- Start calibration run:
  - `{"cmd":"calibrate","pump":1}`
- Measure dispensed volume with a graduated container.
- Submit result:
  - `{"cmd":"calibrate","pump":1,"measured_ml":54.2}`
- Repeat 2-3 times and keep average.

## 7. Dose validation
- Send small test dose:
  - `{"cmd":"dose","pump":1,"ml":5}`
- Verify permission payload and actual dispensed volume.
- Verify remaining bottle volume decreases as expected.

## 8. Safety checks
- Verify `stop_all` always works.
- Verify hard timeout stops pump.
- Verify empty bottle denies dose with clear reason.

## 9. Resilience checks
- Restart broker and verify auto-reconnect.
- Bounce WiFi and verify reconnect.
- Confirm `mqtt_reconnects` increases after recovery.
- Confirm `min_heap` remains stable over time.

## 10. Recovery checks
- Software reset path:
  - Send `{"action":"factory_reset"}` or `factory_reset` on `hydrodozerpump/<node>/cmd`.
  - Verify WiFi credentials are erased and setup portal is available.
  - Verify local filesystem is wiped (config and logs removed).
- Hardware emergency path:
  - Power off device.
  - Place jumper between `D5` and `GND`.
  - Power on and hold jumper for at least 3 seconds.
  - Verify WiFi credentials are erased and local filesystem is wiped.
  - Remove jumper and reboot.
