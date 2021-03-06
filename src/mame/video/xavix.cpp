// license:BSD-3-Clause
// copyright-holders:David Haywood

#include "emu.h"
#include "includes/xavix.h"

// #define VERBOSE 1
#include "logmacro.h"

inline void xavix_state::set_data_address(int address, int bit)
{
	m_tmp_dataaddress = address;
	m_tmp_databit = bit;
}

inline uint8_t xavix_state::get_next_bit()
{
	// going through memory is slow, try not to do it too often!
	if (m_tmp_databit == 0)
	{
		m_bit = m_maincpu->read_full_special(m_tmp_dataaddress);
	}

	uint8_t ret = m_bit >> m_tmp_databit;
	ret &= 1;

	m_tmp_databit++;

	if (m_tmp_databit == 8)
	{
		m_tmp_databit = 0;
		m_tmp_dataaddress++;
	}

	return ret;
}

inline uint8_t xavix_state::get_next_byte()
{
	uint8_t dat = 0;
	for (int i = 0; i < 8; i++)
	{
		dat |= (get_next_bit() << i);
	}
	return dat;
}

inline int xavix_state::get_current_address_byte()
{
	return m_tmp_dataaddress;
}


void xavix_state::video_start()
{
	m_screen->register_screen_bitmap(m_zbuffer);
}


double xavix_state::hue2rgb(double p, double q, double t)
{
	if (t < 0) t += 1;
	if (t > 1) t -= 1;
	if (t < 1 / 6.0f) return p + (q - p) * 6 * t;
	if (t < 1 / 2.0f) return q;
	if (t < 2 / 3.0f) return p + (q - p) * (2 / 3.0f - t) * 6;
	return p;
}

void xavix_state::handle_palette(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect, uint8_t* ramsh, uint8_t* raml, int size, int basecol)
{
	// not verified
	int offs = 0;
	for (int index = 0; index < size; index++)
	{
		uint16_t dat;
		dat = ramsh[offs];
		dat |= raml[offs] << 8;
	
		offs++;

		int l_raw = (dat & 0x1f00) >> 8;
		int s_raw = (dat & 0x00e0) >> 5;
		int h_raw = (dat & 0x001f) >> 0;

		//if (h_raw > 24)
		//  LOG("hraw >24 (%02x)\n", h_raw);

		//if (l_raw > 17)
		//  LOG("lraw >17 (%02x)\n", l_raw);

		//if (s_raw > 7)
		//  LOG("s_raw >5 (%02x)\n", s_raw);

		double l = (double)l_raw / 17.0f;
		double s = (double)s_raw / 7.0f;
		double h = (double)h_raw / 24.0f; // hue values 24-31 render as transparent

		double r, g, b;

		if (s == 0) {
			r = g = b = l; // greyscale
		}
		else {
			double q = l < 0.5f ? l * (1 + s) : l + s - l * s;
			double p = 2 * l - q;
			r = hue2rgb(p, q, h + 1 / 3.0f);
			g = hue2rgb(p, q, h);
			b = hue2rgb(p, q, h - 1 / 3.0f);
		}

		int r_real = r * 255.0f;
		int g_real = g * 255.0f;
		int b_real = b * 255.0f;

		m_palette->set_pen_color(basecol+index, r_real, g_real, b_real);

	}
}

void xavix_state::draw_tilemap(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect, int which)
{
	for (int y = cliprect.min_y; y <= cliprect.max_y; y++)
	{
		draw_tilemap_line(screen, bitmap, cliprect, which, y);
	}
}


