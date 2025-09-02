#include "../../../intf/ac97.h"

void ac97::reset() {
    IO::outw(this->base_address_0 + 0x00, 1); // reset
    IO::io_wait();
    IO::outw(this->base_address_0 + 0x00, 0);
}

void ac97::set_volume(pt::uint8_t vol) {
    IO::outw(this->base_address_0 + 0x02, vol);  // Master Volume L
    IO::outw(this->base_address_0 + 0x04, vol); // Master Volume R
}