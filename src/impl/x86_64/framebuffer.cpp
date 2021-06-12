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