void xavix_state::draw_tilemap_line(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect, int which, int line)
{
	uint8_t* tileregs;
	if (which == 0)
	{
		tileregs = m_tmap1_regs;
	}
	else
	{
		tileregs = m_tmap2_regs;
	}

	// bail if tilemap is disabled
	if (!(tileregs[0x7] & 0x80))
		return;

	int alt_tileaddressing = 0;
	int alt_tileaddressing2 = 0;

	int ydimension = 0;
	int xdimension = 0;
	int ytilesize = 0;
	int xtilesize = 0;
	int yshift = 0;

	switch (tileregs[0x3] & 0x30)
	{
	case 0x00:
		ydimension = 32;
		xdimension = 32;
		ytilesize = 8;
		xtilesize = 8;
		yshift = 3;
		break;

	case 0x10:
		ydimension = 32;
		xdimension = 16;
		ytilesize = 8;
		xtilesize = 16;
		yshift = 3;
		break;

	case 0x20: // guess
		ydimension = 16;
		xdimension = 32;
		ytilesize = 16;
		xtilesize = 8;
		yshift = 4;
		break;

	case 0x30:
		ydimension = 16;
		xdimension = 16;
		ytilesize = 16;
		xtilesize = 16;
		yshift = 4;
		break;
	}

	if (tileregs[0x7] & 0x10)
		alt_tileaddressing = 1;
	else
		alt_tileaddressing = 0;

	if (tileregs[0x7] & 0x02)
		alt_tileaddressing2 = 1;

	if ((tileregs[0x7] & 0x7f) == 0x04)
		alt_tileaddressing2 = 2;

	//LOG("draw tilemap %d, regs base0 %02x base1 %02x base2 %02x tilesize,bpp %02x scrollx %02x scrolly %02x pal %02x mode %02x\n", which, tileregs[0x0], tileregs[0x1], tileregs[0x2], tileregs[0x3], tileregs[0x4], tileregs[0x5], tileregs[0x6], tileregs[0x7]);

	// there's a tilemap register to specify base in main ram, although in the monster truck test mode it points to an unmapped region
	// and expected a fixed layout, we handle that in the memory map at the moment

	int drawline = line;
	int scrolly = tileregs[0x5];

	drawline += scrolly;

	drawline &= ((ydimension * ytilesize) - 1);

	int y = drawline >> yshift;
	int yyline = drawline & (ytilesize - 1);

	for (int x = 0; x < xdimension; x++)
	{
		int count = (y * xdimension) + x;

		int tile = 0;

		// the register being 0 probably isn't the condition here
		if (tileregs[0x0] != 0x00) tile |= m_maincpu->read_full_special((tileregs[0x0] << 8) + count);

		// only read the next byte if we're not in an 8-bit mode
		if (((tileregs[0x7] & 0x7f) != 0x00) && ((tileregs[0x7] & 0x7f) != 0x08))
			tile |= m_maincpu->read_full_special((tileregs[0x1] << 8) + count) << 8;

		// 24 bit modes can use reg 0x2, otherwise it gets used as extra attribute in other modes
		if (alt_tileaddressing2 == 2)
			tile |= m_maincpu->read_full_special((tileregs[0x2] << 8) + count) << 16;


		int bpp = (tileregs[0x3] & 0x0e) >> 1;
		bpp++;
		int pal = (tileregs[0x6] & 0xf0) >> 4;
		int zval = (tileregs[0x6] & 0x0f) >> 0;
		int scrollx = tileregs[0x4];

		int basereg;
		int flipx = 0;
		int flipy = 0;
		int gfxbase;

		// tile 0 is always skipped, doesn't even point to valid data packets in alt mode
		// this is consistent with the top layer too
		// should we draw as solid in solid layer?
		if (tile == 0)
		{
			continue;
		}

		const int debug_packets = 0;
		int test = 0;

		if (!alt_tileaddressing)
		{
			if (alt_tileaddressing2 == 0)
			{
				// Tile Based Addressing takes into account Tile Sizes and bpp
				const int offset_multiplier = (ytilesize * xtilesize) / 8;

				basereg = 0;
				gfxbase = (m_segment_regs[(basereg * 2) + 1] << 16) | (m_segment_regs[(basereg * 2)] << 8);

				tile = tile * (offset_multiplier * bpp);
				tile += gfxbase;
			}
			else if (alt_tileaddressing2 == 1)
			{
				// 8-byte alignment Addressing Mode uses a fixed offset? (like sprites)
				tile = tile * 8;
				basereg = (tile & 0x70000) >> 16;
				tile &= 0xffff;
				gfxbase = (m_segment_regs[(basereg * 2) + 1] << 16) | (m_segment_regs[(basereg * 2)] << 8);
				tile += gfxbase;
			}
			else if (alt_tileaddressing2 == 2)
			{
				// 24-bit addressing (check if this is still needed)
				//tile |= 0x800000;
			}

			// Tilemap specific mode extension with an 8-bit per tile attribute, works in all modes except 24-bit (no room for attribute) and header (not needed?)
			if (tileregs[0x7] & 0x08)
			{
				uint8_t extraattr = m_maincpu->read_full_special((tileregs[0x2] << 8) + count);
				// make use of the extraattr stuff?
				pal = (extraattr & 0xf0) >> 4;
				zval = (extraattr & 0x0f) >> 0;
			}
		}
		else
		{
			// Addressing Mode 2 (plus Inline Header)

			if (debug_packets) LOG("for tile %04x (at %d %d): ", tile, (((x * 16) + scrollx) & 0xff), (((y * 16) + scrolly) & 0xff));


			basereg = (tile & 0xf000) >> 12;
			tile &= 0x0fff;
			gfxbase = (m_segment_regs[(basereg * 2) + 1] << 16) | (m_segment_regs[(basereg * 2)] << 8);

			tile += gfxbase;
			set_data_address(tile, 0);

			// there seems to be a packet stored before the tile?!
			// the offset used for flipped sprites seems to specifically be changed so that it picks up an extra byte which presumably triggers the flipping
			uint8_t byte1 = 0;
			int done = 0;
			int skip = 0;

			do
			{
				byte1 = get_next_byte();

				if (debug_packets) LOG(" %02x, ", byte1);

				if (skip == 1)
				{
					skip = 0;
					//test = 1;
				}
				else if ((byte1 & 0x0f) == 0x01)
				{
					// used
				}
				else if ((byte1 & 0x0f) == 0x03)
				{
					// causes next byte to be skipped??
					skip = 1;
				}
				else if ((byte1 & 0x0f) == 0x05)
				{
					// the upper bits are often 0x00, 0x10, 0x20, 0x30, why?
					flipx = 1;
				}
				else if ((byte1 & 0x0f) == 0x06) // there must be other finish conditions too because sometimes this fails..
				{
					// tile data will follow after this, always?
					pal = (byte1 & 0xf0) >> 4;
					done = 1;
				}
				else if ((byte1 & 0x0f) == 0x07)
				{
					// causes next byte to be skipped??
					skip = 1;
				}
				else if ((byte1 & 0x0f) == 0x09)
				{
					// used
				}
				else if ((byte1 & 0x0f) == 0x0a)
				{
					// not seen
				}
				else if ((byte1 & 0x0f) == 0x0b)
				{
					// used
				}
				else if ((byte1 & 0x0f) == 0x0c)
				{
					// not seen
				}
				else if ((byte1 & 0x0f) == 0x0d)
				{
					// used
				}
				else if ((byte1 & 0x0f) == 0x0e)
				{
					// not seen
				}
				else if ((byte1 & 0x0f) == 0x0f)
				{
					// used
				}

			} while (done == 0);
			if (debug_packets) LOG("\n");
			tile = get_current_address_byte();
		}

		if (test == 1) pal = machine().rand() & 0xf;

		draw_tile_line(screen, bitmap, cliprect, tile, bpp, (x * xtilesize) + scrollx, line, ytilesize, xtilesize, flipx, flipy, pal, zval, yyline);
		draw_tile_line(screen, bitmap, cliprect, tile, bpp, ((x * xtilesize) + scrollx) - 256, line, ytilesize, xtilesize, flipx, flipy, pal, zval, yyline); // wrap-x
	}
}

