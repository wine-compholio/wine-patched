/*
 *
 * vcomp implementation
 *
 * Copyright 2011 Austin English
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

#include "config.h"

#include <stdarg.h>
#include <assert.h>

#include "windef.h"
#include "winbase.h"
#include "wine/debug.h"
#include "wine/list.h"

WINE_DEFAULT_DEBUG_CHANNEL(vcomp);

static HMODULE vcomp_module;
static struct list vcomp_idle_threads = LIST_INIT(vcomp_idle_threads);
static DWORD vcomp_context_tls = TLS_OUT_OF_INDEXES;
static DWORD vcomp_max_threads = 32;
static DWORD vcomp_num_threads = 1;

static RTL_CRITICAL_SECTION vcomp_section;
static RTL_CRITICAL_SECTION_DEBUG critsect_debug =
{
    0, 0, &vcomp_section,
    { &critsect_debug.ProcessLocksList, &critsect_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": vcomp_section") }
};
static RTL_CRITICAL_SECTION vcomp_section = { &critsect_debug, -1, 0, 0, 0, 0 };

struct vcomp_thread_info
{
    struct list             entry;
    CONDITION_VARIABLE      cond;

    /* current task */
    struct vcomp_team_info  *team;
    DWORD                   thread_num;

    /* section */
    DWORD                   section;
};

struct vcomp_team_info
{
    CONDITION_VARIABLE  cond;
    DWORD               num_threads;
    DWORD               finished_threads;

    /* callback arguments */
    unsigned int        nargs;
    void                *wrapper;
    __ms_va_list        valist;

    /* barrier */
    DWORD               barrier;
    DWORD               barrier_count;

    /* section */
    DWORD               section;
    DWORD               num_sections;
    DWORD               section_index;
};

static inline struct vcomp_thread_info *vcomp_get_thread_info(void)
{
    return (struct vcomp_thread_info *)TlsGetValue(vcomp_context_tls);
}

static inline struct vcomp_team_info *vcomp_get_team_info(void)
{
    struct vcomp_thread_info *thread_info = vcomp_get_thread_info();
    return thread_info ? thread_info->team : NULL;
}

static inline void vcomp_set_thread_info(struct vcomp_thread_info *thread_info)
{
    TlsSetValue(vcomp_context_tls, thread_info);
}

int CDECL omp_get_dynamic(void)
{
    TRACE("stub\n");
    return 0;
}

int CDECL omp_get_max_threads(void)
{
    TRACE("stub\n");
    return vcomp_max_threads;
}

int CDECL omp_get_nested(void)
{
    TRACE("stub\n");
    return 0;
}

int CDECL omp_get_num_procs(void)
{
    TRACE("stub\n");
    return 1;
}

int CDECL omp_get_num_threads(void)
{
    struct vcomp_team_info *team_info;

    TRACE("()\n");

    team_info = vcomp_get_team_info();
    return team_info ? team_info->num_threads : 1;
}

int CDECL omp_get_thread_num(void)
{
    struct vcomp_thread_info *thread_info;

    TRACE("()\n");

    thread_info = vcomp_get_thread_info();
    return thread_info ? thread_info->thread_num : 0;
}

/* Time in seconds since "some time in the past" */
double CDECL omp_get_wtime(void)
{
    return GetTickCount() / 1000.0;
}

void CDECL omp_set_dynamic(int val)
{
    TRACE("(%d): stub\n", val);
}

void CDECL omp_set_nested(int nested)
{
    TRACE("(%d): stub\n", nested);
}

void CDECL omp_set_num_threads(int num_threads)
{
    TRACE("(%d): stub\n", num_threads);
    vcomp_num_threads = max(1, min(num_threads, vcomp_max_threads));
}

