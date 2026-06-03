#include "midi_transforms.h"
#include <string.h>

Transform transforms[MAX_TRANSFORMS];
uint8_t   transform_count = 0;
uint8_t   thin_cc_counters[128];

// Internal sentinels stored in filter.midi_type — not part of the public header.
enum FilterSentinel : uint8_t {
    FILT_NOTE_ON = 0,
    FILT_NOTE_OFF,
    FILT_CC,
    FILT_PROGRAM_CHANGE,
    FILT_PITCH_BEND,
    FILT_AFTERTOUCH,
    FILT_POLY_AFTERTOUCH,
    FILT_SYSEX,
    FILT_REALTIME,       // all Clock/Start/Stop/Continue/ActiveSensing/Reset
    FILT_CLOCK,
    FILT_ACTIVE_SENSING,
    FILT_UNKNOWN = 0xFF,
};

static bool matches_filter_type(midi::MidiType type, uint8_t sentinel) {
    switch ((FilterSentinel)sentinel) {
        case FILT_NOTE_ON:          return type == midi::NoteOn;
        case FILT_NOTE_OFF:         return type == midi::NoteOff;
        case FILT_CC:               return type == midi::ControlChange;
        case FILT_PROGRAM_CHANGE:   return type == midi::ProgramChange;
        case FILT_PITCH_BEND:       return type == midi::PitchBend;
        case FILT_AFTERTOUCH:       return type == midi::AfterTouchChannel;
        case FILT_POLY_AFTERTOUCH:  return type == midi::AfterTouchPoly;
        case FILT_SYSEX:            return type == midi::SystemExclusive;
        case FILT_REALTIME:         return type >= midi::Clock;
        case FILT_CLOCK:            return type == midi::Clock;
        case FILT_ACTIVE_SENSING:   return type == midi::ActiveSensing;
        default:                    return false;
    }
}

bool apply_transforms(midi::MidiType& type, uint8_t& data1, uint8_t& data2,
                      uint8_t& channel, TransformDir call_dir) {
    for (uint8_t i = 0; i < transform_count; i++) {
        const Transform& t = transforms[i];
        if (t.dir != DIR_BOTH && t.dir != call_dir) continue;
        if (t.channel != 0    && t.channel != channel) continue;

        switch (t.op) {
            case OP_FILTER:
                if (matches_filter_type(type, t.filter.midi_type)) return false;
                break;
            case OP_REMAP_CHANNEL:
                if (t.remap.from_ch == 0 || t.remap.from_ch == channel)
                    channel = t.remap.to_ch;
                break;
            case OP_TRANSPOSE:
                if (type == midi::NoteOn || type == midi::NoteOff)
                    data1 = (uint8_t)constrain((int)data1 + t.transpose.semitones, 0, 127);
                break;
            case OP_THIN_CC:
                if (type == midi::ControlChange)
                    if (t.thin_cc.cc == 255 || t.thin_cc.cc == data1)
                        if (++thin_cc_counters[data1] % t.thin_cc.interval != 0)
                            return false;
                break;
        }
    }
    return true;
}

static TransformDir parse_dir(const char* s) {
    if (s && strcmp(s, "in")  == 0) return DIR_IN;
    if (s && strcmp(s, "out") == 0) return DIR_OUT;
    return DIR_BOTH;
}

static uint8_t parse_filter_type(const char* s) {
    if (!s) return FILT_UNKNOWN;
    if (strcmp(s, "note_on")         == 0) return FILT_NOTE_ON;
    if (strcmp(s, "note_off")        == 0) return FILT_NOTE_OFF;
    if (strcmp(s, "cc")              == 0) return FILT_CC;
    if (strcmp(s, "program_change")  == 0) return FILT_PROGRAM_CHANGE;
    if (strcmp(s, "pitch_bend")      == 0) return FILT_PITCH_BEND;
    if (strcmp(s, "aftertouch")      == 0) return FILT_AFTERTOUCH;
    if (strcmp(s, "poly_aftertouch") == 0) return FILT_POLY_AFTERTOUCH;
    if (strcmp(s, "sysex")           == 0) return FILT_SYSEX;
    if (strcmp(s, "realtime")        == 0) return FILT_REALTIME;
    if (strcmp(s, "clock")           == 0) return FILT_CLOCK;
    if (strcmp(s, "active_sensing")  == 0) return FILT_ACTIVE_SENSING;
    return FILT_UNKNOWN;
}

void parse_transforms(JsonArrayConst arr) {
    transform_count = 0;
    memset(thin_cc_counters, 0, sizeof(thin_cc_counters));
    for (JsonObjectConst obj : arr) {
        if (transform_count >= MAX_TRANSFORMS) break;
        Transform t = {};
        t.dir     = parse_dir(obj["dir"] | (const char*)nullptr);
        t.channel = (uint8_t)(obj["channel"] | 0);

        const char* op = obj["op"] | "";
        if (strcmp(op, "filter") == 0) {
            uint8_t ftype = parse_filter_type(obj["type"] | (const char*)nullptr);
            if (ftype == FILT_UNKNOWN) continue;
            t.op = OP_FILTER;
            t.filter.midi_type = ftype;
        } else if (strcmp(op, "remap_channel") == 0) {
            t.op = OP_REMAP_CHANNEL;
            t.remap.from_ch = (uint8_t)(obj["from"] | 0);
            t.remap.to_ch   = (uint8_t)(obj["to"]   | 0);
            if (t.remap.to_ch == 0 || t.remap.to_ch > 16) continue;
        } else if (strcmp(op, "transpose") == 0) {
            t.op = OP_TRANSPOSE;
            t.transpose.semitones = (int8_t)(int)(obj["semitones"] | 0);
        } else if (strcmp(op, "thin_cc") == 0) {
            uint8_t interval = (uint8_t)(obj["interval"] | 0);
            if (interval < 2) continue;
            t.op = OP_THIN_CC;
            t.thin_cc.interval = interval;
            t.thin_cc.cc       = (uint8_t)(obj["cc"] | 255);
        } else {
            continue;  // unknown op — skip silently
        }
        transforms[transform_count++] = t;
    }
}
