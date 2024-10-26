#include "../../../intf/pci.h"
#include "../../../intf/io.h"
#include "../../../intf/virtual.h"

pt::uint32_t pciConfigReadDWord(const pt::uint8_t bus, const pt::uint8_t slot, const pt::uint8_t func, const pt::uint8_t offset) {
  const auto address = bus << 16 | slot << 11 |
                       func << 8 | offset & 0xFC | 0x80000000;

  // Write out the address
  IO::outd(0xCF8, address);
  return IO::ind(0xCFC);
}

bool pci::check_device(pci_device* ptr, const pci_query query)
{
  const auto config_word = pciConfigReadDWord(query.bus, query.device, 0, 0);
  const auto vendor_id = config_word & 0xFFFF;
  if (vendor_id == 0xFFFF) {
    ptr->vendor_id = vendor_id;
    return false;
   }
  const auto status_with_command = pciConfigReadDWord(query.bus, query.device, 0, 4);
  const auto class_with_subclass = pciConfigReadDWord(query.bus, query.device, 0, 8);
  const auto base_address0_register = pciConfigReadDWord(query.bus, query.device, 0, 0x10);
  const auto base_address1_register = pciConfigReadDWord(query.bus, query.device, 0, 0x14);
  const auto device_id = config_word >> 16;
  const auto class_code =  class_with_subclass >> 24;
  const auto subclass_code = class_with_subclass & 0xFF;
  ptr->vendor_id = vendor_id;
  ptr->device_id = device_id;
  ptr->class_code = class_code;
  ptr->subclass_code = subclass_code;
  ptr->command = status_with_command & 0xFFFF;
  ptr->base_address_0 = base_address0_register;
  ptr->base_address_1 = base_address1_register;
  return true;
}

pci_device* pci::enumerate()
{
  const auto vmm = VMM::Instance();
  auto* device_instance = static_cast<pci_device *>(vmm->kmalloc(sizeof(pci_device) * 8192));

  int offset = 0;
  for (pt::uint16_t bus = 0; bus < 256; bus++) {
    for (pt::uint8_t device = 0; device < 32; device++) {
      const pci_query query = {bus, device};
      offset = bus * 32 + device;
      if (check_device(device_instance + offset, query)) {
        klog("PCI device found at %x:%x\n", bus, device);
      }
    }
  }
  return device_instance;
}

