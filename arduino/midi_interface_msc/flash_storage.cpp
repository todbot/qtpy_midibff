#include "flash_storage.h"

static Adafruit_FlashTransport_RP2040 _flashTransport;

Adafruit_SPIFlash flash(&_flashTransport);
FatVolume         fatfs;
Adafruit_USBD_MSC usb_msc;
bool              flash_ok    = false;
volatile bool     msc_changed = false;

// --
// USB MSC callbacks
// --

static int32_t msc_read_cb(uint32_t lba, void* buffer, uint32_t bufsize) {
    return flash.readBlocks(lba, (uint8_t*)buffer, bufsize / 512) ? (int32_t)bufsize : -1;
}

static int32_t msc_write_cb(uint32_t lba, uint8_t* buffer, uint32_t bufsize) {
    return flash.writeBlocks(lba, buffer, bufsize / 512) ? (int32_t)bufsize : -1;
}

static void msc_flush_cb() {
    flash.syncBlocks();
    fatfs.cacheClear();
    msc_changed = true;
}

// --
// FAT12 format
// --

// Layout for 1 MB (2048 sector) partition:
//   sector 0=boot, 1-6=FAT1, 7-12=FAT2, 13-16=root dir (64 entries), 17+=data
static bool write_fat12_fs(const char* label) {
    uint8_t sec[512];
    memset(sec, 0, sizeof(sec));

    // Boot sector BPB
    sec[0]  = 0xEB; sec[1]  = 0x58; sec[2]  = 0x90; // JMP + NOP
    memcpy(sec + 3, "MSDOS5.0", 8);
    sec[11] = 0x00; sec[12] = 0x02;  // bytes/sector: 512 (little-endian)
    sec[13] = 0x01;                   // sectors/cluster: 1
    sec[14] = 0x01; sec[15] = 0x00;  // reserved sectors: 1
    sec[16] = 0x02;                   // number of FATs: 2
    sec[17] = 0x40; sec[18] = 0x00;  // root entry count: 64
    sec[19] = 0x00; sec[20] = 0x08;  // total sectors: 2048
    sec[21] = 0xF8;                   // media type
    sec[22] = 0x06; sec[23] = 0x00;  // sectors/FAT: 6
    sec[24] = 0x01; sec[25] = 0x00;  // sectors/track: 1 (unused for flash)
    sec[26] = 0x01; sec[27] = 0x00;  // heads: 1 (unused for flash)
    sec[36] = 0x80;                   // drive number
    sec[38] = 0x29;                   // extended boot signature
    sec[39] = 0xDE; sec[40] = 0xAD; sec[41] = 0xBE; sec[42] = 0xEF; // volume ID
    memset(sec + 43, ' ', 11);        // volume label: 11 chars, space-padded
    if (label) memcpy(sec + 43, label, strnlen(label, 11));
    memcpy(sec + 54, "FAT12   ",  8);    // filesystem type
    sec[510] = 0x55; sec[511] = 0xAA;
    if (!flash.writeBlocks(0, sec, 1)) return false;

    // FAT first sector: entries 0 and 1 are reserved
    memset(sec, 0, sizeof(sec));
    sec[0] = 0xF8; sec[1] = 0xFF; sec[2] = 0xFF;
    if (!flash.writeBlocks(1, sec, 1)) return false;  // FAT1 sector 0
    if (!flash.writeBlocks(7, sec, 1)) return false;  // FAT2 sector 0

    // Remaining FAT sectors and root directory: all zeros (all clusters free)
    memset(sec, 0, sizeof(sec));
    for (uint32_t i = 2; i <= 16; i++) {
        if (i == 7) continue;  // already written
        if (!flash.writeBlocks(i, sec, 1)) return false;
    }
    flash.syncBlocks();
    return true;
}

// --
// Public API
// --

bool flash_begin() {
    flash_ok = flash.begin() && flash.size() > 0;
    return flash_ok;
}

bool flash_format(const char* label) {
    if (!write_fat12_fs(label)) return false;
    return fatfs.begin(&flash);
}

void flash_msc_begin(const char* manufacturer, const char* name) {
    if (!flash_ok) return;
    usb_msc.setID(manufacturer, name, "1.0");
    usb_msc.setCapacity(flash.size() / 512, 512);
    usb_msc.setReadWriteCallback(msc_read_cb, msc_write_cb, msc_flush_cb);
    usb_msc.setUnitReady(true);
    usb_msc.begin();
}