void CDECL _vcomp_barrier(void)
{
    struct vcomp_team_info *team_info;

    TRACE("()\n");

    team_info = vcomp_get_team_info();
    EnterCriticalSection(&vcomp_section);

    team_info->barrier_count++;
    if (team_info->barrier_count >= team_info->num_threads)
    {
        team_info->barrier++;
        team_info->barrier_count = 0;
        WakeAllConditionVariable(&team_info->cond);
    }
    else
    {
        DWORD barrier = team_info->barrier;
        while (team_info->barrier == barrier)
            SleepConditionVariableCS(&team_info->cond, &vcomp_section, INFINITE);
    }

    LeaveCriticalSection(&vcomp_section);
}

void CDECL _vcomp_set_num_threads(int num_threads)
{
    TRACE("(%d)\n", num_threads);
    vcomp_num_threads = max(1, min(num_threads, vcomp_max_threads));
}

int CDECL _vcomp_single_begin(int flags)
{
    TRACE("(%x): stub\n", flags);
    return TRUE;
}

void CDECL _vcomp_single_end(void)
{
    TRACE("stub\n");
}

void CDECL _vcomp_for_static_simple_init(unsigned int first, unsigned int last, int step, BOOL forward,
                                         unsigned int *begin, unsigned int *end)
{
    struct vcomp_thread_info *thread_info = vcomp_get_thread_info();
    struct vcomp_team_info *team_info = thread_info->team;
    unsigned int iterations, per_thread, remaining;
    DWORD num_threads, thread_num;

    TRACE("(%d, %d, %d, %d, %p, %p)\n", first, last, step, forward, begin, end);

    num_threads = team_info->num_threads;
    thread_num  = thread_info->thread_num;

    if (num_threads == 1)
    {
        *begin = first;
        *end   = last;
        return;
    }

    if (step <= 0)
    {
        *begin = 0;
        *end   = forward ? -1 : 1;
        return;
    }

    if (forward)
    {
        DWORD64 last64 = last;
        if (last64 < first)
            last64 += 0x100000000;

        iterations = 1 + (last64 - first) / step;
        per_thread = iterations / num_threads;
        remaining  = iterations - per_thread * num_threads;

        if (thread_num < remaining)
        {
            per_thread++;
        }
        else if (per_thread)
        {
            first += remaining * step;
        }
        else
        {
            *begin = first;
            *end   = first - step;
            return;
        }

        *begin = first + per_thread * thread_num * step;
        *end   = *begin + (per_thread - 1) * step;
    }
    else
    {
        DWORD first64 = first;
        if (first64 < last)
            first64 += 0x100000000;

        iterations = 1 + (first64 - last) / step;
        per_thread = iterations / num_threads;
        remaining  = iterations - per_thread * num_threads;

        if (thread_num < remaining)
        {
            per_thread++;
        }
        else if (per_thread)
        {
            first64 -= remaining * step;
        }
        else
        {
            *begin = first64;
            *end   = first64 + step;
            return;
        }

        *begin = first64 - per_thread * thread_num * step;
        *end   = *begin - (per_thread - 1) * step;
    }
}

void CDECL _vcomp_for_static_init(int first, int last, int step, int chunksize, unsigned int *loops,
                                  int *begin, int *end, int *next, int *lastchunk)
{
    struct vcomp_thread_info *thread_info = vcomp_get_thread_info();
    struct vcomp_team_info *team_info = thread_info->team;
    unsigned int iterations, num_chunks, per_thread, remaining;
    DWORD num_threads, thread_num;

    TRACE("(%d, %d, %d, %d, %p, %p, %p, %p, %p)\n",
          first, last, step, chunksize, loops, begin, end, next, lastchunk);

    num_threads = team_info->num_threads;
    thread_num  = thread_info->thread_num;

    if (chunksize < 1)
        chunksize = 1;

    if (num_threads == 1 && chunksize > 1)
    {
        *loops = 1;
        *begin = first;
        *end   = last;
        *next = chunksize;
        *lastchunk = first;
    }
    else if (last > first)
    {
        iterations = 1 + (last - first) / step;
        num_chunks = (iterations + chunksize - 1) / chunksize;
        per_thread = num_chunks / num_threads;
        remaining  = num_chunks - per_thread * num_threads;

        *loops = per_thread + (thread_num < remaining);
        *begin = first + thread_num * chunksize * step;
        *end   = *begin + (chunksize - 1) * step;
        *next = chunksize * num_threads * step;
        *lastchunk = first + (num_chunks - 1) * chunksize * step;

    }
    else if (last < first)
    {
        iterations = 1 + (first - last) / step;
        num_chunks = (iterations + chunksize - 1) / chunksize;
        per_thread = num_chunks / num_threads;
        remaining  = num_chunks - per_thread * num_threads;

        *loops = per_thread + (thread_num < remaining);
        *begin = first - thread_num * chunksize * step;
        *end   = *begin - (chunksize - 1) * step;
        *next = - chunksize * num_threads * step;
        *lastchunk = first - (num_chunks - 1) * chunksize * step;
    }
    else
    {
        *loops = (thread_num == 0);
        *begin = first;
        *end   = last;
        *next = 0;
        *lastchunk = first;
    }
}

