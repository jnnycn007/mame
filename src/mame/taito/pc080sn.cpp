// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria
/*
Taito PC080SN
-------
Tilemap generator. Two tilemaps, with gfx data fetched from ROM.
Darius uses 3xPC080SN and has double width tilemaps. (NB: it
has not been verified that Topspeed uses this chip. Possibly
it had a variant with added rowscroll capability.)

Standard memory layout (two 64x64 tilemaps with 8x8 tiles)

0000-3fff BG
4000-41ff BG rowscroll      (only verified to exist on Topspeed)
4200-7fff unknown/unused?
8000-bfff FG    (FG/BG layer order fixed per game; Topspeed has BG on top)
c000-c1ff FG rowscroll      (only verified to exist on Topspeed)
c200-ffff unknown/unused?

Double width memory layout (two 128x64 tilemaps with 8x8 tiles)

0000-7fff BG
8000-ffff FG
(Tile layout is different; tiles and colors are separated:
0x0000-3fff  color / flip words
0x4000-7fff  tile number words)

Control registers

+0x20000 (on from tilemaps)
000-001 BG scroll Y
002-003 FG scroll Y

+0x40000
000-001 BG scroll X
002-003 FG scroll X

+0x50000 control word (written infrequently, only 2 bits used)
       ---------------x flip screen
       ----------x----- 0x20 poked here in Topspeed init, followed
                        by zero (Darius does the same).
*/

#include "emu.h"
#include "pc080sn.h"
#include "taito_helper.h"
#include "screen.h"

static constexpr u32 PC080SN_RAM_SIZE = 0x10000;
#define TOPSPEED_ROAD_COLORS

DEFINE_DEVICE_TYPE(PC080SN, pc080sn_device, "pc080sn", "Taito PC080SN")

pc080sn_device::pc080sn_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, PC080SN, tag, owner, clock)
	, device_gfx_interface(mconfig, *this)
	, m_ram()
	, m_bg_ram{ nullptr, nullptr }
	, m_bgscroll_ram{ nullptr, nullptr }
	, m_bgscrollx{ 0, 0 }
	, m_bgscrolly{ 0, 0 }
	, m_tilemap{ nullptr, nullptr }
	, m_x_offset(0)
	, m_y_offset(0)
	, m_y_invert(0)
	, m_dblwidth(false)
{
	for (auto & elem : m_ctrl)
		elem = 0;
}

//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void pc080sn_device::device_start()
{
	/* use the given gfx set for bg tiles */
	if (!m_dblwidth) /* standard tilemaps */
	{
		m_tilemap[0] = &machine().tilemap().create(*this, tilemap_get_info_delegate(*this, FUNC(pc080sn_device::get_tile_info<0>)), TILEMAP_SCAN_ROWS, 8, 8, 64, 64);
		m_tilemap[1] = &machine().tilemap().create(*this, tilemap_get_info_delegate(*this, FUNC(pc080sn_device::get_tile_info<1>)), TILEMAP_SCAN_ROWS, 8, 8, 64, 64);
	}
	else    /* double width tilemaps */
	{
		m_tilemap[0] = &machine().tilemap().create(*this, tilemap_get_info_delegate(*this, FUNC(pc080sn_device::get_tile_info<0>)), TILEMAP_SCAN_ROWS, 8, 8, 128, 64);
		m_tilemap[1] = &machine().tilemap().create(*this, tilemap_get_info_delegate(*this, FUNC(pc080sn_device::get_tile_info<1>)), TILEMAP_SCAN_ROWS, 8, 8, 128, 64);
	}

	m_tilemap[0]->set_transparent_pen(0);
	m_tilemap[1]->set_transparent_pen(0);

	m_tilemap[0]->set_scrolldx(-16 + m_x_offset, -16 - m_x_offset);
	m_tilemap[0]->set_scrolldy(m_y_offset, -m_y_offset);
	m_tilemap[1]->set_scrolldx(-16 + m_x_offset, -16 - m_x_offset);
	m_tilemap[1]->set_scrolldy(m_y_offset, -m_y_offset);

	if (!m_dblwidth)
	{
		m_tilemap[0]->set_scroll_rows(512);
		m_tilemap[1]->set_scroll_rows(512);
	}

	m_ram = make_unique_clear<u16[]>(PC080SN_RAM_SIZE / 2);

	m_bg_ram[0]       = m_ram.get() + 0x0000 /2;
	m_bg_ram[1]       = m_ram.get() + 0x8000 /2;
	m_bgscroll_ram[0] = m_ram.get() + 0x4000 /2;
	m_bgscroll_ram[1] = m_ram.get() + 0xc000 /2;

	save_pointer(NAME(m_ram), PC080SN_RAM_SIZE / 2);
	save_item(NAME(m_ctrl));
}

//-------------------------------------------------
//  device_post_load - device-specific postload
//-------------------------------------------------

void pc080sn_device::device_post_load()
{
	restore_scroll();
}

/*****************************************************************************
    DEVICE HANDLERS
*****************************************************************************/

