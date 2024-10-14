#include "keyboard.h"
#include "com.h"
#include "kernel.h"

constexpr char layout[128] =
{
	0,  27, '1', '2', '3', '4', '5', '6', '7', '8',	'9', '0', '-', '=', '\b', '\t',
	'q', 'w', 'e', 'r',	't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
	0,
	'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
	0,
	'\\', 'z', 'x', 'c', 'v', 'b', 'n',	'm', ',', '.', '/',
	0,
	'*',
	0,
	' ',
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	'-',
	0, 0, 0,
	'+',
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

constexpr char layout_upper[128] =
{
	0,  27, '!', '@', '#', '$', '%', '^', '&', '*',	'(', ')', '_', '+', '\b', '\t',
	'Q', 'W', 'E', 'R',	'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
	0,
	'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\'', '~',
	0,
	'|', 'Z', 'X', 'C', 'V', 'B', 'N',	'M', '<', '>', '?',
	0,
	'*',
	0,
	' ',
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	'-',
	0, 0, 0,
	'+',
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

bool shiftPressed = false;
bool altPressed = false;
bool ctrlPressed = false;
bool capsLockOn = false;

static char keyboard_buffer[128] = {0};
static int write_pos = 0;
static int read_pos = 0;

void keyboard_log(const char *str, ...) {
#ifdef KEYBOARD_LOG
	va_list arg_ptr;
	va_start(arg_ptr, str);
	debug.print_str(str, arg_ptr);
	va_end(arg_ptr);
#endif
}

char get_char() {
	if (read_pos < write_pos) {
		keyboard_log("Reading from %d\n", read_pos % 128);
		const auto c = keyboard_buffer[read_pos%128];
		read_pos += 1;
		return c;
	}
	return -1;
}

void keyboard_routine(const pt::uint8_t scancode)
{
	if (scancode & 0x80)
	{
		//Key released
		const pt::uint8_t code = scancode & ~0x80;
		if (code == L_SHIFT || code == R_SHIFT)
		{
			shiftPressed = false;
		}
		else if (code == L_ALT)
		{
			altPressed = false;
		}
		else if (code == L_CTRL)
		{
			ctrlPressed = false;
		}
	}
	else
	{
		//Key pressed
		if (scancode == L_SHIFT || scancode == R_SHIFT)
		{
			shiftPressed = true;
		}
		else if (scancode == CAPSLOCK)
		{
			capsLockOn = !capsLockOn;
		}
		else if (scancode == L_ALT)
		{
			altPressed = true;
		}
		else if (scancode == L_CTRL)
		{
			ctrlPressed = true;
		}
		else
		{
			const char* current_layout = shiftPressed || capsLockOn ? layout_upper : layout;
			const char key = current_layout[scancode];
			keyboard_buffer[write_pos % 128] = key;
			keyboard_log("Putting the '%c' into %d\n", key, write_pos % 128);
			write_pos = write_pos + 1;
		}
		if (ctrlPressed && shiftPressed && altPressed)
		{
			kernel_panic("Holy trinity!", HolyTrinity);
		}
	}
}