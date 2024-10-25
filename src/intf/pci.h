#pragma once
#include "defs.h"

struct pci_device {
  pt::uint32_t vendor_id;
  pt::uint32_t device_id;
  pt::uint32_t class_code;
  pt::uint32_t subclass_code;
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
