#pragma once
#include "defs.h"
#include "pci.h"

using driver_init_fn = void* (*)(const pci_device* dev);

struct pci_driver {
    pt::uint8_t class_code;
    pt::uint8_t subclass_code;
    driver_init_fn init;
};