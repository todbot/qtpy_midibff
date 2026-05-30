/**
 * midi_interface_oled.ino -- USB-MIDI <-> UART MIDI bridge with SSD1306 OLED display
 * 30 May 2026 - @todbot / Tod Kurt
 * Part of https://github.com/todbot/qtpy_midibff
 *
 * Forwards all MIDI between USB and UART in both directions,
 * and displays each incoming message on a 128x32 I2C OLED.
 * Realtime messages (Clock, ActiveSensing, etc.) are forwarded but not shown,
 * to prevent display flicker. If you're sending dense MIDI (e.g. clock),
 * uncomment the realtime filter lines in filter_usb_to_uart() / filter_uart_to_usb()
 * to drop them before forwarding too.
 *
 * Libraries needed (all available via Library Manager):
 * - Adafruit TinyUSB   -- https://github.com/adafruit/Adafruit_TinyUSB_Arduino
 * - MIDI Library       -- https://github.com/FortySevenEffects/arduino_midi_library
 * - Adafruit SSD1306   -- https://github.com/adafruit/Adafruit_SSD1306
 * - Adafruit GFX Library -- https://github.com/adafruit/Adafruit-GFX-Library
 *
 * To upload:
 * - Board: QTPy RP2040, Xiao RP2040, or similar
 * - Tools > USB Stack: Adafruit TinyUSB
 *
 * To compile on the commandline with arduino-cli for Adafruit QTPy RP2040:
 *   arduino-cli compile \
 *    --fqbn rp2040:rp2040:adafruit_qtpy:usbstack=tinyusb \
 *   arduino/midi_interface_oled
 *
 * OLED: 128x32 SSD1306 on the standard I2C port (SDA/SCL), address 0x3C.
 * Change OLED_ADDR to 0x3D if your module uses the alternate address.
 *
 * SysEx: same behaviour as midi_interface.ino — messages up to 127 bytes are
 * forwarded; larger messages are dropped. See that sketch for details and the
 * MIDI_CREATE_CUSTOM_INSTANCE workaround to raise the limit.
 **/

#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// -- OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
static bool display_ok = false;

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
// OLED helpers
// --

static const char* midi_type_name(midi::MidiType t) {
    switch (t) {
        case midi::NoteOn:            return "NoteOn";
        case midi::NoteOff:           return "NoteOff";
        case midi::ControlChange:     return "CC";
        case midi::ProgramChange:     return "PC";
        case midi::PitchBend:         return "PitchBend";
        case midi::AfterTouchChannel: return "ChanAT";
        case midi::AfterTouchPoly:    return "PolyAT";
        case midi::SystemExclusive:   return "SysEx";
        case midi::SongPosition:      return "SongPos";
        case midi::SongSelect:        return "SongSel";
        case midi::TuneRequest:       return "Tune";
        case midi::Clock:             return "Clock";
        case midi::Start:             return "Start";
        case midi::Stop:              return "Stop";
        case midi::Continue:          return "Continue";
        case midi::ActiveSensing:     return "ActSens";
        case midi::SystemReset:       return "Reset";
        default:                      return "?";
    }
}

// Fills buf with note name + octave, e.g. "C4", "F#3". buf must be >= 5 bytes.
static void note_str(uint8_t note, char* buf) {
    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    snprintf(buf, 5, "%s%d", names[note % 12], (note / 12) - 1);
}

