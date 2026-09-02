# HydroDozerPump Bill of Materials

This bill of materials lists the hardware represented by the firmware and enclosure files. Quantities are for one two-pump controller. Confirm the electrical ratings, connector types, screw sizes, and printed-part fit against the exact components used in the build.

## Controller and pump control

| Item | Quantity | Purpose | Notes |
| --- | ---: | --- | --- |
| Wemos D1 Mini or compatible ESP8266 board | 1 | Runs the firmware, Web UI, Wi-Fi, and MQTT client | The PlatformIO target is `d1_mini`. |
| ULN2003 driver board | 1 | Switches the pump and status-light loads from the ESP8266 | Uses control inputs from `D1`, `D2`, `D6`, and `D7`. Confirm the board and selected load electrical ratings are compatible. |
| Kamoer peristaltic dosing pump | 2 | Dispenses nutrient Part A and Part B | Calibrate each installed pump separately. |
| LMNIL-775P-12V-G and LMNIL-775P-12V-R panel-mount indicator lights | 1 each | Green and red controller status indication | 12 V models; connect through the ULN2003 outputs controlled by `D6` and `D7`, respectively. Observe the marked polarity. |
| Pump power supply | 1 | Powers the selected pumps and controller power path | Select voltage and current capacity for the actual pump models. |
| Factory-reset jumper or momentary switch | 1 | Grounds `D5` during boot for recovery | Hold for at least three seconds to trigger reset. |

## Wiring and assembly

| Item | Quantity | Purpose | Notes |
| --- | ---: | --- | --- |
| Low-voltage hookup wire | As required | Controller, driver, pump, and power wiring | Keep control wiring tidy and strain-relieved. |
| Connectors and terminals | As required | Serviceable power and pump connections | Choose connectors rated for the installed pump current. |
| Enclosure | 1 | Protects the controller and pump wiring | Use the 3MF files in `Enclosure and mount/` as the current design basis. |
| Mounting hardware | As required | Secures boards, pumps, and enclosure parts | Confirm the required screw and standoff dimensions before printing. |

## Printable parts

- [`Enclosure_Box.3mf`](../Enclosure%20and%20mount/Enclosure_Box.3mf)
- [`Mounting_Llogic_plate.3mf`](../Enclosure%20and%20mount/Mounting_Llogic_plate.3mf)
- [`Mounting_Pump_Plate.3mf`](../Enclosure%20and%20mount/Mounting_Pump_Plate.3mf)

<table>
  <tr>
    <td align="center"><img src="Full_Assembly.jpeg" alt="Complete HydroDozerPump assembly with enclosure, two Kamoer pumps, and nutrient bottles" width="100%"><br><em>Complete HydroDozerPump assembly reference</em></td>
    <td align="center"><img src="Completed_Assembly_side_view.jpeg" alt="Side view of the completed HydroDozerPump enclosure with green and red status lights" width="100%"><br><em>Completed enclosure side view</em></td>
  </tr>
</table>

<p align="center"><img src="Mounting_Logic_plate.jpeg" alt="Populated HydroDozerPump logic mounting plate with controller, ULN2003 board, and power terminals" width="100%"><br><em>Populated logic mounting plate reference</em></p>

<p align="center"><img src="Mounting_Pump_Plate.jpeg" alt="HydroDozerPump pump mounting plate and printed pump brackets" width="100%"><br><em>Pump mounting plate and printed bracket reference</em></p>

<p align="center"><img src="Assembly.jpeg" alt="HydroDozerPump enclosure assembly with two Kamoer pumps, mounting plates, terminals, and tubing connections" width="100%"><br><em>Enclosure assembly reference</em></p>

## Component reference photos

<table>
  <tr>
    <td align="center"><img src="wemos-d1-mini.jpeg" alt="Wemos D1 Mini ESP8266 controller board" width="180"><br><em>Wemos D1 Mini</em></td>
    <td align="center"><img src="buck-converter.jpeg" alt="Compact adjustable DC buck converter" width="180"><br><em>Example buck converter</em></td>
    <td align="center"><img src="Lever_wire_connectors.jpeg" alt="Three-position lever wire connector" width="180"><br><em>Lever wire connector</em></td>
  </tr>
  <tr>
    <td align="center"><img src="connectors.jpeg" alt="Connector housings, crimp terminals, and prewired leads" width="180"><br><em>Connector kit</em></td>
    <td align="center"><img src="Crimper.jpeg" alt="Crimping tool for small connector terminals" width="180"><br><em>Crimping tool</em></td>
    <td align="center"><img src="buck-converter-back.jpeg" alt="Rear view of an example buck converter board" width="180"><br><em>Example board layout</em></td>
  </tr>
  <tr>
    <td align="center"><img src="ULN2003_Driver.jpg" alt="ULN2003 driver board" width="180"><br><em>ULN2003 driver board</em></td>
    <td align="center" colspan="2"><img src="Kamoer-DoserPump.jpg" alt="Kamoer peristaltic dosing pump with tubing" width="240"><br><em>Kamoer peristaltic dosing pump</em></td>
  </tr>
  <tr>
    <td align="center" colspan="3"><img src="LMNIL-775P-12V-G-R.jpg" alt="LMNIL-775P-12V green and red panel-mount indicator lights" width="280"><br><em>LMNIL-775P-12V-G / LMNIL-775P-12V-R 12 V status lights</em></td>
  </tr>
</table>

The final wiring drawing is still needed before that section can be illustrated accurately.
