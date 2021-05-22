#include "print.h"
#include "com.h"
#include "multiboot.h"

extern "C" void kernel_main(struct multiboot_info* mbt, unsigned int magic) {
	ComDevice comDevice;
	print_clear();
	if (magic != 0x36d76289)
    {
      print_str ("Invalid magic number: ");
      print_str (hexToString(magic));
      return;
    }
	print_set_color(PRINT_COLOR_YELLOW, PRINT_COLOR_BLACK);
	print_str("Welcome to 64-bit potato OS\n");
	print_str("Boot info: ");
	print_str(hexToString(mbt->flags));
}