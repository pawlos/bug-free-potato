#include "mouse.h"

void mouse_wait(uint8_t type)
{
    uint32_t _time_out = 100000;
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
    else
    {
        while (_time_out--)
        {
            if((IO::inb(0x64) & 2) == 0)
            {
                return;
            }
        }
        return;
    }
}

static uint16_t _x = 0;
static uint16_t _y = 0;

void init_mouse(uint16_t max_x, uint16_t max_y)
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
    uint8_t status = IO::inb(0x60) | 2;

    mouse_wait(1);
    IO::outb(0x64, 0x60);
    mouse_wait(1);
    IO::outb(0x60, status);

    mouse_wait(1);
    IO::outb(0x64, 0xD4);
    mouse_wait(1);
    IO::outb(0x60, 0xF6);

    mouse_wait(0);
    uint8_t ack = IO::inb(0x60);
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


    _x = max_x;
    _y = max_y;
    klog("[MOUSE] Initialized\n");
}

mouse_state mouse {
    .pos_x = 100,
    .pos_y = 100,
    .left_button_pressed = false,
    .right_button_pressed = false
};

extern void DrawCursor(uint32_t x_pos, uint32_t y_pos);
extern void EraseCursor(uint32_t x_pos, uint32_t y_pos);

void mouse_routine(int8_t mouse_byte[])
{
    int8_t mouse_x = mouse_byte[1];
    int8_t mouse_y = mouse_byte[2];
    bool left_button_pressed  = mouse_byte[0] & 1;
    bool right_button_pressed = (mouse_byte[0] & 2) >> 1;

    EraseCursor(mouse.pos_x, mouse.pos_y);
    int16_t newPosX = mouse.pos_x + mouse_x;
    if (newPosX < 0)
        newPosX = 0;
    if (newPosX > _x)
        newPosX = _x;

    mouse.pos_x = newPosX;

    int16_t newPosY = mouse.pos_y - mouse_y;
    if (newPosY <0)
        newPosY = 0;
    if (newPosY > _y)
        newPosY = _y;

    mouse.pos_y = newPosY;
    mouse.left_button_pressed = left_button_pressed;
    mouse.right_button_pressed = right_button_pressed;
    DrawCursor(mouse.pos_x, mouse.pos_y);
}