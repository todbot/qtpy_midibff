/**
 * midi_interface_msc.ino -- USB-MIDI <-> UART MIDI bridge + USB MSC flash drive + SSD1306 OLED
 * 02 Jun 2026 - @todbot / Tod Kurt
 * Part of https://github.com/todbot/qtpy_midibff
 *
 * Extends midi_interface_oled with a USB Mass Storage device backed by the RP2040's
 * internal flash (the filesystem half of the flash). A config.json file on that drive
 * controls runtime behaviour: device name, realtime filtering, and note transposition.
 * The drive appears on the host as a small FAT volume. Edit config.json, eject the
 * drive, then reset the board to apply new settings.
 *
 * Libraries needed (all available via Library Manager):
 * - Adafruit TinyUSB    -- https://github.com/adafruit/Adafruit_TinyUSB_Arduino
 * - MIDI Library        -- https://github.com/FortySevenEffects/arduino_midi_library
 * - Adafruit SSD1306    -- https://github.com/adafruit/Adafruit_SSD1306
 * - Adafruit GFX Library-- https://github.com/adafruit/Adafruit-GFX-Library
 * - Adafruit SPIFlash   -- https://github.com/adafruit/Adafruit_SPIFlash
 * - SdFat - Adafruit Fork -- https://github.com/adafruit/SdFat
 * - ArduinoJson         -- https://arduinojson.org
 *
 * To install libraries with arduino-cli:
 *   arduino-cli lib install "Adafruit TinyUSB Library" "MIDI Library" \
 *     "Adafruit SSD1306" "Adafruit GFX Library" \
 *     "Adafruit SPIFlash" "SdFat - Adafruit Fork" "ArduinoJson"
 *
 * Board setup:
 * - Board: QTPy RP2040 or Xiao RP2040 (or compatible RP2040 board)
 * - Tools > USB Stack: Adafruit TinyUSB
 * - Tools > Flash Size: choose an option with 1MB FS  ← must include a FS partition
 *   QTPy RP2040 (8MB flash): "8MB (Sketch: 7MB, FS: 1MB)"
 *   Xiao RP2040  (2MB flash): "2MB (Sketch: 1MB, FS: 1MB)"
 *
 * To compile on the commandline with arduino-cli:
 *   QTPy RP2040 (8MB flash):
 *     arduino-cli compile \
 *       --fqbn rp2040:rp2040:adafruit_qtpy:usbstack=tinyusb,flash=8388608_1048576 \
 *       arduino/midi_interface_msc
 *   Xiao RP2040 (2MB flash):
 *     arduino-cli compile \
 *       --fqbn rp2040:rp2040:seeed_xiao_rp2040:usbstack=tinyusb,flash=2097152_1048576 \
 *       arduino/midi_interface_msc
 *
 * config.json format (written to drive on first boot if absent):
 *   {
 *     "name": "MIDI BFF",
 *     "manufacturer": "todbot",
 *     "transforms": []
 *   }
 *
 * Fields:
 *   name         -- USB product name and MIDI port name (max 31 chars)
 *   manufacturer -- USB manufacturer string (max 31 chars)
 *   transforms   -- ordered array of transform objects; up to 8 entries
 *
 * Each transform has a required "dir" and "op" field, plus op-specific fields:
 *
 *   dir: "in"  = UART->USB only
 *        "out" = USB->UART only
 *        "both"= both directions
 *
 *   channel: 1-16 to restrict to one channel; omit for all channels (optional on all ops)
 *
 *   {"dir":"both", "op":"filter",        "type":"realtime"}
 *   {"dir":"both", "op":"filter",        "type":"pitch_bend"}
 *     type values: note_on, note_off, cc, program_change, pitch_bend,
 *                  aftertouch, poly_aftertouch, sysex,
 *                  realtime (all: clock/start/stop/continue/active_sensing/reset),
 *                  clock, active_sensing
 *
 *   {"dir":"out",  "op":"remap_channel", "from":1, "to":3}
 *     from: 1-16 or omit/0 for any channel
 *
 *   {"dir":"both", "op":"transpose",     "semitones":12}
 *     semitones: positive = up, negative = down
 *
 *   {"dir":"in",   "op":"thin_cc",       "interval":4}
 *   {"dir":"in",   "op":"thin_cc",       "cc":74, "interval":8}
 *     interval: pass 1 in every N CC messages (>=2)
 *     cc: restrict to one CC number; omit for all CC
 *
 * A full config.json could look like this:
 *
 * {
 *   "name": "MIDI BFF",
 *   "manufacturer": "todbot",
 *   "transforms": [
 *     {"dir": "both", "op": "filter",        "type": "realtime"},
 *     {"dir": "out",  "op": "remap_channel", "from": 1, "to": 3},
 *     {"dir": "both", "op": "transpose",     "semitones": 12},
 *     {"dir": "in",   "op": "thin_cc",       "interval": 4}
 *   ]
 * }
 *
 * Notes:
 * - Config is read once at boot; edit config.json then reset the board to apply.
 * - During MSC write operations the RP2040 must erase/program internal flash, which
 *   pauses XIP (flash execution) for several milliseconds. MIDI forwarding will miss
 *   bytes during that window. This is expected behaviour.
 * - If flash.size() returns 0 after begin(), the board's Flash Size setting has no
 *   filesystem partition. Check Tools > Flash Size and choose an option that includes FS.
 *
 * OLED: 128x32 SSD1306 on the standard I2C port (SDA/SCL), address 0x3C.
 * Change OLED_ADDR to 0x3D if your module uses the alternate address.
 * If the display is absent or fails to initialize, the sketch continues without it.
 *
 * Display examples (128x32, text size 1):
 *
 *   Normal message:         Config changed:
 *   ┌────────────────────┐  ┌────────────────────┐
 *   │USB NoteOn    ch1   │  │Config saved.       │
 *   │n:C4(60)  v:127     │  │Reset to apply.     │
 *   └────────────────────┘  └────────────────────┘
 *
 * SysEx: messages up to 127 bytes are forwarded; larger messages are dropped.
 * See midi_interface.ino for the MIDI_CREATE_CUSTOM_INSTANCE workaround.
 **/

