#include "keyboard.h"
#include "com.h"
#include "kernel.h"

const char layout[128] =
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

const char layout_upper[128] =
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

void keyboard_routine(uint8_t scancode)
{
	if (scancode & 0x80)
	{
		//Key released
		uint8_t code = scancode & ~0x80;
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
			const char* current_layout = shiftPressed ? layout_upper : layout;
			char key = current_layout[scancode];
		}
		if (ctrlPressed && shiftPressed && altPressed)
		{
			kernel_panic("Holy trinity!", HolyTrinity);
		}
	}
}