template <unsigned N>
TILE_GET_INFO_MEMBER(pc080sn_device::get_tile_info)
{
	u16 code, attr;

	if (!m_dblwidth)
	{
		code = m_bg_ram[N][2 * tile_index + 1] & 0x3fff;
		attr = m_bg_ram[N][2 * tile_index];
	}
	else
	{
		code = m_bg_ram[N][tile_index + 0x2000] & 0x3fff;
		attr = m_bg_ram[N][tile_index];
	}

	tileinfo.set(0,
			code,
			(attr & 0x1ff),
			TILE_FLIPYX((attr & 0xc000) >> 14));
}

u16 pc080sn_device::word_r(offs_t offset)
{
	return m_ram[offset];
}

void pc080sn_device::word_w(offs_t offset, u16 data, u16 mem_mask)
{
	COMBINE_DATA(&m_ram[offset]);

	if (!m_dblwidth)
	{
		if (offset < 0x2000)
			m_tilemap[0]->mark_tile_dirty(offset / 2);
		else if (offset >= 0x4000 && offset < 0x6000)
			m_tilemap[1]->mark_tile_dirty((offset & 0x1fff) / 2);
	}
	else
	{
		if (offset < 0x4000)
			m_tilemap[0]->mark_tile_dirty((offset & 0x1fff));
		else if (offset >= 0x4000 && offset < 0x8000)
			m_tilemap[1]->mark_tile_dirty((offset & 0x1fff));
	}
}

void pc080sn_device::xscroll_word_w(offs_t offset, u16 data, u16 mem_mask)
{
	COMBINE_DATA(&m_ctrl[offset]);

	data = m_ctrl[offset];

	switch (offset)
	{
		case 0x00:
			m_bgscrollx[0] = -data;
			break;

		case 0x01:
			m_bgscrollx[1] = -data;
			break;
	}
}

void pc080sn_device::yscroll_word_w(offs_t offset, u16 data, u16 mem_mask)
{
	COMBINE_DATA(&m_ctrl[offset + 2]);

	data = m_ctrl[offset + 2];

	if (m_y_invert)
		data = -data;

	switch (offset)
	{
		case 0x00:
			m_bgscrolly[0] = -data;
			break;

		case 0x01:
			m_bgscrolly[1] = -data;
			break;
	}
}

void pc080sn_device::ctrl_word_w(offs_t offset, u16 data, u16 mem_mask)
{
	COMBINE_DATA(&m_ctrl[offset + 4]);

	data = m_ctrl[offset + 4];

	switch (offset)
	{
		case 0x00:
		{
			u32 const flip = (data & 0x01) ? (TILEMAP_FLIPX | TILEMAP_FLIPY) : 0;

			m_tilemap[0]->set_flip(flip);
			m_tilemap[1]->set_flip(flip);
			break;
		}
	}
#if 0
	popmessage("pc080sn ctrl = %4x", data);
#endif
}


/* This routine is needed as an override by Jumping, which
   doesn't set proper scroll values for foreground tilemap */

void pc080sn_device::set_scroll( int tilemap_num, int scrollx, int scrolly )
{
	m_tilemap[tilemap_num]->set_scrollx(0, scrollx);
	m_tilemap[tilemap_num]->set_scrolly(0, scrolly);
}

/* This routine is needed as an override by Jumping */

void pc080sn_device::set_trans_pen( int tilemap_num, int pen )
{
	m_tilemap[tilemap_num]->set_transparent_pen(pen);
}


void pc080sn_device::tilemap_update( )
{
	m_tilemap[0]->set_scrolly(0, m_bgscrolly[0]);
	m_tilemap[1]->set_scrolly(0, m_bgscrolly[1]);

	if (!m_dblwidth)
	{
		for (int j = 0; j < 256; j++)
			m_tilemap[0]->set_scrollx((j + m_bgscrolly[0]) & 0x1ff, m_bgscrollx[0] - m_bgscroll_ram[0][j]);

		for (int j = 0; j < 256; j++)
			m_tilemap[1]->set_scrollx((j + m_bgscrolly[1]) & 0x1ff, m_bgscrollx[1] - m_bgscroll_ram[1][j]);
	}
	else
	{
		m_tilemap[0]->set_scrollx(0, m_bgscrollx[0]);
		m_tilemap[1]->set_scrollx(0, m_bgscrollx[1]);
	}
}


static u16 topspeed_get_road_pixel_color( u16 pixel, u16 color )
{
	/* Color changes based on screenshots from game flyer */
	u16 pixel_type = (pixel % 0x10);
	u16 road_body_color = (pixel & 0x7ff0) + 4;
	u16 off_road_color = road_body_color + 1;

	if ((color & 0xffe0) == 0xffe0)
	{
		pixel += 10;    /* Tunnel colors */
		road_body_color += 10;
		off_road_color  += 10;
	}
	else
	{
		/* Unsure which way round these bits go */
		if (color & 0x10)   road_body_color += 5;
		if (color & 0x02)   off_road_color  += 5;
	}

	switch (pixel_type)
	{
	case 0x01:      /* Center lines */
		if (color & 0x08)
			pixel = road_body_color;
		break;
	case 0x02:      /* Road edge (inner) */
		if (color & 0x08)
			pixel = road_body_color;
		break;
	case 0x03:      /* Road edge (outer) */
		if (color & 0x04)
			pixel = road_body_color;
		break;
	case 0x04:      /* Road body */
		pixel = road_body_color;
		break;
	case 0x05:      /* Off road */
		pixel = off_road_color;
		break;
	default:
		{}
	}
	return pixel;
}


