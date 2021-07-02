#include "framebuffer.h"

void Framebuffer::Draw(const uint8_t* what, 
					   uint32_t x_pos, uint32_t y_pos,
					   uint32_t width, uint32_t height)
{
	uintptr_t fb_addr   = this->m_addr;
	uint32_t  fb_width  = this->m_width;
	uint32_t  fb_stride = this->m_stride;
	uint8_t   fb_bytes  = this->m_bpp / 8;

	//TODO: use bpp
	for (uint32_t y = y_pos; y < y_pos + height; y++)
	{
		for (uint32_t x = x_pos; x < x_pos + width ; x++)
		{
			uint32_t pos = y*3*width + x*3;
			uint32_t c =  (what[pos] << 16) 
						| (what[pos + 1] << 8) 
						| (what[pos + 2]);
			*(uint64_t *)(fb_addr + x*fb_bytes + y * fb_stride) = c;
		}
	}
}

static uint64_t prevPixel = 0x000000;
void Framebuffer::DrawCursor(uint32_t x_pos, uint32_t y_pos)
{
	uintptr_t fb_addr   = this->m_addr;
	uint32_t  fb_width  = this->m_width;
	uint32_t  fb_stride = this->m_stride;
	uint8_t   fb_bytes  = this->m_bpp / 8;
	prevPixel = *(uint64_t *)(fb_addr + x_pos*fb_bytes + y_pos*fb_stride);
	*(uint64_t *)(fb_addr + x_pos*fb_bytes + y_pos*fb_stride) = 0xff0000;
}

void Framebuffer::EraseCursor(uint32_t x_pos, uint32_t y_pos)
{
	uintptr_t fb_addr   = this->m_addr;
	uint32_t  fb_width  = this->m_width;
	uint32_t  fb_stride = this->m_stride;
	uint8_t   fb_bytes  = this->m_bpp / 8;
	*(uint64_t *)(fb_addr + x_pos*fb_bytes + y_pos*fb_stride) = prevPixel;
}

void Framebuffer::Clear(uint8_t r, uint8_t g, uint8_t b)
{
	uintptr_t fb_addr   = this->m_addr;
	uint32_t  fb_width  = this->m_width;
	uint32_t  fb_height  = this->m_height;
	uint32_t  fb_stride = this->m_stride;
	uint8_t   fb_bytes  = this->m_bpp / 8;

	uint32_t c =  (r << 16)
				| (g << 8)
				| (b);

	for (uint32_t y = 0; y < fb_height; y++)
	{
		for (uint32_t x = 0; x < fb_width ; x++)
		{
			*(uint64_t *)(fb_addr + x*fb_bytes + y * fb_stride) = c;
		}
	}
}