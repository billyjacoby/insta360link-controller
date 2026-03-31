# ESP32 Insta-360 Link Controller Hardware
Now that I've implemented a system daemon for controlling an Insta360 Link and the required ESP32 firmware, I'd like to make a custom PCB to make a handheld controller for the system. The breadboard just isn't cutting it. 

## Key Features
[] 3-axis joystick with button for pan, tilt, zoom, and home
[] On/Off button or switch
[] Calibrate button
[] Some sort of small display for battery %, connection status, etc.
[] Runs off of one 18650 cell
[] Recharge/reflash via USB-C connector
[] Indicator LEDs?
## Pin Assignments

| Function | GPIO | Notes |
|---|---|---|
| Joystick X (pan) | 34 | ADC1, input only |
| Joystick Y (tilt) | 35 | ADC1, input only |
| Joystick button (home) | 32 | ADC1, internal pull-up |
| Zoom potentiometer | 33 | ADC1, adjacent to joystick pins |
| I2C SDA (OLED) | 21 | Hardware I2C default |
| I2C SCL (OLED) | 22 | Hardware I2C default |
| Calibrate button | 25 | Internal pull-up, active low |
| Spare button | 26 | Internal pull-up, active low |
| Status LED | 2 | On-board LED |

**Constraints:** ADC1 pins only for analog inputs (ADC2 conflicts with WiFi). GPIO 0/1/3/6-11/12/15 reserved (boot/flash/UART/strapping).

## Required Subsystems

### Inputs
- 3-axis joystick: 2 analog axes (pan/tilt) + integrated button (home/center)
- Zoom potentiometer: separate analog axis
- Calibrate button: momentary, active low
- Spare button: momentary, active low
- On/off switch
- USB-C connector (charging + serial flash)

### Outputs
- I2C OLED display (SSD1306 or SH1106, 128x64)
- Status LED (on-board GPIO 2)

### MCU
ESP32-WROOM-32 + CP2102N (USB-UART bridge)

### Power
- Battery: Jauch LI INR18650JD-30P (3.6V, 3000mAh, 1A charge rate)
- Charger + power path: [BQ25185DLHR](https://www.digikey.com/en/products/detail/texas-instruments/BQ25185DLHR/21769368) (1A CC/CV, UV/OV/OC/SC/OT protection)
- Fuel gauge: [MAX17048G+T10](https://www.digikey.com/en/products/detail/analog-devices-inc-maxim-integrated/MAX17048G-T10/3758921) (I2C, ModelGauge, no sense resistor)
- 3.3V LDO: [AP2112K-3.3TRG1](https://www.digikey.com/en/products/detail/diodes-incorporated/AP2112K-3-3TRG1/4470746) (600mA, SOT-23-5)
- 5V boost: [TPS61023DRLR](https://www.digikey.com/en/products/detail/texas-instruments/TPS61023DRLR/11310629) (fixed 5V, SOT-563, pots only)
- Hard on/off switch between SYS output and regulators
- USB-C for charging + serial flash (CP2102N)
- Topology: parallel (LDO from battery for 3.3V, boost for 5V)