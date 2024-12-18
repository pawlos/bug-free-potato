#include "mouse.h"
#include "io.h"
#include "kernel.h"
#include <framebuffer.h>


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

void mouse_routine(const pt::int8_t mouse_byte[])
{
    const pt::int8_t mouse_x = mouse_byte[1];
    const pt::int8_t mouse_y = mouse_byte[2];
    const bool left_button_pressed  = mouse_byte[0] & 1;
    const bool right_button_pressed = (mouse_byte[0] & 2) >> 1;

    EraseCursor(mouse.pos_x, mouse.pos_y);
    pt::int16_t newPosX = mouse.pos_x + mouse_x;
    if (newPosX < 0)
        newPosX = 0;
    if (newPosX > screen_max_x)
        newPosX = screen_max_x;

    mouse.pos_x = newPosX;

    pt::int16_t newPosY = mouse.pos_y - mouse_y;
    if (newPosY <0)
        newPosY = 0;
    if (newPosY > screen_max_y)
        newPosY = screen_max_y;

    mouse.pos_y = newPosY;
    mouse.left_button_pressed = left_button_pressed;
    mouse.right_button_pressed = right_button_pressed;
    DrawCursor(mouse.pos_x, mouse.pos_y);
}