#include "flash_storage.h"  // must be first: pulls in TinyUSB before SPIFlash
#include <MIDI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include "midi_transforms.h"

// -- OLED

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
static bool display_ok = false;

// -- MIDI objects

Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDIusb);
MIDI_CREATE_INSTANCE(HardwareSerial,     Serial1,   MIDIuart);

// -- Runtime config

struct Config {
    char name[32];
    char manufacturer[32];
};

static Config config;

// --
// Filter / transform — delegate to the transform pipeline loaded from config.json.
// Returns false to drop the message. See midi_transforms.h for the op reference.
// --

bool filter_usb_to_uart(midi::MidiType& type, uint8_t& data1, uint8_t& data2, uint8_t& channel) {
    return apply_transforms(type, data1, data2, channel, DIR_OUT);
}

bool filter_uart_to_usb(midi::MidiType& type, uint8_t& data1, uint8_t& data2, uint8_t& channel) {
    return apply_transforms(type, data1, data2, channel, DIR_IN);
}

// --
// Config helpers
// --

static void load_defaults() {
    strlcpy(config.name,         "MIDI BFF", sizeof(config.name));
    strlcpy(config.manufacturer, "todbot",   sizeof(config.manufacturer));
}

static void write_default_config() {
    File32 f;
    if (!f.open(&fatfs, "config.json", O_RDWR | O_CREAT | O_TRUNC)) return;
    f.print("{\n"
            "  \"name\": \"MIDI BFF\",\n"
            "  \"manufacturer\": \"todbot\",\n"
            "  \"transforms\": [\n"
            "  ]\n"
            "}\n");
    f.close();
    fatfs.cacheClear();
}

static void read_config() {
    File32 f;
    if (!f.open(&fatfs, "config.json", O_RDONLY)) return;
    char buf[512];
    int32_t n = f.read(buf, sizeof(buf) - 1);
    f.close();
    if (n <= 0) return;
    buf[n] = '\0';
    JsonDocument doc;
    if (deserializeJson(doc, buf)) return;  // parse error → keep defaults
    strlcpy(config.name,         doc["name"]         | "MIDI BFF", sizeof(config.name));
    strlcpy(config.manufacturer, doc["manufacturer"] | "todbot",   sizeof(config.manufacturer));
    parse_transforms(doc["transforms"].as<JsonArrayConst>());
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

static void note_str(uint8_t note, char* buf) {
    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    snprintf(buf, 5, "%s%d", names[note % 12], (note / 12) - 1);
}

static void show_msg(const char* src, midi::MidiType type,
                     uint8_t data1, uint8_t data2, uint8_t channel) {
    if (!display_ok) return;
    char line1[22], line2[22], nname[5];

    if (type < midi::SystemExclusive)
        snprintf(line1, sizeof(line1), "%s %-9s ch%d", src, midi_type_name(type), channel);
    else
        snprintf(line1, sizeof(line1), "%s %s", src, midi_type_name(type));

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
            snprintf(line2, sizeof(line2), "pc:%d", data1 + 1);
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
    snprintf(buf, sizeof(buf), "%d bytes", array_len > 2 ? array_len - 2 : 0);
    display.print(buf);
    display.display();
}

static void show_text(const char* line1, const char* line2) {
    if (!display_ok) return;
    display.clearDisplay();
    display.setCursor(0, 2);
    display.print(line1);
    display.setCursor(0, 18);
    display.print(line2);
    display.display();
}

// --

void setup() {
#if defined(ARDUINO_ARCH_RP2040)
    // Serial1.setRX(midi_rx_pin);
    // Serial1.setTX(midi_tx_pin);
#endif

    // 1. OLED first — flash_format() may need to show "Formatting...".
    display_ok = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
    if (display_ok) {
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setTextWrap(false);
    }

    // 2. Load defaults, init flash, format if needed, read config.
    //    Must complete before USB descriptors so name/manufacturer are ready.
    load_defaults();
    if (flash_begin()) {
        bool mounted = fatfs.begin(&flash);
        if (!mounted) {
            show_text("Formatting...", "");
            mounted = flash_format(config.name);
            if (mounted) write_default_config();
        }
        if (mounted) read_config();
    }

    // 3. Set USB descriptors from config before the TinyUSB stack starts.
    TinyUSBDevice.setManufacturerDescriptor(config.manufacturer);
    TinyUSBDevice.setProductDescriptor(config.name);

    // 4. MSC must be begin()'d before MIDI so it registers first with TinyUSB.
    flash_msc_begin(config.manufacturer, config.name);

    // 5. MIDI (starts the TinyUSB stack).
    usb_midi.setStringDescriptor(config.name);
    usb_midi.begin();
    MIDIusb.begin(MIDI_CHANNEL_OMNI);
    MIDIusb.turnThruOff();
    MIDIuart.begin(MIDI_CHANNEL_OMNI);
    MIDIuart.turnThruOff();

    // 6. OLED splash.
    show_text("MIDI BFF", "");
}

void loop() {
    // If the host wrote new data to the MSC volume, show a reminder to reset.
    if (msc_changed) {
        msc_changed = false;
        show_text("Config saved.", "Reset to apply.");
    }

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
