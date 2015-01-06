/*
 * Copyright (C) 2015 Michael Müller
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

#ifndef __WINE_CUVIDDEC_H
#define __WINE_CUVIDDEC_H

#include "cuda.h"

typedef void *CUvideodecoder;
typedef void *CUvideoctxlock;

/* the following structures are documented but we don't need to know the content */
typedef struct _CUVIDDECODECREATEINFO CUVIDDECODECREATEINFO;
typedef struct _CUVIDPICPARAMS CUVIDPICPARAMS;
typedef struct _CUVIDPROCPARAMS CUVIDPROCPARAMS;

#endif /* __WINE_CUVIDDEC_H */
