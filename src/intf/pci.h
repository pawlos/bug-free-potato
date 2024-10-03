#ifndef PCI_H
#define PCI_H
#include "io.h"

struct pci_device {};

struct pci_query {
  pt::uint8_t bus;
  pt::uint8_t device;
  pt::uint8_t function;
};

class pci {

  public:
    static pci_device* enumerate();
};



#endif //PCI_H
