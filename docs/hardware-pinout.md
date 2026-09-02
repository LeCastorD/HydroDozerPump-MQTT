# HydroDozerPump Hardware Pinout

Board target: Wemos D1 Mini (ESP8266)

## Pin assignments

| Controller pin | ESP8266 GPIO | Firmware role | Connection |
| --- | ---: | --- | --- |
| `D1` | `GPIO5` | Pump A control output | External pump-driver input 1 |
| `D2` | `GPIO4` | Pump B control output | External pump-driver input 2 |
| `D6` | `GPIO12` | Green status-light output | ULN2003 input for the green status light; active-high PWM |
| `D7` | `GPIO13` | Red status-light output | ULN2003 input for the red status light; active-high PWM |
| `D5` | `GPIO14` | Factory-reset input | Jumper or switch to `GND` for recovery |
| `GND` | - | Signal reference | Common ground with ULN2003 board, pumps, status lights, and load supply |

## Pump driver and power

The ESP8266 GPIO pins are control signals only. Do not connect a pump or status light directly to `D1`, `D2`, `D6`, or `D7`. This project uses two Kamoer peristaltic dosing pumps and green/red status lights controlled by a ULN2003 driver board; confirm the board and every connected load's electrical ratings are compatible.

<p align="center"><img src="ULN2003_Driver.jpg" alt="ULN2003 driver board" width="280"><br><em>ULN2003 driver board reference</em></p>

The firmware currently assumes active-high outputs for the ULN2003 inputs:

- `HIGH` = pump on
- `LOW` = pump off

`D6` and `D7` use active-high PWM for the green and red status lights. The firmware turns the green light on at startup, pulses it while a pump runs, and alternates green/red when dosing is paused or a bottle is empty. The red light indicates a low-bottle alarm during normal operation.

If the installed pump or light wiring is inverted, change `PUMP_ACTIVE_HIGH` or `STATUS_LED_ACTIVE_HIGH` in `src/HydroDozerPump/main.cpp` as applicable. Test all channels with the pump lines disconnected from the nutrient bottles.

Connect the ESP8266 ground, ULN2003 ground, and load power-supply return together. The driver must provide appropriate flyback protection for the load. Keep low-voltage control wires separate from high-current pump wiring where practical and provide strain relief at the enclosure.

## Emergency recovery jumper (`D5`)

`D5` is configured with `INPUT_PULLUP`. To perform a hardware factory reset:

1. Power off the controller.
2. Short `D5` to `GND` with the recovery jumper or switch.
3. Power on and hold the connection for at least three seconds.
4. Remove the jumper and reflash the LittleFS image before returning to the Web UI.

The reset erases Wi-Fi credentials and formats LittleFS. It removes the saved configuration, pump log, and Web UI files. Back up `config.json` and `pump_runs.csv` before using this recovery path whenever possible.

## Caution

- Do not use ESP8266 boot-strap pins for critical outputs or inputs unless the startup behavior is fully understood.
- `D5` is a maintenance/recovery input, not a routine operating control.
- Confirm polarity, supply voltage, and driver compatibility before applying power to the pumps or status lights.
