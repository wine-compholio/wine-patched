/*
 * Object-oriented thread pool API
 *
 * Copyright 2014-2015 Sebastian Lackner
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

#include <assert.h>
#include <stdarg.h>
#include <limits.h>

#define NONAMELESSUNION
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "winternl.h"

#include "wine/debug.h"
#include "wine/list.h"

#include "ntdll_misc.h"

WINE_DEFAULT_DEBUG_CHANNEL(threadpool);

static inline LONG interlocked_inc( PLONG dest )
{
    return interlocked_xchg_add( dest, 1 ) + 1;
}

static inline LONG interlocked_dec( PLONG dest )
{
    return interlocked_xchg_add( dest, -1 ) - 1;
}

#define THREADPOOL_WORKER_TIMEOUT 5000

/* internal threadpool representation */
struct threadpool
{
    LONG                    refcount;
    BOOL                    shutdown;
    CRITICAL_SECTION        cs;
    /* pool of work items, locked via .cs */
    struct list             pool;
    RTL_CONDITION_VARIABLE  update_event;
    /* information about worker threads, locked via .cs */
    int                     max_workers;
    int                     min_workers;
    int                     num_workers;
    int                     num_busy_workers;
};

enum threadpool_objtype
{
    TP_OBJECT_TYPE_SIMPLE
};

/* internal threadpool object representation */
struct threadpool_object
{
    LONG                    refcount;
    BOOL                    shutdown;
    /* read-only information */
    enum threadpool_objtype type;
    struct threadpool       *pool;
    struct threadpool_group *group;
    PVOID                   userdata;
    /* information about the group, locked via .group->cs */
    struct list             group_entry;
    BOOL                    is_group_member;
    /* information about the pool, locked via .pool->cs */
    struct list             pool_entry;
    RTL_CONDITION_VARIABLE  finished_event;
    LONG                    num_pending_callbacks;
    LONG                    num_running_callbacks;
    /* arguments for callback */
    union
    {
        struct
        {
            PTP_SIMPLE_CALLBACK callback;
        } simple;
    } u;
};

/* internal threadpool group representation */
struct threadpool_group
{
    LONG                    refcount;
    BOOL                    shutdown;
    CRITICAL_SECTION        cs;
    /* list of group members, locked via .cs */
    struct list             members;
};

static inline struct threadpool *impl_from_TP_POOL( TP_POOL *pool )
{
    return (struct threadpool *)pool;
}

static inline struct threadpool_group *impl_from_TP_CLEANUP_GROUP( TP_CLEANUP_GROUP *group )
{
    return (struct threadpool_group *)group;
}

static void CALLBACK threadpool_worker_proc( void *param );
static NTSTATUS tp_threadpool_alloc( struct threadpool **out );
static void tp_threadpool_shutdown( struct threadpool *pool );
static BOOL tp_threadpool_release( struct threadpool *pool );
static void tp_object_submit( struct threadpool_object *object );
static void tp_object_shutdown( struct threadpool_object *object );
static BOOL tp_object_release( struct threadpool_object *object );
static BOOL tp_group_release( struct threadpool_group *group );

static struct threadpool *default_threadpool = NULL;

/* allocates or returns the default threadpool */
static struct threadpool *get_default_threadpool( void )
{
    if (!default_threadpool)
    {
        struct threadpool *pool;

        if (tp_threadpool_alloc( &pool ) != STATUS_SUCCESS)
            return NULL;

        if (interlocked_cmpxchg_ptr( (void *)&default_threadpool, pool, NULL ) != NULL)
        {
            tp_threadpool_shutdown( pool );
            tp_threadpool_release( pool );
        }
    }
    return default_threadpool;
}