// Displays one non-realtime MIDI message.
// src = "USB" or "UART"; type/data1/data2/channel from the MIDI library getters.
static void show_msg(const char* src, midi::MidiType type,
                     uint8_t data1, uint8_t data2, uint8_t channel) {
    if (!display_ok) return;

    char line1[22], line2[22];
    char nname[5];

    // Line 1: source, type name, channel (channel-voice messages only)
    if (type < midi::SystemExclusive)
        snprintf(line1, sizeof(line1), "%s %-9s ch%d", src, midi_type_name(type), channel);
    else
        snprintf(line1, sizeof(line1), "%s %s", src, midi_type_name(type));

    // Line 2: type-specific data
    switch (type) {
        case midi::NoteOn:
        case midi::NoteOff:
            note_str(data1, nname);
            snprintf(line2, sizeof(line2), "n:%s(%d)  v:%d", nname, data1, data2);
            break;
        case midi::ControlChange:
            snprintf(line2, sizeof(line2), "cc:%d  val:%d", data1, data2);
            break;
        case midi::ProgramChange:
            snprintf(line2, sizeof(line2), "pc:%d", data1 + 1);   // display 1-indexed
            break;
        case midi::PitchBend: {
            int16_t pb = (int16_t)(((uint16_t)data2 << 7 | data1) - 8192);
            snprintf(line2, sizeof(line2), "pb:%d", pb);
            break;
        }
        case midi::AfterTouchChannel:
            snprintf(line2, sizeof(line2), "pressure:%d", data1);
            break;
        case midi::AfterTouchPoly:
            note_str(data1, nname);
            snprintf(line2, sizeof(line2), "n:%s  p:%d", nname, data2);
            break;
        case midi::SongPosition:
            snprintf(line2, sizeof(line2), "beat:%d", (uint16_t)(data2 << 7 | data1));
            break;
        case midi::SongSelect:
            snprintf(line2, sizeof(line2), "song:%d", data1 + 1);
            break;
        default:
            line2[0] = '\0';
            break;
    }

    display.clearDisplay();
    display.setCursor(0, 2);
    display.print(line1);
    display.setCursor(0, 18);
    display.print(line2);
    display.display();
}

static void show_sysex(const char* src, uint16_t array_len) {
    if (!display_ok) return;
    char buf[22];
    display.clearDisplay();
    display.setCursor(0, 2);
    snprintf(buf, sizeof(buf), "%s SysEx", src);
    display.print(buf);
    display.setCursor(0, 18);
    // array_len includes leading 0xF0 and trailing 0xF7; show payload byte count
    snprintf(buf, sizeof(buf), "%d bytes", array_len > 2 ? array_len - 2 : 0);
    display.print(buf);
    display.display();
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
    usb_midi.begin();
    MIDIusb.begin(MIDI_CHANNEL_OMNI);
    MIDIusb.turnThruOff();
    MIDIuart.begin(MIDI_CHANNEL_OMNI);
    MIDIuart.turnThruOff();

    display_ok = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
    if (display_ok) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setTextWrap(false);
        display.setCursor(40, 12);   // center "MIDI BFF" on 128x32
        display.print("MIDI BFF");
        display.display();
    }
}

void loop() {
    // USB -> UART
    while (MIDIusb.read()) {
        midi::MidiType type    = MIDIusb.getType();
        uint8_t        data1   = MIDIusb.getData1();
        uint8_t        data2   = MIDIusb.getData2();
        uint8_t        channel = MIDIusb.getChannel();
        if (type == midi::SystemExclusive) {
            const uint8_t* buf = MIDIusb.getSysExArray();
            if (buf[0] == 0xF0) {
                uint16_t len = MIDIusb.getSysExArrayLength();
                MIDIuart.sendSysEx(len, buf, true);
                show_sysex("USB", len);
            }
        } else {
            if (type < midi::Clock)
                show_msg("USB", type, data1, data2, channel);
            if (filter_usb_to_uart(type, data1, data2, channel))
                MIDIuart.send(type, data1, data2, channel);
        }
    }

    // UART -> USB
    while (MIDIuart.read()) {
        midi::MidiType type    = MIDIuart.getType();
        uint8_t        data1   = MIDIuart.getData1();
        uint8_t        data2   = MIDIuart.getData2();
        uint8_t        channel = MIDIuart.getChannel();
        if (type == midi::SystemExclusive) {
            const uint8_t* buf = MIDIuart.getSysExArray();
            if (buf[0] == 0xF0) {
                uint16_t len = MIDIuart.getSysExArrayLength();
                MIDIusb.sendSysEx(len, buf, true);
                show_sysex("UART", len);
            }
        } else {
            if (type < midi::Clock)
                show_msg("UART", type, data1, data2, channel);
            if (filter_uart_to_usb(type, data1, data2, channel))
                MIDIusb.send(type, data1, data2, channel);
        }
    }
}