void CDECL _vcomp_for_static_end(void)
{
    TRACE("()\n");
}

int CDECL omp_in_parallel(void)
{
    TRACE("()\n");
    return vcomp_get_team_info() != NULL;
}

void CDECL _vcomp_sections_init(int n)
{
    struct vcomp_thread_info *thread_info = vcomp_get_thread_info();
    struct vcomp_team_info *team_info = thread_info->team;

    TRACE("(%d)\n", n);

    EnterCriticalSection(&vcomp_section);
    thread_info->section++;
    if ((int)(thread_info->section - team_info->section) > 0)
    {
        /* first thread in a new section */
        team_info->section = thread_info->section;
        team_info->num_sections  = n;
        team_info->section_index = 0;
    }
    LeaveCriticalSection(&vcomp_section);
}

int CDECL _vcomp_sections_next(void)
{
    struct vcomp_thread_info *thread_info = vcomp_get_thread_info();
    struct vcomp_team_info *team_info = thread_info->team;
    int i = -1;

    TRACE("()\n");

    EnterCriticalSection(&vcomp_section);
    if (thread_info->section == team_info->section &&
        team_info->section_index < team_info->num_sections)
    {
        i = team_info->section_index++;
    }
    LeaveCriticalSection(&vcomp_section);
    return i;
}

void CDECL _vcomp_fork_call_wrapper(void *wrapper, int nargs, __ms_va_list args);

static DWORD WINAPI _vcomp_fork_worker(void *param)
{
    struct vcomp_thread_info *thread_info = param;
    vcomp_set_thread_info(thread_info);

    TRACE("starting worker thread %p\n", thread_info);

    EnterCriticalSection(&vcomp_section);
    for (;;)
    {
        struct vcomp_team_info *team = thread_info->team;
        if (team != NULL)
        {
            /* Leave critical section and execute callback. */
            LeaveCriticalSection(&vcomp_section);
            _vcomp_fork_call_wrapper(team->wrapper, team->nargs, team->valist);
            EnterCriticalSection(&vcomp_section);

            /* Detach current thread from team. */
            thread_info->team = NULL;
            list_remove(&thread_info->entry);
            list_add_tail(&vcomp_idle_threads, &thread_info->entry);
            if (++team->finished_threads >= team->num_threads)
                WakeAllConditionVariable(&team->cond);
        }

        if (!SleepConditionVariableCS(&thread_info->cond, &vcomp_section, 5000) &&
            GetLastError() == ERROR_TIMEOUT && !thread_info->team)
        {
            break;
        }
    }
    list_remove(&thread_info->entry);
    LeaveCriticalSection(&vcomp_section);

    TRACE("terminating worker thread %p\n", thread_info);
    HeapFree(GetProcessHeap(), 0, thread_info);
    FreeLibraryAndExitThread(vcomp_module, 0);
    return 0;
}

