# SPDX-FileCopyrightText: Copyright (c) 2026 Tod Kurt
# SPDX-License-Identifier: MIT
"""
code.py -- USB-MIDI <-> UART MIDI bridge with SSD1306 OLED display
30 May 2026 - @todbot / Tod Kurt
Part of https://github.com/todbot/qtpy_midibff

Forwards all MIDI between USB and UART in both directions,
and displays each incoming message on a 128x32 I2C OLED.
Realtime messages (Clock, ActiveSensing, etc.) are forwarded but not shown,
to prevent display flicker.

Libraries needed:
- tmidi        -- https://github.com/todbot/CircuitPython_TMIDI
- adafruit_ssd1306 -- install with: circup install adafruit_ssd1306

OLED: 128x32 SSD1306 on the standard I2C port (SDA/SCL), address 0x3C.
Change OLED_ADDR to 0x3D if your module uses the alternate address.
If the display is absent or fails to initialize, the sketch continues as
a plain MIDI bridge.

Note: tmidi channels are 0-indexed (MIDI channel 1 = 0, channel 16 = 15).

SysEx messages up to 127 bytes are forwarded. Larger SysEx is consumed
and discarded (stream stays in sync) but not forwarded.
To raise the limit, increase the sysex_buf_* bytearray sizes below.

Display examples (128x32, 8x8 font, 16 chars wide, 2 rows):

  Note On / Off:          CC:                     Program Change:
  ┌────────────────┐      ┌────────────────┐      ┌────────────────┐
  │USB NoteOn      │      │UART CC         │      │USB PC          │
  │ch1 C4 v127     │      │ch10 cc74 v63   │      │ch1 pc5         │
  └────────────────┘      └────────────────┘      └────────────────┘

  Pitch Bend:             SysEx:
  ┌────────────────┐      ┌────────────────┐
  │USB PitchBend   │      │UART SysEx      │
  │ch1 pb-2048     │      │42 bytes        │
  └────────────────┘      └────────────────┘
"""

import usb_midi
import busio
import board
import tmidi

# -- MIDI setup

uart = busio.UART(tx=board.TX, rx=board.RX, baudrate=31250, timeout=0.001)

sysex_buf_usb  = bytearray(128)  # SysEx capture buffer for USB->UART direction
sysex_buf_uart = bytearray(128)  # SysEx capture buffer for UART->USB direction

midi_usb  = tmidi.MIDI(midi_in=usb_midi.ports[0], midi_out=usb_midi.ports[1],
                       sysex_buffer=sysex_buf_usb)
midi_uart = tmidi.MIDI(midi_in=uart, midi_out=uart,
                       sysex_buffer=sysex_buf_uart)

# -- OLED setup

OLED_ADDR = 0x3C

try:
    import adafruit_ssd1306  # not using displayio because framebuf version is faster?
    i2c = busio.I2C(board.SCL, board.SDA)
    oled = adafruit_ssd1306.SSD1306_I2C(128, 32, i2c, addr=OLED_ADDR)
    oled.fill(0)
    oled.text("MIDI BFF", 40, 12, 1)
    oled.show()
except Exception:
    oled = None

# --
# Filter / transform functions.
# Modify msg in place to transform, return True to forward, False to drop.
# SysEx messages are handled separately and do not pass through these functions.
# --

def filter_usb_to_uart(msg):
    # -- drop all realtime messages (Clock, Start, Stop, Continue, Active Sensing, Reset)
    # if msg.type >= tmidi.CLOCK: return False

    # -- drop active sensing only
    # if msg.type == tmidi.ACTIVE_SENSING: return False

    # -- transpose notes on channel 1 up one octave
    # if msg.type in (tmidi.NOTE_ON, tmidi.NOTE_OFF) and msg.channel == 0:
    #     msg.note = min(127, msg.note + 12)

    # -- remap channel 2 -> channel 3 (0-indexed: 1 -> 2)
    # if msg.channel == 1: msg.channel = 2

    return True


def filter_uart_to_usb(msg):
    # -- drop all realtime messages
    # if msg.type >= tmidi.CLOCK: return False

    # -- drop active sensing only
    # if msg.type == tmidi.ACTIVE_SENSING: return False

    # -- transpose notes on channel 1 up one octave
    # if msg.type in (tmidi.NOTE_ON, tmidi.NOTE_OFF) and msg.channel == 0:
    #     msg.note = min(127, msg.note + 12)

    return True


