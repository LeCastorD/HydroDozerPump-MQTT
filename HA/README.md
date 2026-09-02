# Home Assistant Dashboard Template

[`hydrodozerpump-dashboard.yaml`](hydrodozerpump-dashboard.yaml) is a Lovelace dashboard example for one HydroDozerPump device.

The template uses the placeholder device identity `hydrodozerpump_example`. Replace every occurrence with the entity-ID prefix created by Home Assistant for the actual device after MQTT discovery. For example, a device named `hydrodozerpump-ab12` may create entities beginning with `hydrodozerpump_ab12`.

Import the YAML into a manual Home Assistant dashboard only after confirming the discovered entity IDs. Entity names can differ when the device name or Home Assistant discovery identifiers change.
