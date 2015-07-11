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
static void  (CDECL   *p_vcomp_barrier)(void);
static void  (CDECL   *p_vcomp_for_dynamic_init)(int flags, int first, int last, int step, int chunksize);
static int   (CDECL   *p_vcomp_for_dynamic_next)(int *begin, int *end);
static void  (CDECL   *p_vcomp_for_static_end)(void);
static void  (CDECL   *p_vcomp_for_static_init)(int first, int last, int step, int chunksize, unsigned int *loops,
                                                int *begin, int *end, int *next, int *lastchunk);
static void  (CDECL   *p_vcomp_for_static_simple_init)(unsigned int first, unsigned int last, int step,
                                                       BOOL forward, unsigned int *begin, unsigned int *end);
static void  (WINAPIV *p_vcomp_fork)(BOOL ifval, int nargs, void *wrapper, ...);
static void  (CDECL   *p_vcomp_sections_init)(int n);
static int   (CDECL   *p_vcomp_sections_next)(void);
static int   (CDECL   *pomp_get_max_threads)(void);
static int   (CDECL   *pomp_get_num_threads)(void);
static int   (CDECL   *pomp_get_thread_num)(void);
static void  (CDECL   *pomp_set_num_threads)(int num_threads);

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

    VCOMP_GET_PROC(_vcomp_barrier);
    VCOMP_GET_PROC(_vcomp_for_dynamic_init);
    VCOMP_GET_PROC(_vcomp_for_dynamic_next);
    VCOMP_GET_PROC(_vcomp_for_static_end);
    VCOMP_GET_PROC(_vcomp_for_static_init);
    VCOMP_GET_PROC(_vcomp_for_static_simple_init);
    VCOMP_GET_PROC(_vcomp_fork);
    VCOMP_GET_PROC(_vcomp_sections_init);
    VCOMP_GET_PROC(_vcomp_sections_next);
    VCOMP_GET_PROC(omp_get_max_threads);
    VCOMP_GET_PROC(omp_get_num_threads);
    VCOMP_GET_PROC(omp_get_thread_num);
    VCOMP_GET_PROC(omp_set_num_threads);

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

