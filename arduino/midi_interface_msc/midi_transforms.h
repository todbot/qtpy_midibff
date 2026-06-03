#pragma once
#include <MIDI.h>
#include <ArduinoJson.h>

// Maximum number of entries in the "transforms" array in config.json.
#define MAX_TRANSFORMS 8

enum TransformOp  : uint8_t { OP_FILTER, OP_REMAP_CHANNEL, OP_TRANSPOSE, OP_THIN_CC };
enum TransformDir : uint8_t {
    DIR_BOTH,   // apply in both directions (USB->UART and UART->USB)
    DIR_OUT,    // USB->UART only
    DIR_IN,     // UART->USB only
};

struct Transform {
    TransformOp  op;
    TransformDir dir;
    uint8_t      channel;  // 0 = any channel, 1-16 = specific channel
    union {
        struct { uint8_t midi_type; }                   filter;     // internal sentinel
        struct { uint8_t from_ch;   uint8_t to_ch;   }  remap;
        struct { int8_t  semitones;                  }  transpose;
        struct { uint8_t cc;        uint8_t interval; }  thin_cc;   // cc=255 means any
    };
};

extern Transform transforms[MAX_TRANSFORMS];
extern uint8_t   transform_count;
extern uint8_t   thin_cc_counters[128];  // per-CC-number counters for thin_cc ops

// Parse the "transforms" JSON array from config.json into transforms[].
// Call from read_config() after a successful deserializeJson().
void parse_transforms(JsonArrayConst arr);

// Apply all transforms for the given direction. Returns false to drop the message.
// Use DIR_OUT for the USB->UART path, DIR_IN for UART->USB.
bool apply_transforms(midi::MidiType& type, uint8_t& data1, uint8_t& data2,
                      uint8_t& channel, TransformDir dir);
