#pragma once
#include "defs.h"

constexpr int SoundCardClass = 0x04;

struct pci_device {
  pt::uint16_t vendor_id;
  pt::uint16_t device_id;
  pt::uint16_t class_code;
  pt::uint16_t subclass_code;
  pt::uint16_t command;
  pt::uint32_t base_address_0;
  pt::uint32_t base_address_1;
};

struct pci_query {
  pt::uint16_t bus;
  pt::uint8_t device;
  pt::uint8_t function;
};

class pci {
  static bool check_device(pci_device* ptr, pci_query query);
  public:
    static pci_device* enumerate();
};