void xavix_state::draw_sprites(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	for (int y = cliprect.min_y; y <= cliprect.max_y; y++)
	{
		draw_sprites_line(screen, bitmap, cliprect, y);
	}
}

void xavix_state::draw_sprites_line(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect, int line)
{
	int alt_addressing = 0;

	if ((m_spritereg == 0x00) || (m_spritereg == 0x01))
	{
		// 8-bit addressing  (Tile Number)
		// 16-bit addressing (Tile Number) (rad_rh)
		alt_addressing = 1;
	}
	else if (m_spritereg == 0x02)
	{
		// 16-bit addressing (8-byte alignment Addressing Mode)
		alt_addressing = 2;
	}
	else if (m_spritereg == 0x04)
	{
		// 24-bit addressing (Addressing Mode 2)
		alt_addressing = 0;
	}
	else
	{
		popmessage("unknown sprite reg %02x", m_spritereg);
	}


	//LOG("frame\n");
	// priority doesn't seem to be based on list order, there are bad sprite-sprite priorities with either forward or reverse

	for (int i = 0xff; i >= 0; i--)
	{

		uint8_t* spr_attr0 = m_fragment_sprite + 0x000;
		uint8_t* spr_attr1 = m_fragment_sprite + 0x100;
		uint8_t* spr_ypos = m_fragment_sprite + 0x200;
		uint8_t* spr_xpos = m_fragment_sprite + 0x300;
		uint8_t* spr_xposh = m_fragment_sprite + 0x400;
		uint8_t* spr_addr_lo = m_fragment_sprite + 0x500;
		uint8_t* spr_addr_md = m_fragment_sprite + 0x600;
		uint8_t* spr_addr_hi = m_fragment_sprite + 0x700;


		/* attribute 0 bits
		   pppp bbb-    p = palette, b = bpp

		   attribute 1 bits
		   zzzz ssFf    s = size, F = flipy f = flipx
		*/

		int ypos = spr_ypos[i];
		int xpos = spr_xpos[i];
		int tile = 0;

		// high 8-bits only used in 24-bit mode
		if (((m_spritereg & 0x7f) == 0x04) || ((m_spritereg & 0x7f) == 0x15))
			tile |= (spr_addr_hi[i] << 16);

		// mid 8-bits used in everything except 8-bit mode
		if ((m_spritereg & 0x7f) != 0x00)
			tile |= (spr_addr_md[i] << 8);

		// low 8-bits always read
		tile |= spr_addr_lo[i];

		int attr0 = spr_attr0[i];
		int attr1 = spr_attr1[i];

		int pal = (attr0 & 0xf0) >> 4;

		int zval = (attr1 & 0xf0) >> 4;
		int flipx = (attr1 & 0x01);
		int flipy = (attr1 & 0x02);

		int drawheight = 16;
		int drawwidth = 16;

		int xpos_adjust = 0;
		int ypos_adjust = -16;

		// taito nost attr1 is 84 / 80 / 88 / 8c for the various elements of the xavix logo.  monster truck uses ec / fc / dc / 4c / 5c / 6c (final 6 sprites ingame are 00 00 f0 f0 f0 f0, radar?)

		if ((attr1 & 0x0c) == 0x0c)
		{
			drawheight = 16;
			drawwidth = 16;
		}
		else if ((attr1 & 0x0c) == 0x08)
		{
			drawheight = 16;
			drawwidth = 8;
			xpos_adjust += 4;
		}
		else if ((attr1 & 0x0c) == 0x04)
		{
			drawheight = 8;
			drawwidth = 16;
			ypos_adjust -= 4;
		}
		else if ((attr1 & 0x0c) == 0x00)
		{
			drawheight = 8;
			drawwidth = 8;
			xpos_adjust += 4;
			ypos_adjust -= 4;
		}

		ypos ^= 0xff;

		if (ypos & 0x80)
		{
			ypos = -0x80 + (ypos & 0x7f);
		}
		else
		{
			ypos &= 0x7f;
		}

		ypos += 128 - 15 - 8;

		ypos -= ypos_adjust;

		int spritelowy = ypos;
		int spritehighy = ypos + drawheight;

		if ((line >= spritelowy) && (line < spritehighy))
		{
			int drawline = line - spritelowy;


			/* coordinates are signed, based on screen position 0,0 being at the center of the screen
			   tile addressing likewise, point 0,0 is center of tile?
			   this makes the calculation a bit more annoying in terms of knowing when to apply offsets, when to wrap etc.
			   this is likely still incorrect

			   Use of additional x-bit is very confusing rad_snow, taitons1 (ingame) etc. clearly need to use it
			   but the taitons1 xavix logo doesn't even initialize the RAM for it and behavior conflicts with ingame?
			   maybe only works with certain tile sizes?

			   some code even suggests this should be bit 0 of attr0, but it never gets set there

			   there must be a register somewhere (or a side-effect of another mode) that enables / disables this
			   behavior, as we need to make use of xposh for the left side in cases that need it, but that
			   completely breaks the games that never set it at all (monster truck, xavix logo on taitons1)

			 */

			int xposh = spr_xposh[i] & 1;

			if (xpos & 0x80) // left side of center
			{
				xpos &= 0x7f;
				xpos = -0x80 + xpos;
			}
			else // right side of center
			{
				xpos &= 0x7f;

				if (xposh)
					xpos += 0x80;
			}

			xpos += 128 - 8;




			// Everything except directdirect addressing (Addressing Mode 2) goes through the segment registers?
			if (alt_addressing != 0)
			{
				// tile based addressing takes into account tile size (and bpp?)
				if (alt_addressing == 1)
					tile = (tile * drawheight * drawwidth) / 2;

				// 8-byte alignment Addressing Mode uses a fixed offset?
				if (alt_addressing == 2)
					tile = tile * 8;

				int basereg = (tile & 0xf0000) >> 16;
				tile &= 0xffff;
				int gfxbase = (m_segment_regs[(basereg * 2) + 1] << 16) | (m_segment_regs[(basereg * 2)] << 8);
				tile += gfxbase;
			}

	


			int bpp = 1;

			bpp = (attr0 & 0x0e) >> 1;
			bpp += 1;

			draw_tile_line(screen, bitmap, cliprect, tile, bpp, xpos, line, drawheight, drawwidth, flipx, flipy, pal, zval, drawline);

			/*
			if ((spr_ypos[i] != 0x81) && (spr_ypos[i] != 0x80) && (spr_ypos[i] != 0x00))
			{
				LOG("sprite with enable? %02x attr0 %02x attr1 %02x attr3 %02x attr5 %02x attr6 %02x attr7 %02x\n", spr_ypos[i], spr_attr0[i], spr_attr1[i], spr_xpos[i], spr_addr_lo[i], spr_addr_md[i], spr_addr_hi[i] );
			}
			*/
		}
	}
}

