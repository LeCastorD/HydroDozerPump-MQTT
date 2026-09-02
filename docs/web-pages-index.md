# Web Pages Index (Hierarchy)

Source: `src/HydroDozerPump/main.cpp` (`setupWebServer()` + `setupOTA()`).

## Navigation Tree

```text
/  (Home / Management)
|- /calibration                      (Pump calibration page)
|  |- POST /calibration/start        (Start fixed 60s calibration run)
|  `- POST /calibration/submit       (Submit measured mL and apply flow)
|- /mqtt_send                        (Safe MQTT command sender page)
|  `- POST /mqtt_send_do             (Validate + publish HA command payload)
|- /settings                         (System Settings hub)
|  |- /settings/network              (Networking page)
|  |  |- POST /settings/network_save (Save networking settings)
|  |  `- /wifi_manager               (Open WiFiManager confirmation)
|  |     `- /wifi_manager_do         (Erase WiFi + reboot to setup portal)
|  |- /settings/time                 (Time synchronization page)
|  |  `- POST /settings/time_save    (Apply timezone)
|  |- /settings/mqtt                 (MQTT settings page)
|  |  `- POST /settings/mqtt_save    (Save MQTT settings)
|  |- /settings/ota                  (OTA settings page)
|  |  `- POST /settings/ota_save     (Save OTA credentials)
|  |- /schedule_clear                (Clear scheduler execution history, redirect)
|  |- /ha_discovery_reset            (Reset HA Discovery action/result)
|  `- /factory                       (Factory reset warning page)
|     `- /factory_confirm            (Factory reset action page)
|- /pump_runs_view                   (Pump logs view page)
|  `- /pump_runs_download            (CSV download)
|- /update                           (OTA Firmware Update page, provided by ElegantOTA)
`- /reboot                           (Reboot confirmation)
   `- /reboot_do                     (Reboot action page)
```

## Endpoints by Method

- `GET /` -> Home page with main management actions.
- `GET /calibration` -> Calibration UI for Pump A/B.
- `POST /calibration/start` -> Starts calibration run for selected pump.
- `POST /calibration/submit` -> Saves calibrated flow from measured volume.
- `GET /mqtt_send` -> Safe MQTT command sender UI with template dropdown.
- `POST /mqtt_send_do` -> Validates payload and publishes to `hydrodozerpump/ha/cmd`.
- `GET /settings` -> Settings hub page.
- `GET /settings/network` -> Network settings page.
- `POST /settings/network_save` -> Saves network settings, then redirects.
- `GET /settings/time` -> Time synchronization page.
- `POST /settings/time_save` -> Applies timezone, then redirects.
- `GET /settings/mqtt` -> MQTT settings page.
- `POST /settings/mqtt_save` -> Saves MQTT settings, then redirects.
- `GET /settings/ota` -> OTA settings page.
- `POST /settings/ota_save` -> Saves OTA settings, then redirects.
- `GET /schedule_clear` -> Clears scheduler execution history and redirects to `/settings`.
- `GET /wifi_manager` -> WiFiManager warning/confirmation page.
- `GET /wifi_manager_do` -> Triggers WiFi credential reset + reboot.
- `GET /ha_discovery_reset` -> Sends HA discovery reset (if MQTT connected).
- `GET /pump_runs_view` -> Displays pump log status and log content.
- `GET /pump_runs_download` -> Downloads pump log CSV file.
- `GET /update` -> OTA web updater UI (mounted by `ElegantOTA.begin(&server, ...)`).
- `GET /reboot` -> Reboot confirmation page.
- `GET /reboot_do` -> Reboot action page.
- `GET /factory` -> Factory reset warning page.
- `GET /factory_confirm` -> Factory reset action page.
