# HydroDozerPump Hardware BOM

<p>
  This bill of materials lists the electronics, pumps, connectors, assembly hardware, and printable parts used by the HydroDozerPump project. Quantities are for one two-pump controller unless noted otherwise. Confirm electrical ratings, connector types, screw sizes, and printed-part fit against the exact components used in the build.
</p>

<h2 id="component-grid">Components</h2>

<table>
  <tr>
    <td valign="top" width="33%">
      <h3>Wemos D1 Mini</h3>
      <p><strong>Quantity:</strong> 1</p>
      <p>ESP8266 controller running the dosing firmware, Web UI, Wi-Fi, MQTT, and Home Assistant discovery.</p>
      <p align="center"><img src="wemos-d1-mini.jpeg" alt="Wemos D1 Mini ESP8266 controller board" width="180"></p>
    </td>
    <td valign="top" width="33%">
      <h3>ULN2003 driver board</h3>
      <p><strong>Quantity:</strong> 1</p>
      <p>Switches the two pump loads and green/red status lights from the ESP8266 control outputs.</p>
      <p align="center"><img src="ULN2003_Driver.jpg" alt="ULN2003 driver board" width="180"></p>
    </td>
    <td valign="top" width="33%">
      <h3>Kamoer dosing pump</h3>
      <p><strong>Quantity:</strong> 2</p>
      <p>Peristaltic pumps that dispense nutrient Part A and Part B. Calibrate each installed pump separately.</p>
      <p align="center"><img src="Kamoer-DoserPump.jpg" alt="Kamoer peristaltic dosing pump with tubing" width="180"></p>
    </td>
  </tr>
  <tr>
    <td valign="top">
      <h3>12 V indicator lights</h3>
      <p><strong>Quantity:</strong> 1 green and 1 red</p>
      <p>LMNIL-775P-12V-G and LMNIL-775P-12V-R panel-mount lights for controller status indication.</p>
      <p align="center"><img src="LMNIL-775P-12V-G-R.jpg" alt="Green and red LMNIL-775P-12V panel-mount indicator lights" width="180"></p>
    </td>
    <td valign="top">
      <h3>Buck converter</h3>
      <p><strong>Quantity:</strong> 1</p>
      <p>Adjustable DC converter used to provide regulated 5 V for the Wemos. Verify the input range and output setting before connection.</p>
      <p align="center"><img src="buck-converter.jpeg" alt="Adjustable DC buck converter" width="180"></p>
    </td>
    <td valign="top">
      <h3>Lever wire connectors</h3>
      <p><strong>Quantity:</strong> As required</p>
      <p>Serviceable power-distribution and load wiring connectors. Select connectors rated for the installed pump current.</p>
      <p align="center"><img src="Lever_wire_connectors.jpeg" alt="Three-position lever wire connector" width="180"></p>
    </td>
  </tr>
  <tr>
    <td valign="top">
      <h3>JST-XH connector kit</h3>
      <p><strong>Quantity:</strong> 1 kit</p>
      <p>Housings and contacts for controller, pump, and service harnesses. Confirm the chosen pitch and pin count before ordering.</p>
      <p align="center"><img src="connectors.jpeg" alt="Connector housings, crimp terminals, and prewired leads" width="180"></p>
    </td>
    <td valign="top">
      <h3>Prewired connector cables</h3>
      <p><strong>Quantity:</strong> As required</p>
      <p>Prewired and crimped leads for serviceable low-voltage control connections.</p>
      <p align="center"><img src="connectors.jpeg" alt="Prewired connector cables and crimp terminals" width="180"></p>
    </td>
    <td valign="top">
      <h3>Crimping tool</h3>
      <p><strong>Quantity:</strong> 1</p>
      <p>Crimping tool suitable for the selected connector terminals.</p>
      <p align="center"><img src="Crimper.jpeg" alt="Crimping tool for small connector terminals" width="180"></p>
    </td>
  </tr>
  <tr>
    <td valign="top">
      <h3>1000 uF, 25 V electrolytic capacitor</h3>
      <p><strong>Quantity:</strong> 1</p>
      <p>Polarized bulk decoupling capacitor for the low-voltage supply rail. Observe polarity and do not exceed the 25 V rating.</p>
      <p align="center"><img src="Capacitor.jpeg" alt="1000 microfarad 25 volt electrolytic capacitor" width="180"></p>
    </td>
    <td valign="top">
      <h3>12 V pump power supply</h3>
      <p><strong>Quantity:</strong> 1</p>
      <p><strong>Model:</strong> <em>TBD</em></p>
      <p>Supply for the pumps, indicator lights, and buck-converter input. Size the current capacity for both pumps and all connected loads.</p>
      <p><em>Reference image placeholder.</em></p>
    </td>
    <td valign="top">
      <h3>Factory-reset jumper or switch</h3>
      <p><strong>Quantity:</strong> 1</p>
      <p>Connects D5 to GND during boot for recovery. Hold for at least three seconds to trigger the factory reset.</p>
      <p><em>Reference image placeholder.</em></p>
    </td>
  </tr>
  <tr>
    <td valign="top" colspan="3">
      <h3>Printable enclosure parts</h3>
      <p><strong>Quantity:</strong> 1 set</p>
      <p>Print the enclosure box, populated logic mounting plate, and pump mounting plate. Confirm the fit against the selected components before final assembly.</p>
      <p align="center"><img src="Enclosure_box.jpg" alt="HydroDozerPump printed enclosure box and lid preview" width="180"><br><em>Enclosure box and lid</em></p>
      <p align="center"><img src="Mounting_Logic_plate.jpeg" alt="HydroDozerPump logic mounting plate" width="180"><br><em>Logic mounting plate</em></p>
      <p align="center"><img src="Mounting_Pump_Plate.jpeg" alt="HydroDozerPump pump mounting plate" width="180"><br><em>Pump mounting plate</em></p>
      <p><a href="../Enclosure%20and%20mount/Enclosure_Box.3mf">Enclosure_Box.3mf</a><br><a href="../Enclosure%20and%20mount/Mounting_Logic_plate.3mf">Mounting_Logic_plate.3mf</a><br><a href="../Enclosure%20and%20mount/Mounting_Pump_Plate.3mf">Mounting_Pump_Plate.3mf</a></p>
    </td>
  </tr>