void xavix_state::draw_tile_line(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect, int tile, int bpp, int xpos, int ypos, int drawheight, int drawwidth, int flipx, int flipy, int pal, int zval, int line)
{
	//const pen_t *paldata = m_palette->pens();

	if (flipy)
		line = drawheight - line;

	if (ypos > cliprect.max_y || ypos < cliprect.min_y)
		return;

	if ((xpos > cliprect.max_x) || ((xpos + drawwidth) < cliprect.min_x))
		return;


	if ((ypos >= cliprect.min_y && ypos <= cliprect.max_y))
	{
		int bits_per_tileline = drawwidth * bpp;

		// set the address here so we can increment in bits in the draw function
		set_data_address(tile, 0);
		m_tmp_dataaddress = m_tmp_dataaddress + ((line * bits_per_tileline) / 8);
		m_tmp_databit = (line * bits_per_tileline) % 8;

		for (int x = 0; x < drawwidth; x++)
		{
			int col;

			if (flipx)
			{
				col = xpos + (drawwidth - 1) - x;
			}
			else
			{
				col = xpos + x;
			}

			uint8_t dat = 0;

			for (int i = 0; i < bpp; i++)
			{
				dat |= (get_next_bit() << i);
			}

			if ((col >= cliprect.min_x && col <= cliprect.max_x))
			{
				uint16_t* zyposptr = &m_zbuffer.pix16(ypos);

				if (zval >= zyposptr[col])
				{
					int pen = (dat + (pal << 4)) & 0xff;

					if ((m_palram_sh[pen] & 0x1f) < 24) // hue values 24-31 are transparent
					{
						uint16_t* yposptr = &bitmap.pix16(ypos);
						yposptr[col] = pen;

						//uint32_t* yposptr = &bitmap.pix32(ypos);
						//yposptr[col] = paldata[pen];

						zyposptr[col] = zval;
					}
				}
			}
		}
	}
}