/* allocate a new threadpool (with at least one worker thread) */
static NTSTATUS tp_threadpool_alloc( struct threadpool **out )
{
    struct threadpool *pool;
    NTSTATUS status;
    HANDLE thread;

    pool = RtlAllocateHeap( GetProcessHeap(), 0, sizeof(*pool) );
    if (!pool)
        return STATUS_NO_MEMORY;

    pool->refcount              = 2; /* this thread + worker proc */
    pool->shutdown              = FALSE;

    RtlInitializeCriticalSection( &pool->cs );
    pool->cs.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": threadpool.cs");

    list_init( &pool->pool );
    RtlInitializeConditionVariable( &pool->update_event );

    pool->max_workers           = 500;
    pool->min_workers           = 1;
    pool->num_workers           = 1;
    pool->num_busy_workers      = 0;

    status = RtlCreateUserThread( GetCurrentProcess(), NULL, FALSE, NULL, 0, 0,
                                  threadpool_worker_proc, pool, &thread, NULL );
    if (status != STATUS_SUCCESS)
    {
        pool->cs.DebugInfo->Spare[0] = 0;
        RtlDeleteCriticalSection( &pool->cs );
        RtlFreeHeap( GetProcessHeap(), 0, pool );
        return status;
    }
    NtClose( thread );

    TRACE("allocated threadpool %p\n", pool);

    *out = pool;
    return STATUS_SUCCESS;
}

/* shutdown all threads of the threadpool */
static void tp_threadpool_shutdown( struct threadpool *pool )
{
    assert( pool != default_threadpool );

    pool->shutdown = TRUE;
    RtlWakeAllConditionVariable( &pool->update_event );
}

/* release a reference to a threadpool */
static BOOL tp_threadpool_release( struct threadpool *pool )
{
    if (interlocked_dec( &pool->refcount ))
        return FALSE;

    TRACE("destroying threadpool %p\n", pool);

    assert( pool->shutdown );
    assert( list_empty( &pool->pool ) );

    pool->cs.DebugInfo->Spare[0] = 0;
    RtlDeleteCriticalSection( &pool->cs );

    RtlFreeHeap( GetProcessHeap(), 0, pool );
    return TRUE;
}

/* threadpool worker function */
static void CALLBACK threadpool_worker_proc( void *param )
{
    struct threadpool *pool = param;
    LARGE_INTEGER timeout;
    struct list *ptr;

    RtlEnterCriticalSection( &pool->cs );
    for (;;)
    {
        while ((ptr = list_head( &pool->pool )))
        {
            struct threadpool_object *object = LIST_ENTRY( ptr, struct threadpool_object, pool_entry );
            assert( object->num_pending_callbacks > 0 );

            /* If further pending callbacks are queued, move the work item to
             * the end of the pool list. Otherwise remove it from the pool. */
            list_remove( &object->pool_entry );
            if (--object->num_pending_callbacks)
                list_add_tail( &pool->pool, &object->pool_entry );

            /* Leave critical section and do the actual callback. */
            object->num_running_callbacks++;
            pool->num_busy_workers++;
            RtlLeaveCriticalSection( &pool->cs );

            switch (object->type)
            {
                case TP_OBJECT_TYPE_SIMPLE:
                {
                    TRACE( "executing simple callback %p(NULL, %p)\n",
                           object->u.simple.callback, object->userdata );
                    object->u.simple.callback( NULL, object->userdata );
                    TRACE( "callback %p returned\n", object->u.simple.callback );
                    break;
                }

                default:
                    assert(0);
                    break;
            }

            RtlEnterCriticalSection( &pool->cs );
            pool->num_busy_workers--;
            object->num_running_callbacks--;
            if (!object->num_pending_callbacks && !object->num_running_callbacks)
                RtlWakeAllConditionVariable( &object->finished_event );
            tp_object_release( object );
        }

        /* Shutdown worker thread if requested. */
        if (pool->shutdown)
            break;

        /* Wait for new tasks or until timeout expires. Never terminate the last worker. */
        timeout.QuadPart = (ULONGLONG)THREADPOOL_WORKER_TIMEOUT * -10000;
        if (RtlSleepConditionVariableCS( &pool->update_event, &pool->cs, &timeout ) == STATUS_TIMEOUT &&
            !list_head( &pool->pool ) && pool->num_workers > 1)
        {
            break;
        }
    }
    pool->num_workers--;
    RtlLeaveCriticalSection( &pool->cs );
    tp_threadpool_release( pool );
}

