/**
 * midi_interface.ino -- USB-MIDI <-> UART MIDI bridge for QTPy/Xiao boards
 * 29 May 2026 - @todbot / Tod Kurt
 * Part of https://github.com/todbot/qtpy_midibff
 *
 * Forwards all MIDI between USB and UART in both directions.
 * Edit filter_usb_to_uart() and filter_uart_to_usb() to add filtering or
 * transformation (e.g. drop realtime, transpose notes, remap channels).
 *
 * Libraries needed (all available via Library Manager):
 * - Adafruit TinyUSB -- https://github.com/adafruit/Adafruit_TinyUSB_Arduino
 * - MIDI Library    -- https://github.com/FortySevenEffects/arduino_midi_library
 *
 * To upload:
 * - Board: QTPy RP2040, Xiao RP2040, or similar
 * - Tools > USB Stack: Adafruit TinyUSB
 * 
 * To compile on the commandline with arduino-cli for Adafruit QTPy RP2040:
 *   arduino-cli compile \
 *    --fqbn rp2040:rp2040:adafruit_qtpy:usbstack=tinyusb \
 *   arduino/midi_interface
 *
 * Note: SysEx passthrough via send() is library/board-version dependent
 * and is not explicitly tested here.
 **/

#include <Adafruit_TinyUSB.h>
#include <MIDI.h>

// -- hardware pin definitions
// Default Serial1 pins are usually correct for QTPy/Xiao boards.
// Uncomment and adjust if your board core maps Serial1 elsewhere.
// const int midi_rx_pin = 1;
// const int midi_tx_pin = 0;

// -- MIDI objects
Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDIusb);   // USB MIDI
MIDI_CREATE_INSTANCE(HardwareSerial,     Serial1,   MIDIuart);  // UART MIDI

// --
// Filter / transform functions.
// Called once per message in each direction.
// Return false to drop the message entirely.
// Modify the arguments to transform the message before forwarding.
// --

bool filter_usb_to_uart(midi::MidiType& type, uint8_t& data1, uint8_t& data2, uint8_t& channel) {
    // -- drop all realtime messages (Clock, Start, Stop, Continue, ActiveSensing, Reset)
    // if (type >= midi::Clock) return false;

    // -- drop active sensing only
    // if (type == midi::ActiveSensing) return false;

    // -- transpose notes on channel 1 up one octave
    // if ((type == midi::NoteOn || type == midi::NoteOff) && channel == 1)
    //     data1 = min(127, data1 + 12);

    // -- remap channel 2 -> channel 3
    // if (channel == 2) channel = 3;

    return true;
}

bool filter_uart_to_usb(midi::MidiType& type, uint8_t& data1, uint8_t& data2, uint8_t& channel) {
    // -- drop all realtime messages
    // if (type >= midi::Clock) return false;

    // -- drop active sensing only
    // if (type == midi::ActiveSensing) return false;

    // -- transpose notes on channel 1 up one octave
    // if ((type == midi::NoteOn || type == midi::NoteOff) && channel == 1)
    //     data1 = min(127, data1 + 12);

    return true;
}

// --

void setup() {
#if defined(ARDUINO_ARCH_RP2040)
    // Serial1.setRX(midi_rx_pin);
    // Serial1.setTX(midi_tx_pin);
#endif
    TinyUSBDevice.setManufacturerDescriptor("todbot");
    TinyUSBDevice.setProductDescriptor("MIDI BFF");
    usb_midi.setStringDescriptor("MIDI BFF");
    usb_midi.begin();  // must be called before TinyUSB stack initializes
    MIDIusb.begin(MIDI_CHANNEL_OMNI);
    MIDIusb.turnThruOff();   // disable auto-echo; we forward manually
    MIDIuart.begin(MIDI_CHANNEL_OMNI);
    MIDIuart.turnThruOff();
}

void loop() {
    // USB -> UART
    while (MIDIusb.read()) {
        midi::MidiType type    = MIDIusb.getType();
        uint8_t        data1   = MIDIusb.getData1();
        uint8_t        data2   = MIDIusb.getData2();
        uint8_t        channel = MIDIusb.getChannel();
        if (filter_usb_to_uart(type, data1, data2, channel)) {
            MIDIuart.send(type, data1, data2, channel);
        }
    }

    // UART -> USB
    while (MIDIuart.read()) {
        midi::MidiType type    = MIDIuart.getType();
        uint8_t        data1   = MIDIuart.getData1();
        uint8_t        data2   = MIDIuart.getData2();
        uint8_t        channel = MIDIuart.getChannel();
        if (filter_uart_to_usb(type, data1, data2, channel)) {
            MIDIusb.send(type, data1, data2, channel);
        }
    }
}