uint32_t xavix_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	handle_palette(screen, bitmap, cliprect, m_palram_sh, m_palram_l, 256, 0);

	if (m_bmp_palram_sh)
	{
		handle_palette(screen, bitmap, cliprect, m_bmp_palram_sh, m_bmp_palram_l, 256, 256);
		handle_palette(screen, bitmap, cliprect, m_colmix_sh, m_colmix_l, 1, 512);
	}
	else
	{
		handle_palette(screen, bitmap, cliprect, m_colmix_sh, m_colmix_l, 1, 256);
	}

	// not sure what you end up with if you fall through all layers as transparent, so far no issues noticed
	bitmap.fill(m_palette->black_pen(), cliprect);
	m_zbuffer.fill(0, cliprect);




	rectangle clip = cliprect;

	clip.min_y = cliprect.min_y;
	clip.max_y = cliprect.max_y;
	clip.min_x = cliprect.min_x;
	clip.max_x = cliprect.max_x;

	if (m_arena_control & 0x01)
	{
		/* Controls the clipping area (for all layers?) used for effect at start of Slap Fight and to add black borders in other cases
		   based on Slap Fight Tiger lives display (and reference videos) this is slightly offset as all status bar gfx must display
		   Monster Truck black bars are wider on the right hand side, but this matches with the area in which the tilemap is incorrectly rendered so seems to be intentional
		   Snowboard hides garbage sprites on the right hand side with this, confirming the right hand side offset
		   Taito Nostalgia 1 'Gladiator' portraits in demo mode are cut slightly due to the area specified, again the cut-off points for left and right are confirmed as correct on hardware

		   some games enable it with both regs as 00, which causes a problem, likewise ping pong sets both to 0xff
		   but Slap Fight Tiger has both set to 0x82 at a time when everything should be clipped
		*/
		if (((m_arena_start != 0x00) && (m_arena_end != 0x00)) && ((m_arena_start != 0xff) && (m_arena_end != 0xff)))
		{
			clip.max_x = m_arena_start - 3; // must be -3 to hide garbage on the right hand side of snowboarder
			clip.min_x = m_arena_end - 2; // must be -2 to render a single pixel line of the left border on Mappy remix (verified to render), although this creates a single pixel gap on the left of snowboarder status bar (need to verify)

			if (clip.min_x < cliprect.min_x)
				clip.min_x = cliprect.min_x;

			if (clip.max_x > cliprect.max_x)
				clip.max_x = cliprect.max_x;
		}
	}
	bitmap.fill(0, clip);

	draw_tilemap(screen, bitmap, clip, 1);
	draw_tilemap(screen, bitmap, clip, 0);
	draw_sprites(screen, bitmap, clip);

	//popmessage("%02x %02x %02x %02x   %02x %02x %02x %02x   %02x %02x %02x %02x   %02x %02x %02x %02x", m_soundregs[0],m_soundregs[1],m_soundregs[2],m_soundregs[3],m_soundregs[4],m_soundregs[5],m_soundregs[6],m_soundregs[7],m_soundregs[8],m_soundregs[9],m_soundregs[10],m_soundregs[11],m_soundregs[12],m_soundregs[13],m_soundregs[14],m_soundregs[15]);

	// temp, needs priority, transparency etc. also it's far bigger than the screen, I guess it must get scaled?!
	if (m_bmp_base)
	{
		if (m_bmp_base[0x14] & 0x01)
		{
			popmessage("bitmap %02x %02x %02x %02x %02x %02x %02x %02x  -- -- %02x %02x %02x %02x %02x %02x  -- -- %02x %02x %02x %02x %02x %02x",
				m_bmp_base[0x00], m_bmp_base[0x01], m_bmp_base[0x02], m_bmp_base[0x03], m_bmp_base[0x04], m_bmp_base[0x05], m_bmp_base[0x06], m_bmp_base[0x07],
				/*m_bmp_base[0x08], m_bmp_base[0x09],*/ m_bmp_base[0x0a], m_bmp_base[0x0b], m_bmp_base[0x0c], m_bmp_base[0x0d], m_bmp_base[0x0e], m_bmp_base[0x0f],
				/*m_bmp_base[0x10], m_bmp_base[0x11],*/ m_bmp_base[0x12], m_bmp_base[0x13], m_bmp_base[0x14], m_bmp_base[0x15], m_bmp_base[0x16], m_bmp_base[0x17]);

			int base = ((m_bmp_base[0x11] << 8) | m_bmp_base[0x10]) * 0x800;
			int base2 = ((m_bmp_base[0x09] << 8) | m_bmp_base[0x08]) * 0x8;

			//int count = 0;
			set_data_address(base + base2, 0);
	
			for (int y = 0; y < 256; y++)
			{
				for (int x = 0; x < 512; x++)
				{
					uint16_t* yposptr = &bitmap.pix16(y);

					int bpp = 6;

					uint8_t dat = 0;
					for (int i = 0; i < bpp; i++)
					{
						dat |= (get_next_bit() << i);
					}

					yposptr[x] = dat + 0x100;
				}
			}

		}
	}

	return 0;
}