/* initializes a new threadpool object */
static void tp_object_initialize( struct threadpool_object *object, struct threadpool *pool,
                                  PVOID userdata, TP_CALLBACK_ENVIRON *environment )
{
    BOOL simple_cb = (object->type == TP_OBJECT_TYPE_SIMPLE);

    object->refcount                = 1;
    object->shutdown                = FALSE;

    object->pool                    = pool;
    object->group                   = NULL;
    object->userdata                = userdata;

    memset( &object->group_entry, 0, sizeof(object->group_entry) );
    object->is_group_member         = FALSE;

    memset( &object->pool_entry, 0, sizeof(object->pool_entry) );
    RtlInitializeConditionVariable( &object->finished_event );
    object->num_pending_callbacks   = 0;
    object->num_running_callbacks   = 0;

    if (environment)
    {
        if (environment->Version != 1)
            FIXME("unsupported environment version %u\n", environment->Version);

        object->group = impl_from_TP_CLEANUP_GROUP( environment->CleanupGroup );

        WARN("environment not fully implemented yet\n");
    }

    /* Increase reference-count on the pool */
    interlocked_inc( &pool->refcount );

    TRACE("allocated object %p of type %u\n", object, object->type);

    /* For simple callbacks we have to run tp_object_submit before adding this object
     * to the cleanup group. As soon as the cleanup group members are released ->shutdown
     * will be set, and tp_object_submit would fail with an assertion. */
    if (simple_cb)
        tp_object_submit( object );

    if (object->group)
    {
        struct threadpool_group *group = object->group;
        interlocked_inc( &group->refcount );

        RtlEnterCriticalSection( &group->cs );
        list_add_tail( &group->members, &object->group_entry );
        object->is_group_member = TRUE;
        RtlLeaveCriticalSection( &group->cs );
    }

    if (simple_cb)
    {
        tp_object_shutdown( object );
        tp_object_release( object );
    }
}

/* allocates and submits a 'simple' threadpool task. */
static NTSTATUS tp_object_submit_simple( PTP_SIMPLE_CALLBACK callback, PVOID userdata,
                                         TP_CALLBACK_ENVIRON *environment )
{
    struct threadpool_object *object;
    struct threadpool *pool = NULL;

    if (environment)
        pool = (struct threadpool *)environment->Pool;

    if (!pool)
    {
        pool = get_default_threadpool();
        if (!pool)
            return STATUS_NO_MEMORY;
    }

    object = RtlAllocateHeap( GetProcessHeap(), 0, sizeof(*object) );
    if (!object)
        return STATUS_NO_MEMORY;

    object->type = TP_OBJECT_TYPE_SIMPLE;
    object->u.simple.callback = callback;
    tp_object_initialize( object, pool, userdata, environment );

    return STATUS_SUCCESS;
}

/* submits an object to a threadpool */
static void tp_object_submit( struct threadpool_object *object )
{
    struct threadpool *pool = object->pool;

    assert( !object->shutdown );
    assert( !pool->shutdown );

    RtlEnterCriticalSection( &pool->cs );

    /* Start new worker threads if required (and allowed) */
    if (pool->num_busy_workers >= pool->num_workers && pool->num_workers < pool->max_workers)
    {
        NTSTATUS status;
        HANDLE thread;

        status = RtlCreateUserThread( GetCurrentProcess(), NULL, FALSE, NULL, 0, 0,
                                      threadpool_worker_proc, pool, &thread, NULL );
        if (status == STATUS_SUCCESS)
        {
            interlocked_inc( &pool->refcount );
            pool->num_workers++;
            NtClose( thread );
            goto out;
        }
    }

    assert( pool->num_workers > 0 );
    RtlWakeConditionVariable( &pool->update_event );

out:
    /* Queue work item into pool and increment refcount */
    interlocked_inc( &object->refcount );
    if (!object->num_pending_callbacks++)
        list_add_tail( &pool->pool, &object->pool_entry );

    RtlLeaveCriticalSection( &pool->cs );
}

static void tp_object_cancel( struct threadpool_object *object )
{
    struct threadpool *pool = object->pool;
    LONG pending_callbacks = 0;

    RtlEnterCriticalSection( &pool->cs );

    /* Remove the pending callbacks from the pool */
    if (object->num_pending_callbacks)
    {
        pending_callbacks = object->num_pending_callbacks;
        object->num_pending_callbacks = 0;
        list_remove( &object->pool_entry );
    }

    RtlLeaveCriticalSection( &pool->cs );

    /* Release references */
    while (pending_callbacks--)
        tp_object_release( object );
}

