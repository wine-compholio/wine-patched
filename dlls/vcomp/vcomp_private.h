/*
 * vcmp wine internal private include file
 *
 * Copyright 2012 Dan Kegel
 * Copyright 2015 Sebastian Lackner
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

#ifndef __WINE_VCOMP_PRIVATE_H
#define __WINE_VCOMP_PRIVATE_H

struct vcomp_team
{
    struct vcomp_team *parent;
    union
    {
        struct
        {
            int counter;
            int step;
            int iterations_remaining;
            int chunksize;
            int flags;
        } dyn_for;
    } work;
};

extern DWORD vcomp_context_tls DECLSPEC_HIDDEN;

static inline struct vcomp_team *vcomp_get_team(void)
{
    return (struct vcomp_team *)TlsGetValue(vcomp_context_tls);
}

static inline void vcomp_set_team(struct vcomp_team *team)
{
    TlsSetValue(vcomp_context_tls, team);
}

#define VCOMP_DYNAMIC_FOR_FLAGS_DOWN 0x0
#define VCOMP_DYNAMIC_FOR_FLAGS_UP 0x40

#endif
