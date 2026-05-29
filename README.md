# qtpy_midibff

QTPy MIDI BFF — a small MIDI interface board for QTPy / Xiao class boards (RP2040 or compatible).
The board breaks out the host MCU's hardware UART to standard MIDI IN and MIDI OUT jacks (TRS or 5-pin DIN).

<img src="./docs/qtpy_midibff_disp.png" width=500>

## Hardware

The PCB is meant for easy assembly using only through-hole components. 
It is essentialy a QTPy "doubler" with a standard MIDI In and MIDI Out circuit, 
with a 4-pin I2C header for a standard OLED display (usually 128x32 SSD1306).

## Assembling

_Coming soon._



## Arduino sketches

Requires [arduino-pico](https://arduino-pico.readthedocs.io/en/latest/install.html) board core.

Set **Tools > USB Stack: Adafruit TinyUSB** before uploading any sketch.

Libraries needed (Arduino Library Manager):
- [Adafruit TinyUSB](https://github.com/adafruit/Adafruit_TinyUSB_Arduino)
- [MIDI Library](https://github.com/FortySevenEffects/arduino_midi_library)

### `midi_interface` — USB-MIDI ↔ UART-MIDI bridge

[`arduino/midi_interface/`](arduino/midi_interface/)

Forwards all MIDI between USB and UART in both directions. Includes scaffolding to filter or
transform messages — uncomment the examples or add your own:

- Drop realtime messages (clock, active sensing, etc.)
- Transpose notes on a channel
- Remap channels

```
arduino-cli compile \
  --fqbn rp2040:rp2040:adafruit_qtpy:usbstack=tinyusb \
  arduino/midi_interface
```

## CircuitPython sketches

_Coming soon._

## Hardware

Schematic and PCB in `schematics/midi_bff/` (KiCad).