void pc080sn_device::topspeed_custom_draw(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect, int layer, int flags, u8 priority, u16 *color_ctrl_ram, u8 pmask)
{
	u16 scanline[1024];  /* won't be called by a wide-screen game, but just in case... */

	bitmap_ind16 const &srcbitmap = m_tilemap[layer]->pixmap();
	bitmap_ind8 const &flagsbitmap = m_tilemap[layer]->flagsmap();

	int sx;
	int y_index;

	int flip = 0;

	int const min_x = cliprect.min_x;
	int const max_x = cliprect.max_x;
	int const min_y = cliprect.min_y;
	int const max_y = cliprect.max_y;
	int const screen_width = max_x - min_x + 1;
	int const width_mask = 0x1ff; /* underlying tilemap */

	if (!flip)
	{
		sx = m_bgscrollx[layer] + 16 - m_x_offset;
		y_index = m_bgscrolly[layer] + min_y - m_y_offset;
	}
	else    // never used
	{
		sx = 0;
		y_index = 0;
	}

	for (int y = min_y; y <= max_y; y++)
	{
		int const src_y_index = y_index & 0x1ff;  /* tilemaps are 512 px up/down */
		int const row_index = (src_y_index - m_bgscrolly[layer]) & 0x1ff;
		u16 const color = color_ctrl_ram[(row_index + m_y_offset - 2) & 0xff];

		int x_index = sx - (m_bgscroll_ram[layer][row_index]);

		u16 const *const src16 = &srcbitmap.pix(src_y_index);
		u8 const *const tsrc = &flagsbitmap.pix(src_y_index);
		u16 *dst16 = scanline;

		if (flags & TILEMAP_DRAW_OPAQUE)
		{
			for (int i = 0; i < screen_width; i++)
			{
				u16 a = src16[x_index & width_mask];
#ifdef TOPSPEED_ROAD_COLORS
				a = topspeed_get_road_pixel_color(a, color);
#endif
				*dst16++ = a;
				x_index++;
			}
		}
		else
		{
			for (int i = 0; i < screen_width; i++)
			{
				if (tsrc[x_index & width_mask])
				{
					u16 a = src16[x_index & width_mask];
#ifdef TOPSPEED_ROAD_COLORS
					a = topspeed_get_road_pixel_color(a,color);
#endif
					*dst16++ = a;
				}
				else
					*dst16++ = 0x8000;
				x_index++;
			}
		}

		taitoic_drawscanline(bitmap, cliprect, 0, y, scanline, (flags & TILEMAP_DRAW_OPAQUE) ? false : true, ROT0, screen.priority(), priority, pmask);
		y_index++;
	}
}

void pc080sn_device::tilemap_draw(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect, int layer, int flags, u8 priority, u8 pmask)
{
	m_tilemap[layer]->draw(screen, bitmap, cliprect, flags, priority, pmask);
}

void pc080sn_device::tilemap_draw_offset(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect, int layer, int flags, u8 priority, int x_offset, int y_offset, u8 pmask)
{
	int const basedx = -16 - m_x_offset;
	int const basedxflip = -16 + m_x_offset;
	int const basedy = m_y_offset;
	int const basedyflip = -m_y_offset;

	m_tilemap[layer]->set_scrolldx(basedx + x_offset, basedxflip + x_offset);
	m_tilemap[layer]->set_scrolldy(basedy + y_offset, basedyflip + y_offset);
	m_tilemap[layer]->draw(screen, bitmap, cliprect, flags, priority, pmask);
	m_tilemap[layer]->set_scrolldx(basedx, basedxflip);
	m_tilemap[layer]->set_scrolldy(basedy, basedyflip);
}

void pc080sn_device::tilemap_draw_special(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect, int layer, int flags, u8 priority, u16 *ram, u8 pmask)
{
	pc080sn_device::topspeed_custom_draw(screen, bitmap, cliprect, layer, flags, priority, ram, pmask);
}


void pc080sn_device::restore_scroll()
{
	m_bgscrollx[0] = -m_ctrl[0];
	m_bgscrollx[1] = -m_ctrl[1];
	m_bgscrolly[0] = -m_ctrl[2];
	m_bgscrolly[1] = -m_ctrl[3];

	u32 const flip = (m_ctrl[4] & 0x01) ? (TILEMAP_FLIPX | TILEMAP_FLIPY) : 0;
	m_tilemap[0]->set_flip(flip);
	m_tilemap[1]->set_flip(flip);
}