static void CDECL _test_vcomp_for_static_simple_init(void)
{
    static const struct
    {
        unsigned int first;
        unsigned int last;
        int step;
        BOOL forward;
    }
    tests[] =
    {
        /* loop forward, step 1 */
        { 0, 0, 1, TRUE },
        { 0, 1, 1, TRUE },
        { 0, 2, 1, TRUE },
        { 0, 3, 1, TRUE },
        { 0, 0x10000000, 1, TRUE },
        { 0, 0x20000000, 1, TRUE },
        { 0, 0x40000000, 1, TRUE },
        { 0, 0x80000000, 1, TRUE },

        /* loop forward, varying step */
        { 0, 100, 1,   TRUE },
        { 0, 100, 5,   TRUE },
        { 0, 100, 10,  TRUE },
        { 0, 100, 25,  TRUE },
        { 0, 100, 50,  TRUE },
        { 0, 100, 75,  TRUE },
        { 0, 100, 100, TRUE },
        { 0, 100, 150, TRUE },

        /* empty loop forward, varying step */
        { 50, 50, 3,   TRUE },
        { 50, 50, 2,   TRUE },
        { 50, 50, 1,   TRUE },
        { 50, 50, 0,   TRUE },
        { 50, 50, -1,  TRUE },
        { 50, 50, -2,  TRUE },
        { 50, 50, -3,  TRUE },

        /* loop backward, step 1 */
        { 0, 0, 1, FALSE },
        { 1, 0, 1, FALSE },
        { 2, 0, 1, FALSE },
        { 3, 0, 1, FALSE },
        { 0x10000000, 0, 1, FALSE },
        { 0x20000000, 0, 1, FALSE },
        { 0x40000000, 0, 1, FALSE },
        { 0x80000000, 0, 1, FALSE },

        /* loop backward, varying step */
        { 100, 0, 1,   FALSE },
        { 100, 0, 5,   FALSE },
        { 100, 0, 10,  FALSE },
        { 100, 0, 25,  FALSE },
        { 100, 0, 50,  FALSE },
        { 100, 0, 75,  FALSE },
        { 100, 0, 100, FALSE },
        { 100, 0, 150, FALSE },

        /* empty loop backward, varying step */
        { 50, 50, 3,   FALSE },
        { 50, 50, 2,   FALSE },
        { 50, 50, 1,   FALSE },
        { 50, 50, 0,   FALSE },
        { 50, 50, -1,  FALSE },
        { 50, 50, -2,  FALSE },
        { 50, 50, -3,  FALSE },

        /* test overflow in forward loop */
        { 0, 0, 1, TRUE },
        { 1, 0, 1, TRUE },
        { 2, 0, 1, TRUE },
        { 3, 0, 1, TRUE },
        { 0x10000000, 0, 1, TRUE },
        { 0x20000000, 0, 1, TRUE },
        { 0x40000000, 0, 1, TRUE },
        { 0x80000000, 0, 1, TRUE },

        { 100, 0, 1,   TRUE },
        { 100, 0, 5,   TRUE },
        { 100, 0, 10,  TRUE },
        { 100, 0, 25,  TRUE },
        { 100, 0, 50,  TRUE },
        { 100, 0, 75,  TRUE },
        { 100, 0, 100, TRUE },
        { 100, 0, 150, TRUE },

        /* test overflow in backward loop */
        { 0, 0, 1, FALSE },
        { 0, 1, 1, FALSE },
        { 0, 2, 1, FALSE },
        { 0, 3, 1, FALSE },
        { 0, 0x10000000, 1, FALSE },
        { 0, 0x20000000, 1, FALSE },
        { 0, 0x40000000, 1, FALSE },
        { 0, 0x80000000, 1, FALSE },

        { 0, 100, 1,   FALSE },
        { 0, 100, 5,   FALSE },
        { 0, 100, 10,  FALSE },
        { 0, 100, 25,  FALSE },
        { 0, 100, 50,  FALSE },
        { 0, 100, 75,  FALSE },
        { 0, 100, 100, FALSE },
        { 0, 100, 150, FALSE },
    };
    unsigned int begin, end, expected_begin, expected_end;
    int thread_num  = pomp_get_thread_num();
    int num_threads = pomp_get_num_threads();
    int i;

    for (i = 0; i < sizeof(tests)/sizeof(tests[0]); i++)
    {
        unsigned int iterations, per_thread, remaining;
        DWORD64 first = tests[i].first;
        DWORD64 last  = tests[i].last;
        int     step  = tests[i].step;

        if (num_threads == 1)
        {
            expected_begin = first;
            expected_end   = last;
            goto done;
        }

        if (tests[i].forward)
        {
            if (tests[i].step <= 0)
            {
                expected_begin = 0;
                expected_end   = -1;
                goto done;
            }

            if (last < first)
                last += 0x100000000;

            iterations = 1 + (last - first) / step;
            per_thread = iterations / num_threads;
            remaining  = iterations - per_thread * num_threads;

            if (thread_num < remaining)
                per_thread++;
            else if (per_thread)
                first += remaining * step;
            else
            {
                expected_begin = first;
                expected_end   = first - step;
                goto done;
            }

            expected_begin = first + per_thread * thread_num * step;
            expected_end   = expected_begin + (per_thread - 1) * step;
        }
        else
        {
            if (tests[i].step <= 0)
            {
                expected_begin = 0;
                expected_end   = 1;
                goto done;
            }

            if (first < last)
                first += 0x100000000;

            iterations = 1 + (first - last) / step;
            per_thread = iterations / num_threads;
            remaining  = iterations - per_thread * num_threads;

            if (thread_num < remaining)
                per_thread++;
            else if (per_thread)
                first -= remaining * step;
            else
            {
                expected_begin = first;
                expected_end   = first + step;
                goto done;
            }

            expected_begin = first - per_thread * thread_num * step;
            expected_end   = expected_begin - (per_thread - 1) * step;
        }

done:
        begin = end = 0xdeadbeef;
        p_vcomp_for_static_simple_init(tests[i].first, tests[i].last, tests[i].step,
                                       tests[i].forward, &begin, &end);

        ok(begin == expected_begin, "%d:%d/%d: expected begin = %u, got %u\n",
           i, thread_num, num_threads, expected_begin, end);
        ok(end == expected_end, "%d:%d/%d: expected end = %u, got %u\n",
           i, thread_num, num_threads, expected_end, end);

        p_vcomp_for_static_end();
        p_vcomp_barrier();
    }
}

static void test_vcomp_for_static_simple_init(void)
{
    int i;

    for (i = 1; i <= 4; i++)
    {
        trace("Running tests with %d threads\n", i);
        pomp_set_num_threads(i);
        p_vcomp_fork(TRUE, 0, _test_vcomp_for_static_simple_init);
    }
}

