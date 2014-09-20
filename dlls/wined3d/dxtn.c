/*
 * Copyright 2014 Michael Müller
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wine/port.h"
#include "wined3d_private.h"
#include "wine/library.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d);

#ifdef SONAME_LIBTXC_DXTN

static void* txc_dxtn_handle;
static void (*pfetch_2d_texel_rgba_dxt1)(int srcRowStride, const BYTE *pixData, int i, int j, DWORD *texel);
static void (*ptx_compress_dxtn)(int comps, int width, int height, const BYTE *srcPixData,
                                 GLenum destformat, BYTE *dest, int dstRowStride);

/* pfetch_2d_texel_rgba_dxt1 doesn't correctly handle pitch - this wrapper should fix it */
static inline void dxt1_get_pixel(const BYTE *src, DWORD pitch_in, unsigned int x, unsigned int y, DWORD *color)
{
    const BYTE *src_block = src + (y / 4) * pitch_in + (x / 4) * 8;
    pfetch_2d_texel_rgba_dxt1(0, src_block, x & 3, y & 3, color);
}

static inline BOOL dxt1_to_x8r8g8b8(const BYTE *src, BYTE *dst,
        DWORD pitch_in, DWORD pitch_out, unsigned int w, unsigned int h, BOOL alpha)
{
    unsigned int x, y;
    DWORD color;

    TRACE("Converting %ux%u pixels, pitches %u %u\n", w, h, pitch_in, pitch_out);

    for (y = 0; y < h; ++y)
    {
        DWORD *dst_line = (DWORD *)(dst + y * pitch_out);
        for (x = 0; x < w; ++x)
        {
            dxt1_get_pixel(src, pitch_in, x, y, &color);
            if (alpha)
                dst_line[x] = (color & 0xFF00FF00) | ((color & 0xFF) << 16) | ((color & 0xFF0000) >> 16);
            else
            {
                dst_line[x] = 0xFF000000 | ((color & 0xFF) << 16) |
                              (color & 0xFF00) | ((color & 0xFF0000) >> 16);
            }
        }
    }

    return TRUE;
}

static inline BOOL x8r8g8b8_to_dxt1(const BYTE *src, BYTE *dst,
        DWORD pitch_in, DWORD pitch_out, unsigned int w, unsigned int h, BOOL alpha)
{
    unsigned int x, y;
    DWORD color, *tmp;

    TRACE("Converting %ux%u pixels, pitches %u %u\n", w, h, pitch_in, pitch_out);

    tmp = HeapAlloc(GetProcessHeap(), 0, h * w * sizeof(DWORD));
    if (!tmp)
    {
        ERR("Failed to allocate memory for conversion\n");
        return FALSE;
    }

    for (y = 0; y < h; ++y)
    {
        const DWORD *src_line = (const DWORD *)(src + y * pitch_in);
        DWORD *dst_line = tmp + y * w;
        for (x = 0; x < w; ++x)
        {
            color = src_line[x];
            if (alpha)
                dst_line[x] = (color & 0xFF00FF00) | ((color & 0xFF) << 16) | ((color & 0xFF0000) >> 16);
            else
            {
                dst_line[x] = 0xFF000000 | ((color & 0xFF) << 16) |
                              (color & 0xFF00) | ((color & 0xFF0000) >> 16);
            }
        }
    }

    ptx_compress_dxtn(4, w, h, (BYTE *)tmp, alpha ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT :
        GL_COMPRESSED_RGB_S3TC_DXT1_EXT, dst, pitch_out);

    HeapFree(GetProcessHeap(), 0, tmp);
    return TRUE;
}

static inline BOOL x1r5g5b5_to_dxt1(const BYTE *src, BYTE *dst,
        DWORD pitch_in, DWORD pitch_out, unsigned int w, unsigned int h, BOOL alpha)
{
    static const unsigned char convert_5to8[] =
    {
        0x00, 0x08, 0x10, 0x19, 0x21, 0x29, 0x31, 0x3a,
        0x42, 0x4a, 0x52, 0x5a, 0x63, 0x6b, 0x73, 0x7b,
        0x84, 0x8c, 0x94, 0x9c, 0xa5, 0xad, 0xb5, 0xbd,
        0xc5, 0xce, 0xd6, 0xde, 0xe6, 0xef, 0xf7, 0xff,
    };
    unsigned int x, y;
    DWORD *tmp;
    WORD color;

    TRACE("Converting %ux%u pixels, pitches %u %u.\n", w, h, pitch_in, pitch_out);

    tmp = HeapAlloc(GetProcessHeap(), 0, h * w * sizeof(DWORD));
    if (!tmp)
    {
        ERR("Failed to allocate memory for conversion\n");
        return FALSE;
    }

    for (y = 0; y < h; ++y)
    {
        const WORD *src_line = (const WORD *)(src + y * pitch_in);
        DWORD *dst_line = tmp + y * w;
        for (x = 0; x < w; ++x)
        {
            color = src_line[x];
            if (alpha)
            {
                dst_line[x] = ((color & 0x8000) ? 0xFF000000 : 0) |
                              convert_5to8[(color & 0x001f)] << 16 |
                              convert_5to8[(color & 0x03e0) >> 5] << 8 |
                              convert_5to8[(color & 0x7c00) >> 10];
            }
            else
            {
                dst_line[x] = convert_5to8[(color & 0x001f)] << 16 |
                              convert_5to8[(color & 0x03e0) >> 5] << 8 |
                              convert_5to8[(color & 0x7c00) >> 10];
            }
        }
    }

    ptx_compress_dxtn(4, w, h, (BYTE *)tmp, alpha ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT :
        GL_COMPRESSED_RGB_S3TC_DXT1_EXT, dst, pitch_out);

    HeapFree(GetProcessHeap(), 0, tmp);
    return TRUE;
}

