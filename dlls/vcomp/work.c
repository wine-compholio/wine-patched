/*
 * vcomp work-sharing implementation
 *
 * Copyright 2012 Dan Kegel
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

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(vcomp);

void CDECL _vcomp_for_static_init(int first, int last, int mystep, int chunksize, int *pnloops, int *pfirst, int *plast, int *pchunksize, int *pfinalchunkstart)
{
    TRACE("(%d, %d, %d, %d, %p, %p, %p, %p, %p): stub\n",
          first, last, mystep, chunksize, pnloops, pfirst, plast, pchunksize, pfinalchunkstart);
    *pfirst = first;
    *plast = last;
    *pfinalchunkstart = last;
    *pnloops = 1;
    *pchunksize = 0;  /* moot, since nloops=1 */
}

void CDECL _vcomp_for_static_simple_init(int first, int last, int mystep, int step, int *pfirst, int *plast)
{
    TRACE("(%d, %d, %d, %d, %p, %p): stub\n", first, last, mystep, step, pfirst, plast);
    *pfirst = first;
    *plast = last;
}

void CDECL _vcomp_for_static_end(void)
{
    TRACE("stub\n");
}
