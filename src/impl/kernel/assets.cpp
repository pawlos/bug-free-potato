#include "assets.h"
#include "fat12.h"
#include "virtual.h"
#include "kernel.h"

const char*          Logo         = nullptr;
const unsigned char* PotatoLogo   = nullptr;
pt::uint8_t*         BootSound    = nullptr;
pt::uint32_t         BootSoundSize = 0;
pt::uint8_t*         FontData     = nullptr;
pt::uint32_t         FontDataSize  = 0;

extern VMM vmm;

void load_assets() {
    FAT12_File file;

    if (FAT12::open_file("potato.txt", &file)) {
        char* buf = (char*)vmm.kmalloc(file.file_size + 1);
        if (buf) {
            pt::uint32_t n = FAT12::read_file(&file, buf, file.file_size);
            buf[n] = '\0';
            Logo = buf;
            klog("[ASSETS] Loaded potato.txt (%d bytes)\n", n);
        }
        FAT12::close_file(&file);
    } else {
        klog("[ASSETS] Warning: potato.txt not found\n");
    }

    if (FAT12::open_file("potato.raw", &file)) {
        unsigned char* buf = (unsigned char*)vmm.kmalloc(file.file_size);
        if (buf) {
            pt::uint32_t n = FAT12::read_file(&file, buf, file.file_size);
            PotatoLogo = buf;
            klog("[ASSETS] Loaded potato.raw (%d bytes)\n", n);
        }
        FAT12::close_file(&file);
    } else {
        klog("[ASSETS] Warning: potato.raw not found\n");
    }

    if (FAT12::open_file("font.psf", &file)) {
        FontData = (pt::uint8_t*)vmm.kmalloc(file.file_size);
        if (FontData) {
            FontDataSize = FAT12::read_file(&file, FontData, file.file_size);
            klog("[ASSETS] Loaded font.psf (%d bytes)\n", FontDataSize);
        }
        FAT12::close_file(&file);
    } else {
        klog("[ASSETS] Warning: font.psf not found\n");
    }

    if (FAT12::open_file("boot.raw", &file)) {
        BootSound = (pt::uint8_t*)vmm.kmalloc(file.file_size);
        if (BootSound) {
            BootSoundSize = FAT12::read_file(&file, BootSound, file.file_size);
            klog("[ASSETS] Loaded boot.raw (%d bytes)\n", BootSoundSize);
        }
        FAT12::close_file(&file);
    } else {
        klog("[ASSETS] Warning: boot.raw not found\n");
    }
}