WRITE8_MEMBER(xavix_state::spritefragment_dma_params_1_w)
{
	m_spritefragment_dmaparam1[offset] = data;
}

WRITE8_MEMBER(xavix_state::spritefragment_dma_params_2_w)
{
	m_spritefragment_dmaparam2[offset] = data;
}

WRITE8_MEMBER(xavix_state::spritefragment_dma_trg_w)
{
	uint16_t len = data & 0x07;
	uint16_t src = (m_spritefragment_dmaparam1[1] << 8) | m_spritefragment_dmaparam1[0];
	uint16_t dst = (m_spritefragment_dmaparam2[0] << 8);

	uint8_t unk = m_spritefragment_dmaparam2[1];

	LOG("%s: spritefragment_dma_trg_w with trg %02x size %04x src %04x dest %04x unk (%02x)\n", machine().describe_context(), data & 0xf8, len, src, dst, unk);

	if (unk)
	{
		fatalerror("m_spritefragment_dmaparam2[1] != 0x00 (is %02x)\n", m_spritefragment_dmaparam2[1]);
	}

	if (len == 0x00)
	{
		len = 0x08;
		LOG(" (length was 0x0, assuming 0x8)\n");
	}

	len = len << 8;

	if (data & 0x40)
	{
		for (int i = 0; i < len; i++)
		{
			uint8_t dat = m_maincpu->read_full_special(src + i);
			m_fragment_sprite[(dst + i) & 0x7ff] = dat;
		}
	}
}

