#include "../../../intf/pci.h"

pt::uint32_t pciConfigReadDWord(pt::uint8_t bus, pt::uint8_t slot, pt::uint8_t func, pt::uint8_t offset) {
  pt::uint32_t lbus  = (pt::uint32_t)bus;
  pt::uint32_t lslot = (pt::uint32_t)slot;
  pt::uint32_t lfunc = (pt::uint32_t)func;

  // Create configuration address as per Figure 1
  const pt::uint32_t address = (pt::uint32_t) ((lbus << 16) | (lslot << 11) |
                                         (lfunc << 8) | (offset & 0xFC) | ((pt::uint32_t) 0x80000000));

  // Write out the address
  IO::outd(0xCF8, address);
  return (pt::uint32_t)(IO::ind(0xCFC));
}

bool pci::check_device(pci_device* ptr, const pci_query query)
{
  auto config_word = pciConfigReadDWord(query.bus, query.device, 0, 0);
  auto vendor_id = config_word & 0xFFFF;
  if (vendor_id == 0xFFFF) {
    (*ptr).vendor_id = vendor_id;
    return false;
   }
  auto device_id = config_word >> 16;
  auto class_with_subclass = pciConfigReadDWord(query.bus, query.device, 0, 8);
  auto class_code =  class_with_subclass >> 24;
  auto subclass_code = class_with_subclass & 0xFF;
  (*ptr).vendor_id = vendor_id;
  (*ptr).device_id = device_id;
  (*ptr).class_code = class_code;
  (*ptr).subclass_code = subclass_code;
  return true;
}

pci_device* pci::enumerate()
{
  auto vmm = VMM::Instance();
  pci_device* device_instance = (pci_device *)vmm->kmalloc(sizeof(pci_device)*8192);
  pt::uint16_t bus;
  pt::uint8_t device;

  int offset = 0;
  for (bus = 0; bus < 256; bus++) {
    for (device = 0; device < 32; device++) {
      pci_query query = {bus, device};
      pci::check_device(device_instance + offset, query);
      offset += sizeof(pci_device);
    }
  }
  return device_instance;
}

