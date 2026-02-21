#include "assets.h"
#include "vfs.h"
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
    File file;

    if (VFS::open_file("potato.txt", &file)) {
        char* buf = (char*)vmm.kmalloc(file.file_size + 1);
        if (buf) {
            pt::uint32_t n = VFS::read_file(&file, buf, file.file_size);
            buf[n] = '\0';
            Logo = buf;
            klog("[ASSETS] Loaded potato.txt (%d bytes)\n", n);
        }
        VFS::close_file(&file);
    } else {
        klog("[ASSETS] Warning: potato.txt not found\n");
    }

    if (VFS::open_file("potato.raw", &file)) {
        unsigned char* buf = (unsigned char*)vmm.kmalloc(file.file_size);
        if (buf) {
            pt::uint32_t n = VFS::read_file(&file, buf, file.file_size);
            PotatoLogo = buf;
            klog("[ASSETS] Loaded potato.raw (%d bytes)\n", n);
        }
        VFS::close_file(&file);
    } else {
        klog("[ASSETS] Warning: potato.raw not found\n");
    }

    if (VFS::open_file("font.psf", &file)) {
        FontData = (pt::uint8_t*)vmm.kmalloc(file.file_size);
        if (FontData) {
            FontDataSize = VFS::read_file(&file, FontData, file.file_size);
            klog("[ASSETS] Loaded font.psf (%d bytes)\n", FontDataSize);
        }
        VFS::close_file(&file);
    } else {
        klog("[ASSETS] Warning: font.psf not found\n");
    }

    if (VFS::open_file("boot.raw", &file)) {
        BootSound = (pt::uint8_t*)vmm.kmalloc(file.file_size);
        if (BootSound) {
            BootSoundSize = VFS::read_file(&file, BootSound, file.file_size);
            klog("[ASSETS] Loaded boot.raw (%d bytes)\n", BootSoundSize);
        }
        VFS::close_file(&file);
    } else {
        klog("[ASSETS] Warning: boot.raw not found\n");
    }
}
