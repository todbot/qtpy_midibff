#pragma once
// Must be included before any other header that pulls in Adafruit_SPIFlash,
// because Adafruit_TinyUSB.h must precede Adafruit_SPIFlash.h to avoid
// a redefinition conflict with the rp2040 core's bundled SdFat.
#include <Adafruit_TinyUSB.h>
#include <Adafruit_SPIFlash.h>

extern Adafruit_SPIFlash flash;
extern FatVolume         fatfs;
extern Adafruit_USBD_MSC usb_msc;
extern bool              flash_ok;
extern volatile bool     msc_changed;

// Initialize the flash transport. Returns false (and sets flash_ok=false) if
// the flash is unavailable or the board has no FS partition configured.
// Call before fatfs.begin().
bool flash_begin();

// Format the FS partition as FAT12 and mount it via fatfs.begin().
// FatFormatter cannot be used here because it requires >6 MB; this writes
// the boot sector and FAT tables directly. Adafruit_FlashCache handles
// erase-before-write, so no manual eraseChip() is needed.
// Returns true if format and mount both succeed.
bool flash_format(const char* label);

// Configure usb_msc and call usb_msc.begin(). No-op if flash_ok is false.
// Must be called before usb_midi.begin() (MSC must register with TinyUSB first).
void flash_msc_begin(const char* manufacturer, const char* name);
