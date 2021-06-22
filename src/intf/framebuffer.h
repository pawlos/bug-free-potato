#pragma once
#include "defs.h"
#include "boot.h"

class Framebuffer
{
private:
	uintptr_t m_addr;
	uint32_t  m_width;
	uint32_t  m_height;
	uint32_t  m_bpp;
	uint32_t  m_stride;
public:
	Framebuffer(uintptr_t addr, uint32_t width, 
				uint32_t height, uint32_t bpp, 
				uint32_t stride) : m_addr(addr), m_width(width),
								   m_height(height), m_bpp(bpp),
								   m_stride(stride)
	{}

	Framebuffer(boot_framebuffer *fb) : Framebuffer(
											 fb->framebuffer_addr,
											 fb->framebuffer_width,
											 fb->framebuffer_height,
											 fb->framebuffer_bpp,
											 fb->framebuffer_pitch)
	{}

	void Draw(const uint8_t* what, 
			  uint32_t width, uint32_t height, 
			  uint32_t x_pos, uint32_t y_pos);

	void Clear(uint8_t r, uint8_t g, uint8_t b);
};