void WINAPIV _vcomp_fork(BOOL ifval, int nargs, void *wrapper, ...)
{
    struct vcomp_thread_info thread_info, *prev_thread_info;
    struct vcomp_team_info team_info;
    DWORD num_threads = vcomp_num_threads; /* FIXME */
    BOOL parallel = ifval;

    TRACE("(%d, %d, %p, ...)\n", ifval, nargs, wrapper);

    /* Initialize members of team_info. */
    InitializeConditionVariable(&team_info.cond);
    team_info.num_threads       = 1;
    team_info.finished_threads  = 0;
    team_info.nargs             = nargs;
    team_info.wrapper           = wrapper;
    __ms_va_start(team_info.valist, wrapper);
    team_info.barrier           = 0;
    team_info.barrier_count     = 0;
    team_info.section           = -1;

    /* Initialize members of thread_info. */
    list_init(&thread_info.entry);
    InitializeConditionVariable(&thread_info.cond);
    thread_info.team        = &team_info;
    thread_info.thread_num  = 0;
    thread_info.section     = 0;

    if (parallel)
    {
        struct list *ptr;
        EnterCriticalSection(&vcomp_section);

        /* Try to reuse idle threads. */
        while (team_info.num_threads < num_threads &&
               (ptr = list_head( &vcomp_idle_threads )))
        {
            struct vcomp_thread_info *info = LIST_ENTRY(ptr, struct vcomp_thread_info, entry);
            list_remove(&info->entry);
            list_add_tail(&thread_info.entry, &info->entry);
            info->team          = &team_info;
            info->thread_num    = team_info.num_threads++;
            info->section       = 0;
            WakeAllConditionVariable(&info->cond);
        }

        /* Spawn additional new threads. */
        while (team_info.num_threads < num_threads)
        {
            struct vcomp_thread_info *info;
            HMODULE module;
            HANDLE thread;

            info = HeapAlloc(GetProcessHeap(), 0, sizeof(*info));
            if (!info) break;

            InitializeConditionVariable(&info->cond);
            info->team       = &team_info;
            info->thread_num = team_info.num_threads;
            info->section    = 0;

            thread = CreateThread(NULL, 0, _vcomp_fork_worker, info, 0, NULL);
            if (!thread)
            {
                HeapFree(GetProcessHeap(), 0, info);
                break;
            }

            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                               (const WCHAR *)vcomp_module, &module);

            list_add_tail(&thread_info.entry, &info->entry);
            team_info.num_threads++;
            CloseHandle(thread);
        }

        LeaveCriticalSection(&vcomp_section);
    }

    /* Call the callback in the context of the new team. */
    prev_thread_info = vcomp_get_thread_info();
    vcomp_set_thread_info(&thread_info);
    _vcomp_fork_call_wrapper(team_info.wrapper, team_info.nargs, team_info.valist);
    vcomp_set_thread_info(prev_thread_info);

    /* Implicit join, wait for other tasks. */
    if (parallel)
    {
        EnterCriticalSection(&vcomp_section);

        team_info.finished_threads++;
        while (team_info.finished_threads < team_info.num_threads)
            SleepConditionVariableCS(&team_info.cond, &vcomp_section, INFINITE);

        LeaveCriticalSection(&vcomp_section);
        assert(list_empty(&thread_info.entry));
    }

    __ms_va_end(team_info.valist);
}

