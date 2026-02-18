#pragma once
#include "defs.h"

extern const char*          Logo;
extern const unsigned char* PotatoLogo;
extern pt::uint8_t*         BootSound;
extern pt::uint32_t         BootSoundSize;
extern pt::uint8_t*         FontData;
extern pt::uint32_t         FontDataSize;

// Load all assets from the FAT12 disk (potato.txt, potato.raw, font.psf, boot.raw).
// Must be called after FAT12::initialize() succeeds.
void load_assets();
