# Security and Credentials

## Change the default credentials before deployment

HydroDozerPump includes initial credentials so that a newly flashed controller can be configured. These values are public and must not be used on a device connected to a trusted network or MQTT broker.

| Service | Default username | Default password | Where to change it |
| --- | --- | --- | --- |
| Web UI and OTA update | `admin` | `adminpass` | Web UI: **Settings** > **OTA** |
| MQTT broker | `homeassistant` | `homeassistantpass` | Web UI: **Settings** > **MQTT** |

After changing the Web UI/OTA credentials, use the new values for both the Web UI login and the `/update` OTA firmware page.

Use unique, strong passwords and a dedicated MQTT account limited to this device's topics. Do not expose the Web UI or MQTT broker directly to the public internet. If you reset the device to factory settings, repeat this process before returning it to service.