READ8_MEMBER(xavix_state::spritefragment_dma_status_r)
{
	// expects bit 0x40 to clear in most cases
	return 0x00;
}

READ8_MEMBER(xavix_state::pal_ntsc_r)
{
	// only seen 0x10 checked in code
	// in monster truck the tile base address gets set based on this, there are 2 copies of the test screen in rom, one for pal, one for ntsc, see 1854c
	// likewise card night has entirely different tilesets for each region title
	return m_region->read();
}


WRITE8_MEMBER(xavix_state::tmap1_regs_w)
{
	/*
	   0x0 pointer to low tile bits
	   0x1 pointer to middle tile bits
	   0x2 pointer to upper tile bits

	   0x3 Fftt bbb-  Ff = flip Y,X
					  tt = tile/tilemap size
					  b = bpp
					  - = unused

	   0x4 scroll
	   0x5 scroll

	   0x6 pppp zzzz  p = palette
					  z = priority

	   0x7 e--m mmmm  e = enable
					  m = mode

	   modes are
		---0 0000 (00) 8-bit addressing  (Tile Number)
		---0 0001 (01) 16-bit addressing (Tile Number) (monster truck, ekara)
		---0 0010 (02) 16-bit addressing (8-byte alignment Addressing Mode) (boxing)
		---0 0011 (03) 16-bit addressing (Addressing Mode 2)
		---0 0100 (04) 24-bit addressing (Addressing Mode 2) (epo_efdx)

		---0 1000 (08) 8-bit+8 addressing  (Tile Number + 8-bit Attribute)
		---0 1001 (09) 16-bit+8 addressing (Tile Number + 8-bit Attribute) (Taito Nostalgia 2)
		---0 1010 (0a) 16-bit+8 addressing (8-byte alignment Addressing Mode + 8-bit Attribute) (boxing, Snowboard)
		---0 1011 (0b) 16-bit+8 addressing (Addressing Mode 2 + 8-bit Attribute)

		---1 0011 (13) 16-bit addressing (Addressing Mode 2 + Inline Header)  (monster truck)
		---1 0100 (14) 24-bit addressing (Addressing Mode 2 + Inline Header)

	*/

	if ((offset != 0x4) && (offset != 0x5))
	{
		LOG("%s: tmap1_regs_w offset %02x data %02x\n", machine().describe_context(), offset, data);
	}

	COMBINE_DATA(&m_tmap1_regs[offset]);
}