BOOL wined3d_dxt1_decode(const BYTE *src, BYTE *dst, DWORD pitch_in, DWORD pitch_out,
        enum wined3d_format_id format, unsigned int w, unsigned int h)
{
    if (!ptx_compress_dxtn)
    {
        FIXME("Failed to decode DXT1 image, there is a problem with %s.\n", SONAME_LIBTXC_DXTN);
        return FALSE;
    }

    switch (format)
    {
        case WINED3DFMT_B8G8R8A8_UNORM:
            return dxt1_to_x8r8g8b8(src, dst, pitch_in, pitch_out, w, h, TRUE);
        case WINED3DFMT_B8G8R8X8_UNORM:
            return dxt1_to_x8r8g8b8(src, dst, pitch_in, pitch_out, w, h, FALSE);
        default:
            break;
    }

    FIXME("Cannot find a conversion function from format DXT1 to %s.\n", debug_d3dformat(format));
    return FALSE;
}

BOOL wined3d_dxt1_encode(const BYTE *src, BYTE *dst, DWORD pitch_in, DWORD pitch_out,
        enum wined3d_format_id format, unsigned int w, unsigned int h)
{
    if (!pfetch_2d_texel_rgba_dxt1)
    {
        FIXME("Failed to encode DXT1 image, there is a problem with %s.\n", SONAME_LIBTXC_DXTN);
        return FALSE;
    }

    switch (format)
    {
        case WINED3DFMT_B8G8R8A8_UNORM:
            return x8r8g8b8_to_dxt1(src, dst, pitch_in, pitch_out, w, h, TRUE);
        case WINED3DFMT_B8G8R8X8_UNORM:
            return x8r8g8b8_to_dxt1(src, dst, pitch_in, pitch_out, w, h, FALSE);
        case WINED3DFMT_B5G5R5A1_UNORM:
            return x1r5g5b5_to_dxt1(src, dst, pitch_in, pitch_out, w, h, TRUE);
        case WINED3DFMT_B5G5R5X1_UNORM:
            return x1r5g5b5_to_dxt1(src, dst, pitch_in, pitch_out, w, h, FALSE);
        default:
            break;
    }

    FIXME("Cannot find a conversion function from format %s to DXT1.\n", debug_d3dformat(format));
    return FALSE;
}

BOOL wined3d_dxtn_init(void)
{
    txc_dxtn_handle = wine_dlopen(SONAME_LIBTXC_DXTN, RTLD_NOW, NULL, 0);
    if (!txc_dxtn_handle)
    {
        FIXME("Wine cannot find the library %s, DXTn software support unavailable.\n", SONAME_LIBTXC_DXTN);
        return FALSE;
    }

    #define LOAD_FUNCPTR(f) if((p##f = wine_dlsym(txc_dxtn_handle, #f, NULL, 0)) == NULL){WARN("Can't find symbol %s\n", #f);}
    LOAD_FUNCPTR(fetch_2d_texel_rgba_dxt1);
    LOAD_FUNCPTR(tx_compress_dxtn);
    #undef LOAD_FUNCPTR

    return TRUE;
}

void wined3d_dxtn_free(void)
{
    if (txc_dxtn_handle)
        wine_dlclose(txc_dxtn_handle, NULL, 0);
}

#else

BOOL wined3d_dxt1_decode(const BYTE *src, BYTE *dst, DWORD pitch_in, DWORD pitch_out,
        enum wined3d_format_id format, unsigned int w, unsigned int h)
{
    FIXME("Failed to convert DXT1 texture. Wine is compiled without DXT1 support.\n");
    return FALSE;
}


BOOL wined3d_dxt1_encode(const BYTE *src, BYTE *dst, DWORD pitch_in, DWORD pitch_out,
        enum wined3d_format_id format, unsigned int w, unsigned int h)
{
    FIXME("Failed to convert DXT1 texture. Wine is compiled without DXT1 support.\n");
    return FALSE;
}

BOOL wined3d_dxtn_init(void)
{
    return FALSE;
}

void wined3d_dxtn_free(void)
{
    /* nothing to do */
}

#endif