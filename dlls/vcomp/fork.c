/*
 * vcomp fork/join implementation
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

struct vcomp_team
{
    struct vcomp_team *parent;
};

static inline struct vcomp_team *vcomp_get_team(void)
{
    return (struct vcomp_team *)TlsGetValue(vcomp_context_tls);
}

static inline void vcomp_set_team(struct vcomp_team *team)
{
    TlsSetValue(vcomp_context_tls, team);
}

WINE_DEFAULT_DEBUG_CHANNEL(vcomp);

void CDECL _vcomp_fork_call_wrapper(void *wrapper, int nargs, __ms_va_list args);

/* When Visual C encounters a '#pragma omp parallel' directive,
 * it wraps the next statement in a function, and passes the address
 * of the wrapper function to _vcomp_fork, which calls that function--
 * possibly after spawning extra copies on new threads.
 *
 * If the directive has an if() clause, the value passed to the if clause
 * is passed as the first argument to _vcomp_fork; if it is false,
 * or if OMP_NUM_THREADS is 1, or omp_set_num_threads(1) has been called,
 * or if too many threads are already in use, native _vcomp_fork doesn't spawn
 * any extra threads, it just calls the wrapper function.
 *
 * The OpenMP standard allows implementations to fall back to executing
 * everything on a single thread, so that's what we'll do for now;
 * our _vcomp_fork will simply call the wrapper function.
 * That's enough to make many, but not all, apps run correctly.
 *
 * If the statement being wrapped refers to variables from an outer scope,
 * Visual C passes them to _vcomp_fork and thence the wrapper as follows:
 * - Unchanging ints are always passed by value
 * - Unchanging floats are passed by value on i386, but by reference on amd64
 * - Everything else is passed by reference
 *
 * The call to _vcomp_fork is synthesized by the compiler;
 * user code isn't even aware that a call is being made.  And the callee
 * (_vcomp_fork) is also under Visual C's control.  Thus the compiler
 * is free to use a nonstandard  ABI for this call.  And it does, in that
 * float arguments are not promoted to double.  (Some apps
 * that use floats would probably be very annoyed if they were silently
 * promoted to doubles by "#pragma omp parallel".)
 *
 * The call from _vcomp_fork to the wrapper function also doesn't quite
 * follow the normal win32/win64 calling conventions:
 * 1) Since Visual C never passes floats or doubles by value to the
 * wrapper on amd64, native vcomp.dll does not copy floating point parameters
 * to registers, contrary to the win64 ABI.  Manual tests confirm this.
 * 2) Since the wrapper itself doesn't use varargs at all, _vcomp_fork can't
 * just pass an __ms_va_list; it has to push the arguments onto the stack again.
 * This can't be done in C, so we use assembly in _vcomp_fork_call_wrapper.
 * (That function is a close copy of call_method in oleaut32/typelib.c,
 * with unneeded instructions removed.)
 */

void WINAPIV _vcomp_fork(BOOL ifval, int nargs, void *wrapper, ...)
{
    __ms_va_list valist;
    struct vcomp_team team;

    TRACE("(%d, %d, %p, ...)\n", ifval, nargs, wrapper);

    team.parent = vcomp_get_team();
    vcomp_set_team(&team);

    __ms_va_start(valist, wrapper);
    _vcomp_fork_call_wrapper(wrapper, nargs, valist);
    __ms_va_end(valist);

    vcomp_set_team(team.parent);
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

int CDECL omp_in_parallel(void)
{
    int val = (vcomp_get_team() != NULL);

    TRACE("returning %d\n", val);
    return val;
}