WRITE8_MEMBER(xavix_state::tmap2_regs_w)
{
	// same as above but for 2nd tilemap
	if ((offset != 0x4) && (offset != 0x5))
	{
		LOG("%s: tmap2_regs_w offset %02x data %02x\n", machine().describe_context(), offset, data);
	}

	COMBINE_DATA(&m_tmap2_regs[offset]);
}


WRITE8_MEMBER(xavix_state::spriteregs_w)
{
	LOG("%s: spriteregs_w data %02x\n", machine().describe_context(), data);
	/*
		This is similar to Tilemap reg 7 and is used to set the addressing mode for sprite data

		---0 -000 (00) 8-bit addressing  (Tile Number)
		---0 -001 (01) 16-bit addressing (Tile Number)
		---0 -010 (02) 16-bit addressing (8-byte alignment Addressing Mode)
		---0 -011 (03) 16-bit addressing (Addressing Mode 2)
		---0 -100 (04) 24-bit addressing (Addressing Mode 2)

		---1 -011 (13) 16-bit addressing (Addressing Mode 2 + Inline Header)
		---1 -100 (14) 24-bit addressing (Addressing Mode 2 + Inline Header)
	*/
	m_spritereg = data;
}

READ8_MEMBER(xavix_state::tmap1_regs_r)
{
	LOG("%s: tmap1_regs_r offset %02x\n", offset, machine().describe_context());
	return m_tmap1_regs[offset];
}

READ8_MEMBER(xavix_state::tmap2_regs_r)
{
	LOG("%s: tmap2_regs_r offset %02x\n", offset, machine().describe_context());
	return m_tmap2_regs[offset];
}

// The Text Array / Memory Emulator acts as a memory area that you can point the tilemap sources at to get a fixed pattern of data
WRITE8_MEMBER(xavix_state::xavix_memoryemu_txarray_w)
{
	if (offset < 0x400)
	{
		// nothing
	}
	else if (offset < 0x800)
	{
		m_txarray[0] = data;
	}
	else if (offset < 0xc00)
	{
		m_txarray[1] = data;
	}
	else if (offset < 0x1000)
	{
		m_txarray[2] = data;
	}
}

READ8_MEMBER(xavix_state::xavix_memoryemu_txarray_r)
{
	if (offset < 0x100)
	{
		offset &= 0xff;
		return ((offset >> 4) | (offset << 4));
	}
	else if (offset < 0x200)
	{
		offset &= 0xff;
		return ((offset >> 4) | (~offset << 4));
	}
	else if (offset < 0x300)
	{
		offset &= 0xff;
		return ((~offset >> 4) | (offset << 4));
	}
	else if (offset < 0x400)
	{
		offset &= 0xff;
		return ((~offset >> 4) | (~offset << 4));
	}
	else if (offset < 0x800)
	{
		return m_txarray[0];
	}
	else if (offset < 0xc00)
	{
		return m_txarray[1];
	}
	else if (offset < 0x1000)
	{
		return m_txarray[2];
	}

	return 0xff;
}