static void CDECL _test_vcomp_for_static_init(void)
{
    static const struct
    {
        int first;
        int last;
        int step;
        int chunksize;
    }
    tests[] =
    {
        /* loop forward, step 1 */
        { 0, 0, 1, 1 },
        { 0, 1, 1, 1 },
        { 0, 2, 1, 1 },
        { 0, 3, 1, 1 },
        { 0, 0x10000000, 1, 1 },
        { 0, 0x20000000, 1, 1 },
        { 0, 0x40000000, 1, 1 },
        { 0, (int)0x80000000, 1, 1 },

        /* loop forward, varying step */
        {0, 100, 1,   1},
        {0, 100, 5,   1},
        {0, 100, 10,  1},
        {0, 100, 25,  1},
        {0, 100, 50,  1},
        {0, 100, 75,  1},
        {0, 100, 100, 1},
        {0, 100, 150, 1},

        /* empty loop forward, varying step */
        { 50, 50, 3,   1 },
        { 50, 50, 2,   1 },
        { 50, 50, 1,   1 },
        /*{ 50, 50, 0,   1 }, - generates a division by zero exception */
        { 50, 50, -1,  1 },
        { 50, 50, -2,  1 },
        { 50, 50, -3,  1 },

        /* loop backwards, step 1 */
        { 0, 0, 1, 1 },
        { 1, 0, 1, 1 },
        { 2, 0, 1, 1 },
        { 3, 0, 1, 1 },
        { 0x10000000, 0, 1, 1 },
        { 0x20000000, 0, 1, 1 },
        { 0x40000000, 0, 1, 1 },
        { (int)0x80000000, 0, 1, 1 },

        /* loop backwards, varying step */
        {100, 0, 1,   1},
        {100, 0, 5,   1},
        {100, 0, 10,  1},
        {100, 0, 25,  1},
        {100, 0, 50,  1},
        {100, 0, 75,  1},
        {100, 0, 100, 1},
        {100, 0, 150, 1},

        /* loop forward, varying chunksize */
        {0, 100, 1, 1},
        {0, 100, 1, 5},
        {0, 100, 1, 10},
        {0, 100, 1, 25},
        {0, 100, 1, 50},
        {0, 100, 1, 75},
        {0, 100, 1, 100},
        {0, 100, 1, 150},

        {0, 100, 7, 1},
        {0, 100, 7, 5},
        {0, 100, 7, 10},
        {0, 100, 7, 25},
        {0, 100, 7, 50},
        {0, 100, 7, 75},
        {0, 100, 7, 100},
        {0, 100, 7, 150},

        /* loop backwards, varying chunksize */
        {100, 0, 1, 1},
        {100, 0, 1, 5},
        {100, 0, 1, 10},
        {100, 0, 1, 25},
        {100, 0, 1, 50},
        {100, 0, 1, 75},
        {100, 0, 1, 100},
        {100, 0, 1, 150},

        {100, 0, 7, 1},
        {100, 0, 7, 5},
        {100, 0, 7, 10},
        {100, 0, 7, 25},
        {100, 0, 7, 50},
        {100, 0, 7, 75},
        {100, 0, 7, 100},
        {100, 0, 7, 150},

    };
    int begin, end, expected_begin, expected_end;
    unsigned int loops, expected_loops;
    int lastchunk, expected_lastchunk;
    int chunksize, expected_chunksize;
    int thread_num  = pomp_get_thread_num();
    int num_threads = pomp_get_num_threads();
    int i;

    for (i = 0; i < sizeof(tests)/sizeof(tests[0]); i++)
    {
        unsigned int iterations, num_chunks, per_thread, remaining;
        int first = tests[i].first;
        int last  = tests[i].last;
        int step  = tests[i].step;
        chunksize = tests[i].chunksize;

        if (chunksize < 1)
            chunksize = 1;

        if (num_threads == 1 && chunksize > 1)
        {
            expected_loops = 1;
            expected_begin = first;
            expected_end   = last;
            expected_chunksize = chunksize;
            expected_lastchunk = first;
        }
        else if (last > first)
        {
            iterations = 1 + (last - first) / step;
            num_chunks = (iterations + chunksize - 1) / chunksize;
            per_thread = num_chunks / num_threads;
            remaining  = num_chunks - per_thread * num_threads;

            expected_loops = per_thread + (thread_num < remaining);
            expected_begin = first + thread_num * chunksize * step;
            expected_end   = expected_begin + (chunksize - 1) * step;
            expected_chunksize = chunksize * num_threads * step;
            expected_lastchunk = first + (num_chunks - 1) * chunksize * step;

        }
        else if (last < first)
        {
            iterations = 1 + (first - last) / step;
            num_chunks = (iterations + chunksize - 1) / chunksize;
            per_thread = num_chunks / num_threads;
            remaining  = num_chunks - per_thread * num_threads;

            expected_loops = per_thread + (thread_num < remaining);
            expected_begin = first - thread_num * chunksize * step;
            expected_end   = expected_begin - (chunksize - 1) * step;
            expected_chunksize = - chunksize * num_threads * step;
            expected_lastchunk = first - (num_chunks - 1) * chunksize * step;
        }
        else
        {
            expected_loops = (thread_num == 0);
            expected_begin = first;
            expected_end   = last;
            expected_chunksize = 0;
            expected_lastchunk = first;
        }

        loops = begin = end = chunksize = lastchunk = 0xdeadbeef;
        p_vcomp_for_static_init(tests[i].first, tests[i].last, tests[i].step, tests[i].chunksize,
                                &loops, &begin, &end, &chunksize, &lastchunk);

        ok(loops == expected_loops, "%d:%d/%d: expected loops = %u, got %u\n",
           i, thread_num, num_threads, expected_loops, loops);
        ok(begin == expected_begin || broken(begin == 0xdeadbeef && first == last && thread_num > 0),
           "%d:%d/%d: expected begin = %u, got %u\n", i, thread_num, num_threads, expected_begin, end);
        ok(end == expected_end || broken(end == 0xdeadbeef && first == last && thread_num > 0),
           "%d:%d/%d: expected end = %u, got %u\n", i, thread_num, num_threads, expected_end, end);
        ok(chunksize == expected_chunksize || broken(chunksize == (int)0xdeadbeef && first == last) ||
           broken(chunksize != expected_chunksize && num_threads == 1),
           "%d:%d/%d: expected chunksize = %u, got %u\n", i, thread_num, num_threads, expected_chunksize, chunksize);
        ok(lastchunk == expected_lastchunk || broken(lastchunk == 0xdeadbeef && first == last && thread_num > 0),
           "%d:%d/%d: expected lastchunk = %u, got %u\n", i, thread_num, num_threads, expected_lastchunk, lastchunk);

        p_vcomp_for_static_end();
        p_vcomp_barrier();
    }
}

