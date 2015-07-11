/*
 * Unit test suite for vcomp fork/join implementation
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

#include "wine/test.h"

static const int is_win64 = (sizeof(void *) > sizeof(int));

static HMODULE hvcomp = 0;
static void  (WINAPIV *p_vcomp_fork)(BOOL ifval, int nargs, void *wrapper, ...);
static int   (CDECL   *pomp_get_max_threads)(void);

#define VCOMP_GET_PROC(func) \
    do \
    { \
        p ## func = (void *)GetProcAddress(hvcomp, #func); \
        if (!p ## func) trace("Failed to get address for %s\n", #func); \
    } \
    while (0)

static BOOL init_vcomp(void)
{
    hvcomp = LoadLibraryA("vcomp.dll");
    if (!hvcomp)
    {
        win_skip("vcomp.dll not installed\n");
        return FALSE;
    }

    VCOMP_GET_PROC(_vcomp_fork);
    VCOMP_GET_PROC(omp_get_max_threads);

    return TRUE;
}

#undef VCOMP_GET_PROC

static void CDECL _test_vcomp_fork_ptr(LONG *a, LONG *b, LONG *c, LONG *d, LONG *e)
{
    InterlockedIncrement(a);
    InterlockedIncrement(b);
    InterlockedIncrement(c);
    InterlockedIncrement(d);
    InterlockedIncrement(e);
}

static void CDECL _test_vcomp_fork_uintptr(UINT_PTR a, UINT_PTR b, UINT_PTR c, UINT_PTR d, UINT_PTR e)
{
    ok(a == 1, "expected a = 1, got %p\n", (void *)a);
    ok(b == MAXUINT_PTR - 2, "expected b = MAXUINT_PTR - 2, got %p\n", (void *)b);
    ok(c == 3, "expected c = 3, got %p\n", (void *)c);
    ok(d == MAXUINT_PTR - 4, "expected d = MAXUINT_PTR - 4, got %p\n", (void *)d);
    ok(e == 5, "expected e = 5, got %p\n", (void *)e);
}

static void CDECL _test_vcomp_fork_float(float a, float b, float c, float d, float e)
{
    ok(1.4999 < a && a < 1.5001, "expected a = 1.5, got %f\n", a);
    ok(2.4999 < b && b < 2.5001, "expected b = 2.5, got %f\n", b);
    ok(3.4999 < c && c < 3.5001, "expected c = 3.5, got %f\n", c);
    ok(4.4999 < d && d < 4.5001, "expected d = 4.5, got %f\n", d);
    ok(5.4999 < e && e < 5.5001, "expected e = 5.5, got %f\n", e);
}

static void test_vcomp_fork(void)
{
    LONG a, b, c, d, e;
    int n = pomp_get_max_threads();

    a = 0; b = 1; c = 2; d = 3; e = 4;
    p_vcomp_fork(FALSE, 5, _test_vcomp_fork_ptr, &a, &b, &c, &d, &e);
    ok(a == 1, "expected a = 1, got %u\n", a);
    ok(b == 2, "expected b = 2, got %u\n", b);
    ok(c == 3, "expected c = 3, got %u\n", c);
    ok(d == 4, "expected d = 4, got %u\n", d);
    ok(e == 5, "expected e = 5, got %u\n", e);

    a = 0; b = 1; c = 2; d = 3; e = 4;
    p_vcomp_fork(TRUE, 5, _test_vcomp_fork_ptr, &a, &b, &c, &d, &e);
    ok(a > 0 && a <= (n + 0), "expected a > 0 && a <= (n + 0), got %u\n", a);
    ok(b > 1 && b <= (n + 1), "expected b > 1 && b <= (n + 1), got %u\n", b);
    ok(c > 2 && c <= (n + 2), "expected c > 2 && c <= (n + 2), got %u\n", c);
    ok(d > 3 && d <= (n + 3), "expected d > 3 && d <= (n + 3), got %u\n", d);
    ok(e > 4 && e <= (n + 4), "expected e > 4 && e <= (n + 4), got %u\n", e);

    p_vcomp_fork(TRUE, 5, _test_vcomp_fork_uintptr, (UINT_PTR)1, (UINT_PTR)(MAXUINT_PTR - 2),
        (UINT_PTR)3, (UINT_PTR)(MAXUINT_PTR - 4), (UINT_PTR)5);

    if (is_win64)
        skip("skipping float test on x86_64\n");
    else
    {
        void (CDECL *func)(BOOL, int, void *, float, float, float, float, float) = (void *)p_vcomp_fork;
        func(TRUE, 5, _test_vcomp_fork_float, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f);
    }
}

START_TEST(vcomp)
{
    if (!init_vcomp())
        return;

    test_vcomp_fork();

    FreeLibrary(hvcomp);
}
