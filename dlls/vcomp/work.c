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
#include "vcomp_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(vcomp);

void CDECL _vcomp_for_dynamic_init(int flags, int first, int last, int mystep, int chunksize)
{
    struct vcomp_team *pt = vcomp_get_team();

    TRACE("(%d, %d, %d, %d, %d): stub\n", flags, first, last, mystep, chunksize);

    pt->work.dyn_for.counter = first;
    pt->work.dyn_for.chunksize = chunksize;
    pt->work.dyn_for.flags = flags;
    pt->work.dyn_for.step = mystep;
    if (flags & VCOMP_DYNAMIC_FOR_FLAGS_UP)
        pt->work.dyn_for.iterations_remaining = 1 + (last - first) / mystep;
    else
        pt->work.dyn_for.iterations_remaining = 1 + (first - last) / mystep;
}

int CDECL _vcomp_for_dynamic_next(int *pcounter, int *pchunklimit)
{
    struct vcomp_team *pt = vcomp_get_team();
    int n;

    TRACE("(%p, %p): stub.\n", pcounter, pchunklimit);

    n = pt->work.dyn_for.chunksize;
    if (n > pt->work.dyn_for.iterations_remaining)
        n = pt->work.dyn_for.iterations_remaining;

    *pcounter = pt->work.dyn_for.counter;

    if (pt->work.dyn_for.flags & VCOMP_DYNAMIC_FOR_FLAGS_UP)
    {
        pt->work.dyn_for.counter += pt->work.dyn_for.step * n;
        *pchunklimit = pt->work.dyn_for.counter - 1;
    }
    else
    {
        pt->work.dyn_for.counter -= pt->work.dyn_for.step * n;
        *pchunklimit = pt->work.dyn_for.counter + 1;
    }
    pt->work.dyn_for.iterations_remaining -= n;

    TRACE("counter %d, iterations_remaining %d, n %d, returning %d\n",
          pt->work.dyn_for.counter, pt->work.dyn_for.iterations_remaining, n, (n > 0));
    return (n > 0);
}

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