static void test_vcomp_for_static_init(void)
{
    int i;

    for (i = 1; i <= 4; i++)
    {
        trace("Running tests with %d threads\n", i);
        pomp_set_num_threads(i);
        p_vcomp_fork(TRUE, 0, _test_vcomp_for_static_init);
    }
}

static void CDECL _test_vcomp_sections_init(LONG *a, LONG *b, LONG *c)
{
    int i;

    p_vcomp_sections_init(20);
    while ((i = p_vcomp_sections_next()) != -1)
    {
        InterlockedIncrement(a);
        Sleep(50);
    }

    p_vcomp_sections_init(30);
    while ((i = p_vcomp_sections_next()) != -1)
    {
        InterlockedIncrement(b);
        Sleep(50);
    }

    p_vcomp_sections_init(40);
    while ((i = p_vcomp_sections_next()) != -1)
    {
        InterlockedIncrement(c);
        Sleep(50);
    }
}

static void test_vcomp_sections_init(void)
{
    LONG a, b, c;
    int i;

    for (i = 1; i <= 4; i++)
    {
        trace("Running tests with %d threads\n", i);
        pomp_set_num_threads(i);

        a = b = c = 0;
        p_vcomp_fork(TRUE, 3, _test_vcomp_sections_init, &a, &b, &c);
        ok(a == 20, "expected a = 20, got %d\n", a);
        ok(b == 30, "expected b = 30, got %d\n", b);
        ok(c == 40, "expected c = 40, got %d\n", c);
    }
}

static void CDECL _test_vcomp_for_dynamic_init(LONG *a, LONG *b, LONG *c)
{
    int begin, end;

    p_vcomp_for_dynamic_init(0x40, 1, 100000, 1, 30);
    while (p_vcomp_for_dynamic_next(&begin, &end))
    {
        InterlockedExchangeAdd(a, end - begin + 1);
        Sleep(50);
    }

    p_vcomp_for_dynamic_init(0, 1337, 1, 1, 50);
    while (p_vcomp_for_dynamic_next(&begin, &end))
    {
        InterlockedExchangeAdd(b, begin - end + 1);
        Sleep(50);
    }

    p_vcomp_for_dynamic_init(0x40, 1, 100000, 7, 30);
    while (p_vcomp_for_dynamic_next(&begin, &end))
    {
        while (begin <= end)
        {
            InterlockedIncrement(c);
            begin += 7;
        }
        Sleep(50);
    }
}

static void test_vcomp_for_dynamic_init(void)
{
    LONG a, b, c;
    int i;

    for (i = 1; i <= 4; i++)
    {
        trace("Running tests with %d threads\n", i);
        pomp_set_num_threads(i);

        a = b = c = 0;
        p_vcomp_fork(TRUE, 3, _test_vcomp_for_dynamic_init, &a, &b, &c);
        ok(a == 100000, "expected a = 100000, got %d\n", a);
        ok(b == 1337, "expected b = 1337, got %d\n", b);
        ok(c == 14286, "expected c = 14286, got %d\n", c);
    }
}

START_TEST(vcomp)
{
    if (!init_vcomp())
        return;

    test_vcomp_fork();
    test_vcomp_for_static_simple_init();
    test_vcomp_for_static_init();
    test_vcomp_sections_init();
    test_vcomp_for_dynamic_init();

    FreeLibrary(hvcomp);
}
