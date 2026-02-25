#include "keyboard.h"
#include "com.h"
#include "kernel.h"
#include "window.h"

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
static bool e0_prefix = false;  // set when 0xE0 extended-key prefix is received

static char keyboard_buffer[128] = {0};
static int write_pos = 0;
static int read_pos = 0;

// Raw key-event ring buffer (press + release, for Doom / game input).
static KeyEvent event_buffer[128];
static int event_write = 0;
static int event_read  = 0;

static void push_key_event(pt::uint8_t scancode, bool pressed) {
    event_buffer[event_write % 128] = { scancode, pressed };
    event_write++;
    wm_route_key_event(wev_make_key(scancode, pressed));
}

bool get_key_event(KeyEvent* out) {
    if (event_read >= event_write) return false;
    *out = event_buffer[event_read % 128];
    event_read++;
    return true;
}

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

char keyboard_scancode_to_char(pt::uint8_t scancode) {
	if (scancode >= 128) return 0;
	const char* current_layout = shiftPressed || capsLockOn ? layout_upper : layout;
	return current_layout[scancode];
}

void keyboard_routine(const pt::uint8_t scancode)
{
	// E0 is the extended-key prefix byte (Right Ctrl, Right Alt, arrows, etc.).
	// Absorb it, set a flag, and wait for the actual key byte on the next IRQ.
	if (scancode == 0xE0) {
		e0_prefix = true;
		return;
	}
	// E1 prefix (used only by the Pause key sequence) — just ignore it.
	if (scancode == 0xE1) {
		return;
	}
	e0_prefix = false;  // consume the flag regardless of key

	if (scancode & 0x80)
	{
		//Key released
		const pt::uint8_t code = scancode & ~0x80;
		push_key_event(code, false);
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
		push_key_event(scancode, true);
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