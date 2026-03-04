#include "device/mouse.h"
#include "io.h"
#include "kernel.h"
#include <framebuffer.h>
#include "window.h"


void DrawCursor(const pt::uint32_t x_pos, const pt::uint32_t y_pos)
{
    Framebuffer::get_instance()->DrawCursor(x_pos, y_pos);
}

void EraseCursor(const pt::uint32_t x_pos, const pt::uint32_t y_pos)
{
    Framebuffer::get_instance()->EraseCursor(x_pos, y_pos);
}

void mouse_wait(const pt::uint8_t type)
{
    pt::uint32_t _time_out = 100000;
    if (type == 0)
    {
        while (_time_out--)
        {
            if((IO::inb(0x64) & 1) == 1)
            {
                return;
            }
        }
        return;
    }
    while (_time_out--)
    {
        if((IO::inb(0x64) & 2) == 0)
        {
            return;
        }
    }
}

static pt::int16_t screen_max_x = 0;
static pt::int16_t screen_max_y = 0;

void init_mouse(const pt::int16_t max_x, const pt::int16_t max_y)
{
    klog("[MOUSE] Init mouse\n");

    mouse_wait(1);
    IO::outb(0x64, 0xA8);
    klog("[MOUSE] Enabled AUX device\n");

    mouse_wait(1);
    IO::outb(0x64, 0xFF);
    klog("[MOUSE] Reset\n");
    mouse_wait(0);
    IO::inb(0x60);

    mouse_wait(1);
    IO::outb(0x64, 0x20);
    klog("[MOUSE] Enabled IRQ\n");

    mouse_wait(0);
    const pt::uint8_t status = IO::inb(0x60) | 2;

    mouse_wait(1);
    IO::outb(0x64, 0x60);
    mouse_wait(1);
    IO::outb(0x60, status);

    mouse_wait(1);
    IO::outb(0x64, 0xD4);
    mouse_wait(1);
    IO::outb(0x60, 0xF6);

    mouse_wait(0);
    pt::uint8_t ack = IO::inb(0x60);
    if (ack != 0xFA)
        kernel_panic("Mouse did not ACKed defaults!", MouseNotAcked);

    mouse_wait(1);
    IO::outb(0x64, 0xD4);
    mouse_wait(1);
    IO::outb(0x60, 0xF4);

    mouse_wait(0);
    ack = IO::inb(0x60);
    if (ack != 0xFA)
        kernel_panic("Mouse did not ACKed enable!", MouseNotAcked);


    screen_max_x = max_x;
    screen_max_y = max_y;
    klog("[MOUSE] Initialized\n");
}

mouse_state mouse {
    .pos_x = 100,
    .pos_y = 100,
    .left_button_pressed = false,
    .right_button_pressed = false
};

// ── Mouse delta event ring buffer ────────────────────────────────────────
static constexpr int MOUSE_BUF_SIZE = 64;
static MouseEvent mouse_event_buf[MOUSE_BUF_SIZE];
static int mouse_write_pos = 0;
static int mouse_read_pos  = 0;

bool get_mouse_event(MouseEvent* out) {
    if (mouse_read_pos == mouse_write_pos) return false;
    *out = mouse_event_buf[mouse_read_pos % MOUSE_BUF_SIZE];
    mouse_read_pos++;
    return true;
}

static bool prev_left_button = false;

void mouse_routine(const pt::int8_t mouse_byte[])
{
    const pt::int8_t mouse_x = mouse_byte[1];
    const pt::int8_t mouse_y = mouse_byte[2];
    const bool left_button_pressed  = mouse_byte[0] & 1;
    const bool right_button_pressed = (mouse_byte[0] & 2) >> 1;

    // Queue delta event before updating absolute position.
    MouseEvent ev;
    ev.dx           = mouse_x;
    ev.dy           = mouse_y;  // PS/2 convention: positive = up
    ev.left_button  = left_button_pressed;
    ev.right_button = right_button_pressed;
    mouse_event_buf[mouse_write_pos % MOUSE_BUF_SIZE] = ev;
    mouse_write_pos++;

    EraseCursor(mouse.pos_x, mouse.pos_y);
    pt::int16_t newPosX = mouse.pos_x + mouse_x;
    if (newPosX < -(CURSOR_WIDTH - MIN_CURSOR_VISIBLE))
        newPosX = -(CURSOR_WIDTH - MIN_CURSOR_VISIBLE);
    if (newPosX >= screen_max_x - MIN_CURSOR_VISIBLE)
        newPosX = screen_max_x - MIN_CURSOR_VISIBLE;

    mouse.pos_x = newPosX;

    pt::int16_t newPosY = mouse.pos_y - mouse_y;
    if (newPosY < -(CURSOR_HEIGHT - MIN_CURSOR_VISIBLE))
        newPosY = -(CURSOR_HEIGHT - MIN_CURSOR_VISIBLE);
    if (newPosY >= screen_max_y - MIN_CURSOR_VISIBLE)
        newPosY = screen_max_y - MIN_CURSOR_VISIBLE;

    mouse.pos_y = newPosY;
    mouse.left_button_pressed = left_button_pressed;
    mouse.right_button_pressed = right_button_pressed;
    DrawCursor(mouse.pos_x, mouse.pos_y);

    // Click-to-focus: detect left-button press edge (rising edge only)
    bool left_clicked = left_button_pressed && !prev_left_button;
    prev_left_button = left_button_pressed;
    if (left_clicked) {
        pt::uint32_t hit = WindowManager::window_at(mouse.pos_x, mouse.pos_y);
        WindowManager::set_focus(hit);  // INVALID_WID clears focus (desktop click)
    }
}