#if defined(__i386__)
__ASM_GLOBAL_FUNC( _vcomp_fork_call_wrapper,
                   "pushl %ebp\n\t"
                   __ASM_CFI(".cfi_adjust_cfa_offset 4\n\t")
                   __ASM_CFI(".cfi_rel_offset %ebp,0\n\t")
                   "movl %esp,%ebp\n\t"
                   __ASM_CFI(".cfi_def_cfa_register %ebp\n\t")
                   "pushl %esi\n\t"
                   __ASM_CFI(".cfi_rel_offset %esi,-4\n\t")
                   "pushl %edi\n\t"
                   __ASM_CFI(".cfi_rel_offset %edi,-8\n\t")
                   "movl 12(%ebp),%edx\n\t"
                   "movl %esp,%edi\n\t"
                   "shll $2,%edx\n\t"
                   "jz 1f\n\t"
                   "subl %edx,%edi\n\t"
                   "andl $~15,%edi\n\t"
                   "movl %edi,%esp\n\t"
                   "movl 12(%ebp),%ecx\n\t"
                   "movl 16(%ebp),%esi\n\t"
                   "cld\n\t"
                   "rep; movsl\n"
                   "1:\tcall *8(%ebp)\n\t"
                   "leal -8(%ebp),%esp\n\t"
                   "popl %edi\n\t"
                   __ASM_CFI(".cfi_same_value %edi\n\t")
                   "popl %esi\n\t"
                   __ASM_CFI(".cfi_same_value %esi\n\t")
                   "popl %ebp\n\t"
                   __ASM_CFI(".cfi_def_cfa %esp,4\n\t")
                   __ASM_CFI(".cfi_same_value %ebp\n\t")
                   "ret" )

#elif defined(__x86_64__)

__ASM_GLOBAL_FUNC( _vcomp_fork_call_wrapper,
                   "pushq %rbp\n\t"
                   __ASM_CFI(".cfi_adjust_cfa_offset 8\n\t")
                   __ASM_CFI(".cfi_rel_offset %rbp,0\n\t")
                   "movq %rsp,%rbp\n\t"
                   __ASM_CFI(".cfi_def_cfa_register %rbp\n\t")
                   "pushq %rsi\n\t"
                   __ASM_CFI(".cfi_rel_offset %rsi,-8\n\t")
                   "pushq %rdi\n\t"
                   __ASM_CFI(".cfi_rel_offset %rdi,-16\n\t")
                   "movq %rcx,%rax\n\t"
                   "movq $4,%rcx\n\t"
                   "cmp %rcx,%rdx\n\t"
                   "cmovgq %rdx,%rcx\n\t"
                   "leaq 0(,%rcx,8),%rdx\n\t"
                   "subq %rdx,%rsp\n\t"
                   "andq $~15,%rsp\n\t"
                   "movq %rsp,%rdi\n\t"
                   "movq %r8,%rsi\n\t"
                   "rep; movsq\n\t"
                   "movq 0(%rsp),%rcx\n\t"
                   "movq 8(%rsp),%rdx\n\t"
                   "movq 16(%rsp),%r8\n\t"
                   "movq 24(%rsp),%r9\n\t"
                   "callq *%rax\n\t"
                   "leaq -16(%rbp),%rsp\n\t"
                   "popq %rdi\n\t"
                   __ASM_CFI(".cfi_same_value %rdi\n\t")
                   "popq %rsi\n\t"
                   __ASM_CFI(".cfi_same_value %rsi\n\t")
                   __ASM_CFI(".cfi_def_cfa_register %rsp\n\t")
                   "popq %rbp\n\t"
                   __ASM_CFI(".cfi_adjust_cfa_offset -8\n\t")
                   __ASM_CFI(".cfi_same_value %rbp\n\t")
                   "ret")
#else

void CDECL _vcomp_fork_call_wrapper(void *wrapper, int nargs, __ms_va_list args)
{
    ERR("Not implemented for this architecture\n");
}

#endif

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    TRACE("(0x%p, %d, %p)\n", hinstDLL, fdwReason, lpvReserved);

    switch (fdwReason)
    {
        case DLL_WINE_PREATTACH:
            return FALSE;    /* prefer native version */

        case DLL_PROCESS_ATTACH:
            vcomp_module = hinstDLL;
            DisableThreadLibraryCalls(hinstDLL);
            if ((vcomp_context_tls = TlsAlloc()) == TLS_OUT_OF_INDEXES)
            {
                ERR("Failed to allocate TLS index\n");
                return FALSE;
            }
            break;

        case DLL_PROCESS_DETACH:
            TlsFree(vcomp_context_tls);
            break;
    }

    return TRUE;
}