# --
# tmidi's send() cannot carry SysEx payloads (Message.__bytes__() emits only
# the 0xF0 status byte). Write the captured payload directly to the output port.
# --

def forward_sysex(buf, n, out_port):
    out_port.write(b'\xf0')
    out_port.write(memoryview(buf)[:n])
    out_port.write(b'\xf7')


# --
# OLED display helpers
# --

_NOTE_NAMES = ("C","C#","D","D#","E","F","F#","G","G#","A","A#","B")

def _note_str(note):
    return _NOTE_NAMES[note % 12] + str(note // 12 - 1)

_MSG_NAMES = {
    tmidi.NOTE_ON:          "NoteOn",
    tmidi.NOTE_OFF:         "NoteOff",
    tmidi.CC:               "CC",
    tmidi.PROGRAM_CHANGE:   "PC",
    tmidi.PITCH_BEND:       "PitchBend",
    tmidi.CHANNEL_PRESSURE: "ChanAT",
    tmidi.AFTERTOUCH:       "PolyAT",
    tmidi.SYSEX:            "SysEx",
    tmidi.SONG_POSITION:    "SongPos",
    tmidi.SONG_SELECT:      "SongSel",
    tmidi.TUNE_REQUEST:     "Tune",
    tmidi.CLOCK:            "Clock",
    tmidi.START:            "Start",
    tmidi.STOP:             "Stop",
    tmidi.CONTINUE:         "Continue",
    tmidi.ACTIVE_SENSING:   "ActSens",
    tmidi.SYSTEM_RESET:     "Reset",
}

def show_msg(src, msg):
    if oled is None:
        return
    mtype = msg.type
    ch = msg.channel + 1   # display 1-indexed channel

    line1 = f"{src} {_MSG_NAMES.get(mtype, '?')}"

    if mtype in (tmidi.NOTE_ON, tmidi.NOTE_OFF):
        line2 = f"ch{ch} {_note_str(msg.note)} v{msg.velocity}"
    elif mtype == tmidi.CC:
        line2 = f"ch{ch} cc{msg.data0} v{msg.data1}"
    elif mtype == tmidi.PROGRAM_CHANGE:
        line2 = f"ch{ch} pc{msg.data0 + 1}"        # display 1-indexed
    elif mtype == tmidi.PITCH_BEND:
        line2 = f"ch{ch} pb{msg.pitch_bend}"
    elif mtype == tmidi.CHANNEL_PRESSURE:
        line2 = f"ch{ch} at{msg.data0}"
    elif mtype == tmidi.AFTERTOUCH:                 # poly aftertouch
        line2 = f"ch{ch} {_note_str(msg.data0)} p{msg.data1}"
    elif mtype == tmidi.SONG_POSITION:
        line2 = f"beat{msg.data1 << 7 | msg.data0}"
    elif mtype == tmidi.SONG_SELECT:
        line2 = f"song{msg.data0 + 1}"
    else:
        line2 = ""

    oled.fill(0)
    oled.text(line1, 0, 2, 1)
    oled.text(line2, 0, 18, 1)
    oled.show()


def show_sysex(src, n):
    if oled is None:
        return
    oled.fill(0)
    oled.text(f"{src} SysEx", 0, 2, 1)
    oled.text(f"{n} bytes", 0, 18, 1)
    oled.show()


# --

while True:
    # USB -> UART
    while msg := midi_usb.receive():
        if msg.type == tmidi.SYSEX:
            if msg.data0 > 0:
                forward_sysex(sysex_buf_usb, msg.data0, uart)
                show_sysex("USB", msg.data0)
        else:
            if msg.type < tmidi.CLOCK:
                show_msg("USB", msg)
            if filter_usb_to_uart(msg):
                midi_uart.send(msg)

    # UART -> USB
    while msg := midi_uart.receive():
        if msg.type == tmidi.SYSEX:
            if msg.data0 > 0:
                forward_sysex(sysex_buf_uart, msg.data0, usb_midi.ports[1])
                show_sysex("UART", msg.data0)
        else:
            if msg.type < tmidi.CLOCK:
                show_msg("UART", msg)
            if filter_uart_to_usb(msg):
                midi_usb.send(msg)