static void tp_object_wait( struct threadpool_object *object )
{
    struct threadpool *pool = object->pool;

    RtlEnterCriticalSection( &pool->cs );

    /* Wait until there are no longer pending or running callbacks */
    while (object->num_pending_callbacks || object->num_running_callbacks)
        RtlSleepConditionVariableCS( &object->finished_event, &pool->cs, NULL );

    RtlLeaveCriticalSection( &pool->cs );
}

/* mark an object as 'shutdown', submitting is no longer possible */
static void tp_object_shutdown( struct threadpool_object *object )
{
    object->shutdown = TRUE;
}

/* release a reference to a threadpool object */
static BOOL tp_object_release( struct threadpool_object *object )
{
    if (interlocked_dec( &object->refcount ))
        return FALSE;

    TRACE("destroying object %p of type %u\n", object, object->type);

    assert( object->shutdown );
    assert( !object->num_pending_callbacks );
    assert( !object->num_running_callbacks );

    /* release reference to the group */
    if (object->group)
    {
        struct threadpool_group *group = object->group;

        RtlEnterCriticalSection( &group->cs );
        if (object->is_group_member)
        {
            list_remove( &object->group_entry );
            object->is_group_member = FALSE;
        }
        RtlLeaveCriticalSection( &group->cs );

        tp_group_release( group );
    }

    /* release reference to threadpool */
    tp_threadpool_release( object->pool );

    RtlFreeHeap( GetProcessHeap(), 0, object );
    return TRUE;
}

/* allocates a new cleanup group */
static NTSTATUS tp_group_alloc( struct threadpool_group **out )
{
    struct threadpool_group *group;

    group = RtlAllocateHeap( GetProcessHeap(), 0, sizeof(*group) );
    if (!group)
        return STATUS_NO_MEMORY;

    group->refcount     = 1;
    group->shutdown     = FALSE;

    RtlInitializeCriticalSection( &group->cs );
    group->cs.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": threadpool_group.cs");

    list_init( &group->members );

    TRACE("allocated group %p\n", group);

    *out = group;
    return STATUS_SUCCESS;
}

/* marks a cleanup group for shutdown */
static void tp_group_shutdown( struct threadpool_group *group )
{
    group->shutdown = TRUE;
}

/* releases a reference to a cleanup group */
static BOOL tp_group_release( struct threadpool_group *group )
{
    if (interlocked_dec( &group->refcount ))
        return FALSE;

    TRACE("destroying group %p\n", group);

    assert( group->shutdown );
    assert( list_empty( &group->members ) );

    group->cs.DebugInfo->Spare[0] = 0;
    RtlDeleteCriticalSection( &group->cs );

    RtlFreeHeap( GetProcessHeap(), 0, group );
    return TRUE;
}

/* releases all group members of a cleanup group */
static void tp_group_release_members( struct threadpool_group *group, BOOL cancel_pending, PVOID userdata )
{
    struct threadpool_object *object, *next;
    struct list members;

    RtlEnterCriticalSection( &group->cs );

    /* Unset group, increase references, and mark objects for shutdown */
    LIST_FOR_EACH_ENTRY_SAFE( object, next, &group->members, struct threadpool_object, group_entry )
    {
        assert( object->group == group );
        assert( object->is_group_member );

        /* Simple callbacks are very special. The user doesn't hold any reference, so
         * they would be released too early. Add one additional temporary reference. */
        if (object->type == TP_OBJECT_TYPE_SIMPLE)
        {
            if (interlocked_inc( &object->refcount ) == 1)
            {
                /* Object is basically already destroyed, but group reference
                 * was not deleted yet. We can safely ignore this object. */
                interlocked_dec( &object->refcount );
                list_remove( &object->group_entry );
                object->is_group_member = FALSE;
                continue;
            }
        }

        object->is_group_member = FALSE;
        tp_object_shutdown( object );
    }

    /* Move members to a local list */
    list_init( &members );
    list_move_tail( &members, &group->members );

    RtlLeaveCriticalSection( &group->cs );

    /* Cancel pending callbacks if requested */
    if (cancel_pending)
    {
        LIST_FOR_EACH_ENTRY( object, &members, struct threadpool_object, group_entry )
        {
            tp_object_cancel( object );
        }
    }

    /* Wait for remaining callbacks to finish */
    LIST_FOR_EACH_ENTRY_SAFE( object, next, &members, struct threadpool_object, group_entry )
    {
        tp_object_wait( object );
        tp_object_release( object );
    }
}