</table>

<h2 id="assembly-hardware">Assembly hardware</h2>

<table>
  <thead>
    <tr>
      <th>Hardware</th>
      <th>Quantity</th>
      <th>Use</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>Fasteners and standoffs</td>
      <td><em>TBD</em></td>
      <td>Secure the controller, driver board, pumps, mounting plates, and enclosure panels. Confirm dimensions against the printed parts before ordering.</td>
    </tr>
    <tr>
      <td>Cable ties</td>
      <td>As required</td>
      <td>Harness management and strain relief inside the enclosure.</td>
    </tr>
    <tr>
      <td>Tubing and bottle fittings</td>
      <td><em>TBD</em></td>
      <td>Connect the Kamoer pumps to the nutrient bottles and dosing lines. Match tubing to the installed pump heads and liquids.</td>
    </tr>
    <tr>
      <td>Low-voltage hookup wire</td>
      <td>As required</td>
      <td>Controller, driver, status-light, pump, and power wiring.</td>
    </tr>
  </tbody>
</table>

<h2 id="component-images">Component images</h2>

<p>
  The component cards above contain the available reference images. The image files remain in this folder so they can be reused by the assembly and wiring documentation.
</p>

<h2 id="printable-parts-and-credits">Printable parts and credits</h2>

<table>
  <thead>
    <tr>
      <th>Part or holder</th>
      <th>Source</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>Wemos D1 Mini board holder</td>
      <td><a href="https://www.printables.com/model/48425-wemos-d1-mini-frame-module-for-enclosures-openscad">Printables model 48425</a></td>
    </tr>
    <tr>
      <td>Capacitor holder</td>
      <td><a href="https://www.thingiverse.com/thing:4089178">Thingiverse model 4089178</a></td>
    </tr>
    <tr>
      <td>Generic lever wire connector holder</td>
      <td><a href="https://www.printables.com/model/170622-generic-lever-wire-connector-holder">Printables model 170622</a></td>
    </tr>
    <tr>
      <td>Cable tie holder</td>
      <td><a href="https://github.com/pfliegster/cable-tie-holders">pfliegster/cable-tie-holders</a></td>
    </tr>
    <tr>
      <td>Standoff</td>
      <td><a href="https://www.thingiverse.com/thing:351092">Thingiverse model 351092</a></td>
    </tr>
    <tr>
      <td>Enclosure box</td>
      <td><a href="https://www.printables.com/model/72839-customizable-parametric-stable-and-waterproof-elec">Printables enclosure model 72839</a></td>
    </tr>
  </tbody>
</table>

<h2 id="power-and-wiring-note">Power and wiring note</h2>

<p>
  The power path is <strong>12 V supply &rarr; pumps, status lights, and buck-converter input &rarr; regulated 5 V to the Wemos</strong>. The ESP8266 GPIO pins control the ULN2003 only; they must not power pumps or indicator lights directly. Connect the Wemos, ULN2003, and load power-supply returns to a common ground. See the <a href="hardware-pinout.md">hardware pinout</a> and the <a href="../README.md#wiring">Wiring</a> section for the connection details.
</p>
