# Power Architecture Design

## Overview

Power subsystem for the ESP32-based Insta360 Link handheld controller. Runs on a single 18650 Li-Ion cell with USB-C charging, true power path (system operates while charging or without a battery), and hardware battery protections including undervoltage lockout.

## Requirements

- Single 18650 cell: Jauch LI INR18650JD-30P (3.0-4.2V, 3000mAh, 0.5C standard charge)
- 1A charge rate (0.33C, well within cell's 0.5C standard / 6A max)
- USB-C power passthrough (system runs from USB while battery charges)
- Works with no battery installed (bench testing)
- 3.3V rail: ESP32, OLED, fuel gauge (~110mA typical)
- 5V rail: joystick/zoom potentiometers only (~1.5mA)
- Hard on/off switch (zero quiescent to regulators when off)
- Battery state-of-charge displayed on OLED

## Topology: Parallel

Two independent regulators fed from the charger's SYS output through a hard switch:

- **3.3V LDO** directly from battery/USB (efficient — only 0.3V dropout from 3.6V nominal)
- **5V boost** separately for the three 10K potentiometers (1.5mA total load)

This is more efficient than a series topology (boost to 5V then LDO to 3.3V) because the LDO wastes ~33mW vs ~187mW. The boost converter only needs to handle 1.5mA instead of the full system current.

## Block Diagram

```
                                ┌─────────────────┐
     USB-C ──────────────┬──────┤  BQ25185DLHR     │
     (VBUS 5V)           │      │  Charger IC       │
                         │      │  1A CC/CV          │
                    ┌────┘      │  UV/OV/OC/SC/OT    │
                    │    CP2102N│                     │
                    │    (USB   │  STAT ──── GPIO     │
                    │    UART)  └──┬──────────┬───────┘
                    │             │          │
                    │           BAT        SYS (power path output)
                    │             │          │
                    │       ┌─────┴─────┐    │
                    │       │  18650     │    │
                    │       │  3.0-4.2V  │    │
                    │       └─────┬─────┘    │
                    │             │          │
                    │   ┌─────────┴───┐      │
                    │   │ MAX17048    │      │
                    │   │ Fuel Gauge  │      │
                    │   │ I2C: 0x36  │      │
                    │   └─────────────┘      │
                    │    SDA/SCL ── GPIO 21/22 (shared w/ OLED)
                    │                        │
                    │          ┌──── HARD SWITCH ────┐
                    │          │                     │
                    │   ┌──────┴───────┐    ┌────────┴────────┐
                    │   │ AP2112K-3.3  │    │ TPS61023        │
                    │   │ LDO 3.3V    │    │ Boost 5V        │
                    │   │ 600mA       │    │ 3A (1.5mA used) │
                    │   └──────┬───────┘    └────────┬────────┘
                    │          │                     │
                    │     3.3V RAIL              5V RAIL
                    │          │                     │
                    │    ┌─────┼────────┐      ┌─────┼──────┐
                    │    │     │        │      │     │      │
                    │  ESP32  OLED  MAX17048  Joy X Joy Y  Zoom
                    │
                    └── ESP32 UART (RX/TX) for USB serial
```

## Component Selection

### Charger + Power Path: BQ25185DLHR

- [DigiKey: BQ25185DLHR](https://www.digikey.com/en/products/detail/texas-instruments/BQ25185DLHR/21769368)
- Texas Instruments, $2.03, 14,032 in stock
- 10-WSON (2.2x2mm)
- 1A max charge current (set via R_ISET resistor, ~1.1k for 1A)
- CC/CV charging to 4.2V
- Hardware fault protection: overcurrent, overtemperature, overvoltage, **undervoltage lockout**, short circuit
- Power path: SYS output powers system from USB or battery automatically
- Standalone operation (no host MCU required)
- Multi-chemistry support with programmable charge voltage
- STAT pin for charge status (LED or GPIO readback)

### Fuel Gauge: MAX17048G+T10

- [DigiKey: MAX17048G+T10](https://www.digikey.com/en/products/detail/analog-devices-inc-maxim-integrated/MAX17048G-T10/3758921)
- Analog Devices / Maxim, $4.10, 22,144 in stock
- 8-TDFN (2x2mm)
- ModelGauge algorithm — no external current sense resistor needed
- I2C interface at address 0x36 (shares bus with OLED on GPIO 21/22)
- Reports state-of-charge as a percentage directly
- Alert pin for low-battery interrupt (can wake ESP32 from sleep)
- ~23uA quiescent current
- SOC accuracy +/-2.5%
- Arduino/ESP32 libraries available

### 5V Boost Converter: TPS61023DRLR

- [DigiKey: TPS61023DRLR](https://www.digikey.com/en/products/detail/texas-instruments/TPS61023DRLR/11310629)
- Texas Instruments, $1.08, 3,729 in stock
- SOT-563 (1.6x1.6mm)
- Fixed 5V output
- Input range 0.5-5.5V (covers full Li-Ion discharge curve)
- 3A output capacity (massively overspec for 1.5mA pot load, but cheapest available)
- 96% peak efficiency
- Requires 1uH inductor + input/output capacitors
- <1uA shutdown current

### 3.3V LDO: AP2112K-3.3TRG1

- [DigiKey: AP2112K-3.3TRG1](https://www.digikey.com/en/products/detail/diodes-incorporated/AP2112K-3-3TRG1/4470746)
- Diodes Incorporated, $0.22, 58,640 in stock
- SOT-23-5
- 600mA output (ESP32 WiFi draws ~110mA typical)
- 250mV dropout at full load
- Fixed 3.3V output
- Only requires input and output decoupling capacitors

### BOM Summary

| Component | Part Number | Price |
|-----------|-------------|-------|
| Charger + Power Path | BQ25185DLHR | $2.03 |
| Fuel Gauge | MAX17048G+T10 | $4.10 |
| 5V Boost | TPS61023DRLR | $1.08 |
| 3.3V LDO | AP2112K-3.3TRG1 | $0.22 |
| **Total (ICs only)** | | **$7.43** |

Passives (decoupling caps, boost inductor, R_ISET) add <$1.

## Power Flow Scenarios

| Scenario | Behavior |
|----------|----------|
| USB plugged in, charging | USB powers system via SYS. Battery charges at 1A CC/CV simultaneously. |
| USB plugged in, no battery | USB powers system via SYS. Works for bench testing/development. |
| Battery only | Battery powers system via SYS through hard switch to regulators. |
| Battery undervoltage | BQ25185 UVLO trips, SYS disconnected, system off. Battery protected from deep discharge. |
| Power off (switch open) | Zero current to regulators. BQ25185 + fuel gauge remain on battery (~30uA quiescent). USB charging still works. |

## Design Notes

- **Hard switch placement**: Between BQ25185 SYS output and regulators. USB charging works even when controller is "off".
- **Fuel gauge wiring**: MAX17048 connects directly to battery (not through the switch) so it always tracks SOC regardless of switch position.
- **Charge current**: Set by R_ISET resistor on BQ25185. For 1A charge: R_ISET ~ 1.1k ohm (per datasheet formula).
- **3.3V dropout limit**: AP2112K needs ~3.55V input minimum to regulate 3.3V. Below that the rail sags, but the fuel gauge will warn at a configurable threshold well before this point.
- **Boost inductor**: TPS61023 requires a 1uH inductor. Current rating can be minimal since the load is only 1.5mA. A small 0805 or 1008 inductor is sufficient.
- **Charge status**: BQ25185 STAT pin can drive a charge indicator LED directly, or connect to an ESP32 GPIO for software readback displayed on the OLED.
- **I2C bus**: Shared between OLED (SSD1306/SH1106) and fuel gauge (MAX17048) on GPIO 21 (SDA) / GPIO 22 (SCL). Different I2C addresses, no conflict.

## Battery Specifications

| Parameter | Value |
|-----------|-------|
| Cell | Jauch LI INR18650JD-30P |
| Nominal voltage | 3.6V |
| Capacity | 3000mAh |
| Charge voltage | 4.2V |
| Standard charge current | 0.5C (1.5A) |
| Max charge current | 6.0A |
| Discharge cutoff | 2.5V |
| Standard charge cutoff current | 50mA |