/***********************************************************************
 *           TpAllocCleanupGroup    (NTDLL.@)
 */
NTSTATUS WINAPI TpAllocCleanupGroup( TP_CLEANUP_GROUP **out )
{
    TRACE("%p\n", out);

    if (!out)
        return STATUS_ACCESS_VIOLATION;

    return tp_group_alloc( (struct threadpool_group **)out );
}

/***********************************************************************
 *           TpAllocPool    (NTDLL.@)
 */
NTSTATUS WINAPI TpAllocPool( TP_POOL **out, PVOID reserved )
{
    TRACE("%p %p\n", out, reserved);

    if (reserved)
        FIXME("reserved argument is nonzero (%p)", reserved);

    if (!out)
        return STATUS_ACCESS_VIOLATION;

    return tp_threadpool_alloc( (struct threadpool **)out );
}

/***********************************************************************
 *           TpReleaseCleanupGroup    (NTDLL.@)
 */
VOID WINAPI TpReleaseCleanupGroup( TP_CLEANUP_GROUP *group )
{
    struct threadpool_group *this = impl_from_TP_CLEANUP_GROUP( group );
    TRACE("%p\n", group);

    if (this)
    {
        tp_group_shutdown( this );
        tp_group_release( this );
    }
}

/***********************************************************************
 *           TpReleaseCleanupGroupMembers    (NTDLL.@)
 */
VOID WINAPI TpReleaseCleanupGroupMembers( TP_CLEANUP_GROUP *group, BOOL cancel_pending, PVOID userdata )
{
    struct threadpool_group *this = impl_from_TP_CLEANUP_GROUP( group );
    TRACE("%p %d %p\n", group, cancel_pending, userdata);

    if (this)
    {
        tp_group_release_members( this, cancel_pending, userdata );
    }
}

/***********************************************************************
 *           TpReleasePool    (NTDLL.@)
 */
VOID WINAPI TpReleasePool( TP_POOL *pool )
{
    struct threadpool *this = impl_from_TP_POOL( pool );
    TRACE("%p\n", pool);

    if (this)
    {
        tp_threadpool_shutdown( this );
        tp_threadpool_release( this );
    }
}

/***********************************************************************
 *           TpSetPoolMaxThreads    (NTDLL.@)
 */
VOID WINAPI TpSetPoolMaxThreads( TP_POOL *pool, DWORD maximum )
{
    struct threadpool *this = impl_from_TP_POOL( pool );
    TRACE("%p %d\n", pool, maximum);

    if (this)
    {
        RtlEnterCriticalSection( &this->cs );
        this->max_workers = max(maximum, 1);
        RtlLeaveCriticalSection( &this->cs );
    }
}

/***********************************************************************
 *           TpSetPoolMinThreads    (NTDLL.@)
 */
BOOL WINAPI TpSetPoolMinThreads( TP_POOL *pool, DWORD minimum )
{
    struct threadpool *this = impl_from_TP_POOL( pool );
    FIXME("%p %d: semi-stub\n", pool, minimum);

    if (this)
    {
        RtlEnterCriticalSection( &this->cs );
        this->min_workers = max(minimum, 1);
        RtlLeaveCriticalSection( &this->cs );
    }
    return TRUE;
}

/***********************************************************************
 *           TpSimpleTryPost    (NTDLL.@)
 */
NTSTATUS WINAPI TpSimpleTryPost( PTP_SIMPLE_CALLBACK callback, PVOID userdata,
                                 TP_CALLBACK_ENVIRON *environment )
{
    TRACE("%p %p %p\n", callback, userdata, environment);

    return tp_object_submit_simple( callback, userdata, environment );
}
