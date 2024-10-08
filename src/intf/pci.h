#pragma once
#include "defs.h"
#include "io.h"
#include "virtual.h"

struct pci_device {
  int vendor_id;
  int device_id;
  int class_code;
  int subclass_code;
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
