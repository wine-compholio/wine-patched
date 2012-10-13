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

static void WINAPIV (*p_vcomp_fork)(BOOL ifval, int nargs, void *wrapper, ...);
static int CDECL (*pomp_get_max_threads)(void);

#define GETFUNC(x) do { p##x = (void*)GetProcAddress(vcomp, #x); ok(p##x != NULL, "Export '%s' not found\n", #x); } while(0)

static BOOL init(void)
{
    HMODULE vcomp = LoadLibraryA("vcomp.dll");
    if (!vcomp)
    {
        win_skip("vcomp.dll not installed\n");
        return FALSE;
    }

    GETFUNC(_vcomp_fork);
    GETFUNC(omp_get_max_threads);

    return TRUE;
}

/* Test whether a variety of types are passed correctly.
 * Pass five of each because the first four parameters are
 * handled differently on amd64, and we want to test both
 * ways.
 */

static void CDECL _test_vcomp_fork_ptr_worker(LONG volatile *a, LONG volatile *b, LONG volatile *c, LONG volatile *d, LONG volatile *e)
{
    InterlockedIncrement(a);
    InterlockedIncrement(b);
    InterlockedIncrement(c);
    InterlockedIncrement(d);
    InterlockedIncrement(e);
}

static void test_vcomp_fork_ptr(void)
{
    LONG volatile a, b, c, d, e;
    int n;

    /* #pragma omp parallel if(FALSE) shared(a, b, c, d, e)
     * { InterlockedIncrement(&a); ... InterlockedIncrement(&e); }
     */
    a=0; b=1; c=2; d=3; e=4;
    p_vcomp_fork(FALSE, 5, _test_vcomp_fork_ptr_worker, &a, &b, &c, &d, &e);
    ok(a == 1, "a == 1\n");
    ok(b == 2, "a == 2\n");
    ok(c == 3, "a == 3\n");
    ok(d == 4, "a == 4\n");
    ok(e == 5, "a == 5\n");

    /* #pragma omp parallel if(TRUE) shared(a, b, c, d, e)
     * { InterlockedIncrement(&a); ... InterlockedIncrement(&e); }
     */
    a=0; b=1; c=2; d=3; e=4;
    n = pomp_get_max_threads();
    p_vcomp_fork(TRUE, 5, _test_vcomp_fork_ptr_worker, &a, &b, &c, &d, &e);
    ok(a > 0 && a <= (n+0), "a > 0 && a <= (n+0)\n");
    ok(b > 1 && b <= (n+1), "b > 1 && b <= (n+1)\n");
    ok(c > 2 && c <= (n+2), "c > 2 && c <= (n+2)\n");
    ok(d > 3 && d <= (n+3), "d > 3 && d <= (n+3)\n");
    ok(e > 4 && e <= (n+4), "e > 4 && e <= (n+4)\n");
}

static void CDECL _test_vcomp_fork_uintptr_worker(UINT_PTR a, UINT_PTR b, UINT_PTR c, UINT_PTR d, UINT_PTR e)
{
    ok(a == 1, "expected a == 1\n");
    ok(b == MAXUINT_PTR-2, "expected b == MAXUINT_PTR-2\n");
    ok(c == 3, "expected c == 3\n");
    ok(d == MAXUINT_PTR-4, "expected d == MAXUINT_PTR-4\n");
    ok(e == 5, "expected e == 5\n");
}

static void test_vcomp_fork_uintptr(void)
{
    /* test_vcomp_fork_ptr ought to have been enough, but probably
     * didn't vary all the bits of the high word, so do that here.
     */
    p_vcomp_fork(TRUE, 5, _test_vcomp_fork_uintptr_worker, \
        (UINT_PTR)1, (UINT_PTR)(MAXUINT_PTR-2), \
        (UINT_PTR)3, (UINT_PTR)(MAXUINT_PTR)-4, (UINT_PTR) 5);
}

static void CDECL _test_vcomp_fork_float_worker(float a, float b, float c, float d, float e)
{
    ok(1.4999 < a && a < 1.5001, "expected a == 1.5, got %f\n", a);
    ok(2.4999 < b && b < 2.5001, "expected b == 2.5, got %f\n", b);
    ok(3.4999 < c && c < 3.5001, "expected c == 3.5, got %f\n", c);
    ok(4.4999 < d && d < 4.5001, "expected d == 4.5, got %f\n", d);
    ok(5.4999 < e && e < 5.5001, "expected e == 5.5, got %f\n", e);
}

static void test_vcomp_fork_float(void)
{
    static void CDECL (*p_vcomp_fork_f5)(BOOL, int, void *, float, float, float, float, float);

    if (is_win64)
    {
        skip("Skipping float test on x86_64.\n");
        return;
    }

    /*
     * 32 bit Visual C sometimes passes 32 bit floats by value to
     * the wrapper, so verify that here.
     *
     * x86-64 Visual C has not yet been observed passing 32 bit floats by
     * value to the wrapper, and indeed _vcomp_fork does not even copy the
     * first four args to floating point registers, so this test fails
     * on x86-64 for the first four arguments even on native.
     * Therefore don't run it.  (It's hard to write a reliable test to show
     * this, since the floating point registers might just happen
     * to have the right values once in a blue moon.)
     */

    /* Avoid float promotion by using a prototype tailored for this call */
    p_vcomp_fork_f5 = (void *)p_vcomp_fork;
    p_vcomp_fork_f5(TRUE, 5, _test_vcomp_fork_float_worker, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f);
}

START_TEST(fork)
{
    if (!init())
        return;

    test_vcomp_fork_ptr();
    test_vcomp_fork_uintptr();
    test_vcomp_fork_float();
}
