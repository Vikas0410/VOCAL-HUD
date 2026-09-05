# Hardware Schematic Reference

This document describes the per-node wiring for Vocal-HUD, reconstructed from the project's hardware schematic (XIAO ESP32-C3 + INMP441 + MAX98357A + TTP223).

Each of the two wearable nodes is built identically.

## Signal Groups

### 1. INMP441 (I2S Microphone) → XIAO ESP32-C3

| INMP441 Pin | ESP32-C3 Pin | Notes |
|---|---|---|
| VDD | 3V3 | |
| GND | GND | |
| SCK (bit clock) | I2S BCLK pin | Shared clock domain with amplifier |
| WS (word select) | I2S LRCLK pin | Shared clock domain with amplifier |
| SD (serial data out) | I2S DIN pin | Mic → MCU audio data |
| L/R | GND | Selects left channel (mono capture) |

### 2. XIAO ESP32-C3 → MAX98357A (I2S Amplifier)

| ESP32-C3 Pin | MAX98357A Pin | Notes |
|---|---|---|
| I2S BCLK | BCLK | Same clock line shared with mic input side |
| I2S LRCLK | LRC | |
| I2S DOUT | DIN | MCU → amplifier audio data |
| 3V3 | VDD | |
| GND | GND | |
| — | GAIN / SD_MODE | Set per desired output gain / shutdown control |

Amplifier output (OUTP/OUTN) drives the speaker differentially.

### 3. TTP223 (Touch Sensor) → XIAO ESP32-C3

| TTP223 Pin | ESP32-C3 Pin | Notes |
|---|---|---|
| VDD | 3V3 | |
| GND | GND (through 10 kΩ pull-down) | Prevents floating-input glitches |
| I/O (touch output) | GPIO (interrupt-capable pin) | Rising edge = touch activation |

## Notes on Clock Sharing

The microphone and amplifier are wired to the **same BCLK and LRCLK lines**, driven by the ESP32-C3's I2S peripheral. This keeps capture and playback on one synchronized clock domain rather than running the mic and amp off independent, potentially drifting clocks — the main source of the synchronization issues called out in the project report.

## Common Grounding

All components share a common ground plane. This was called out in the original report as important for reducing audio noise on the analog output side of the MAX98357A.
