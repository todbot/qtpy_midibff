# SPDX-FileCopyrightText: Copyright (c) 2026 Tod Kurt
# SPDX-License-Identifier: MIT
"""
code.py -- USB-MIDI <-> UART MIDI bridge for QTPy/Xiao boards
29 May 2026 - @todbot / Tod Kurt
Part of https://github.com/todbot/qtpy_midibff

Forwards all MIDI between USB and UART in both directions.
Edit filter_usb_to_uart() and filter_uart_to_usb() to filter or transform
messages: drop realtime, transpose notes, remap channels, etc.

Libraries needed:
- tmidi -- https://github.com/todbot/CircuitPython_TMIDI
  Copy tmidi.py to this directory or to /lib/ on your device.

SysEx messages up to 127 bytes are forwarded. Larger SysEx is consumed
and discarded (stream stays in sync) but not forwarded.
To raise the limit, increase the sysex_buf_* bytearray sizes below.

Note: tmidi channels are 0-indexed (MIDI channel 1 = 0, channel 16 = 15).
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

while True:
    # USB -> UART
    while msg := midi_usb.receive():
        if msg.type == tmidi.SYSEX:
            if msg.data0 > 0:
                forward_sysex(sysex_buf_usb, msg.data0, uart)
        elif filter_usb_to_uart(msg):
            midi_uart.send(msg)

    # UART -> USB
    while msg := midi_uart.receive():
        if msg.type == tmidi.SYSEX:
            if msg.data0 > 0:
                forward_sysex(sysex_buf_uart, msg.data0, usb_midi.ports[1])
        elif filter_uart_to_usb(msg):
            midi_usb.send(msg)
