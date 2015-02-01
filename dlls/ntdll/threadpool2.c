/*
 * Vista Threadpool implementation
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

/* Besides winetests the following resources were used to implement some
 * internal details of the threadpool implementation:
 *
 *   [1] Concurrent Programming on Windows, by Joe Duffy
 */

static inline LONG interlocked_inc( PLONG dest )
{
    return interlocked_xchg_add( dest, 1 ) + 1;
}

static inline LONG interlocked_dec( PLONG dest )
{
    return interlocked_xchg_add( dest, -1 ) - 1;
}

#define THREADPOOL_WORKER_TIMEOUT 5000

/* allocated on the stack while a callback is running */
struct threadpool_instance
{
    struct threadpool_object *object;
    DWORD threadid;
    LONG disassociated;
    LONG may_run_long;

    /* cleanup actions */
    struct
    {
        CRITICAL_SECTION *critical_section;
        HANDLE mutex;
        HANDLE semaphore;
        LONG   semaphore_count;
        HANDLE event;
        HMODULE library;
    } cleanup;
};

/* internal threadpool representation */
struct threadpool
{
    LONG refcount;
    BOOL shutdown;
    CRITICAL_SECTION cs;

    /* user-defined preferences for number of works */
    int max_workers;
    int min_workers;

    /* pool of work items, locked via .cs */
    struct list pool;
    RTL_CONDITION_VARIABLE update_event;

    /* information about worker threads, locked via .cs */
    int num_workers;
    int num_busy_workers;
};

/* internal threadpool object representation */
struct threadpool_object
{
    LONG refcount;
    BOOL shutdown;

    /* read-only information */
    struct threadpool *pool;
    struct threadpool_group *group;
    PVOID userdata;
    PTP_CLEANUP_GROUP_CANCEL_CALLBACK group_cancel_callback;
    PTP_SIMPLE_CALLBACK finalization_callback;
    LONG may_run_long;
    HMODULE race_dll;

    /* information about the group, locked via .group->cs */
    struct list group_entry; /* only used when .group != NULL */

    /* information about the pool, locked via .pool->cs */
    struct list pool_entry;  /* only used when .num_pending_callbacks != 0 */
    LONG num_pending_callbacks;
    LONG num_running_callbacks;
    RTL_CONDITION_VARIABLE finished_event;

    /* type of this object */
    enum
    {
        TP_OBJECT_TYPE_UNDEFINED,
        TP_OBJECT_TYPE_SIMPLE,
        TP_OBJECT_TYPE_WORK,
        TP_OBJECT_TYPE_TIMER
    } type;

    /* arguments for callback */
    union
    {
        /* simple callback */
        struct
        {
            PTP_SIMPLE_CALLBACK callback;
        } simple;
        /* work callback */
        struct
        {
            PTP_WORK_CALLBACK callback;
        } work;
        /* timer callback */
        struct
        {
            PTP_TIMER_CALLBACK callback;

            /* information about the timer, locked via timerqueue.cs */
            BOOL timer_initialized;
            BOOL timer_pending;
            struct list timer_entry;

            BOOL timer_set;
            ULONGLONG timeout;
            LONG period;
            LONG window_length;
        } timer;
    } u;
};

/* internal threadpool group representation */
struct threadpool_group
{
    LONG refcount;
    BOOL shutdown;
    CRITICAL_SECTION cs;

    /* locked via .cs */
    struct list members;
};

/* global timerqueue object */
static RTL_CRITICAL_SECTION_DEBUG timerqueue_debug;
static struct
{
    CRITICAL_SECTION cs;

    /* number of timer objects total */
    BOOL thread_running;
    LONG num_timers;

    /* list of pending timers */
    struct list pending_timers;
    RTL_CONDITION_VARIABLE update_event;
}
timerqueue =
{
    { &timerqueue_debug, -1, 0, 0, 0, 0 },
    FALSE,
    0,
    LIST_INIT( timerqueue.pending_timers ),
    RTL_CONDITION_VARIABLE_INIT
};
static RTL_CRITICAL_SECTION_DEBUG timerqueue_debug =
{
    0, 0, &timerqueue.cs,
    { &timerqueue_debug.ProcessLocksList, &timerqueue_debug.ProcessLocksList },
    0, 0, { (DWORD_PTR)(__FILE__ ": timerqueue.cs") }
};

static inline struct threadpool *impl_from_TP_POOL( TP_POOL *pool )
{
    return (struct threadpool *)pool;
}

static inline struct threadpool_object *impl_from_TP_WORK( TP_WORK *work )
{
    struct threadpool_object *object = (struct threadpool_object *)work;
    assert( !object || object->type == TP_OBJECT_TYPE_WORK );
    return object;
}

static inline struct threadpool_object *impl_from_TP_TIMER( TP_TIMER *timer )
{
    struct threadpool_object *object = (struct threadpool_object *)timer;
    assert( !object || object->type == TP_OBJECT_TYPE_TIMER );
    return object;
}

static inline struct threadpool_group *impl_from_TP_CLEANUP_GROUP( TP_CLEANUP_GROUP *group )
{
    return (struct threadpool_group *)group;
}

static inline struct threadpool_instance *impl_from_TP_CALLBACK_INSTANCE( TP_CALLBACK_INSTANCE *instance )
{
    return (struct threadpool_instance *)instance;
}

static void CALLBACK threadpool_worker_proc( void *param );
static void CALLBACK timerqueue_thread_proc( void *param );

static NTSTATUS tp_threadpool_alloc( struct threadpool **out );
static BOOL tp_threadpool_release( struct threadpool *pool );
static void tp_threadpool_shutdown( struct threadpool *pool );

static void tp_object_submit( struct threadpool_object *object );
static BOOL tp_object_release( struct threadpool_object *object );
static void tp_object_shutdown( struct threadpool_object *object );

static BOOL tp_group_release( struct threadpool_group *group );

/***********************************************************************
 * TIMERQUEUE IMPLEMENTATION
 ***********************************************************************
 *
 * Based on [1] there is only one (persistent) thread which handles
 * timer events. There is a similar implementation in ntdll/
 * threadpool.c, but its not directly possible to merge them because of
 * specific implementation differences, like handling several events at
 * once using a windowlength parameter. */

static NTSTATUS tp_timerqueue_acquire( struct threadpool_object *timer )
{
    NTSTATUS status = STATUS_SUCCESS;
    assert( timer->type == TP_OBJECT_TYPE_TIMER );

    timer->u.timer.timer_initialized = TRUE;
    timer->u.timer.timer_pending = FALSE;
    memset( &timer->u.timer.timer_entry, 0, sizeof(timer->u.timer.timer_entry) );

    timer->u.timer.timer_set = FALSE;
    timer->u.timer.timeout = 0;
    timer->u.timer.period = 0;
    timer->u.timer.window_length = 0;

    RtlEnterCriticalSection( &timerqueue.cs );

    if (!timerqueue.thread_running)
    {
        HANDLE thread;
        status = RtlCreateUserThread( GetCurrentProcess(), NULL, FALSE, NULL, 0, 0,
                                      timerqueue_thread_proc, NULL, &thread, NULL );
        if (status == STATUS_SUCCESS)
        {
            NtClose( thread );
            timerqueue.thread_running = TRUE;
        }
    }

    if (!status) timerqueue.num_timers++;
    RtlLeaveCriticalSection( &timerqueue.cs );
    return status;
}

static void tp_timerqueue_release( struct threadpool_object *timer )
{
    assert( timer->type == TP_OBJECT_TYPE_TIMER );
    RtlEnterCriticalSection( &timerqueue.cs );

    if (timer->u.timer.timer_initialized)
    {
        if (timer->u.timer.timer_pending)
        {
            list_remove( &timer->u.timer.timer_entry );
            timer->u.timer.timer_pending = FALSE;
        }

        if (!--timerqueue.num_timers)
        {
            assert( list_empty( &timerqueue.pending_timers ) );
            RtlWakeAllConditionVariable( &timerqueue.update_event );
        }

        timer->u.timer.timer_initialized = FALSE;
    }

    RtlLeaveCriticalSection( &timerqueue.cs );
}

static void tp_timerqueue_update_timer( struct threadpool_object *new_timer, LARGE_INTEGER *timeout,
                                        LONG period, LONG window_length )
{
    BOOL submit_timer = FALSE;
    struct threadpool_object *timer;
    ULONGLONG when;

    assert( new_timer->type == TP_OBJECT_TYPE_TIMER );
    RtlEnterCriticalSection( &timerqueue.cs );
    assert( new_timer->u.timer.timer_initialized );

    /* Remember if the timer is set or unset */
    new_timer->u.timer.timer_set = timeout != NULL;

    if (timeout)
    {
        when = timeout->QuadPart;

        /* A timeout of zero means that the timer should be submitted immediately */
        if (when == 0)
        {
            submit_timer = TRUE;
            if (!period)
            {
                timeout = NULL;
                goto update_timer;
            }
            when = (ULONGLONG)period * -10000;
        }

        /* Convert relative timeout to absolute */
        if ((LONGLONG)when < 0)
        {
            LARGE_INTEGER now;
            NtQuerySystemTime( &now );
            when = now.QuadPart - when;
        }
    }

update_timer:

    /* If timer is still pending, then remove the old one */
    if (new_timer->u.timer.timer_pending)
    {
        list_remove( &new_timer->u.timer.timer_entry );
        memset( &new_timer->u.timer.timer_entry, 0, sizeof(new_timer->u.timer.timer_entry) );
        new_timer->u.timer.timer_pending = FALSE;
    }

    /* Timer should be enabled again, add it to the queue */
    if (timeout)
    {
        new_timer->u.timer.timeout       = when;
        new_timer->u.timer.period        = period;
        new_timer->u.timer.window_length = window_length;

        /* insert new_timer into the timer queue */
        LIST_FOR_EACH_ENTRY( timer, &timerqueue.pending_timers, struct threadpool_object, u.timer.timer_entry )
        {
            assert( timer->type == TP_OBJECT_TYPE_TIMER );
            if (new_timer->u.timer.timeout < timer->u.timer.timeout)
                break;
        }
        list_add_before( &timer->u.timer.timer_entry, &new_timer->u.timer.timer_entry );

        /* wake up thread if it should expire earlier than before */
        if (list_head( &timerqueue.pending_timers ) == &new_timer->u.timer.timer_entry )
            RtlWakeAllConditionVariable( &timerqueue.update_event );

        new_timer->u.timer.timer_pending = TRUE;
    }

    RtlLeaveCriticalSection( &timerqueue.cs );

    if (submit_timer)
        tp_object_submit( new_timer );
}

static void CALLBACK timerqueue_thread_proc( void *param )
{
    LARGE_INTEGER now, timeout;
    ULONGLONG timeout_lower, timeout_upper;
    struct threadpool_object *other_timer;
    struct list *ptr;

    RtlEnterCriticalSection( &timerqueue.cs );

    for (;;)
    {
        NtQuerySystemTime( &now );

        while ((ptr = list_head( &timerqueue.pending_timers )))
        {
            struct threadpool_object *timer = LIST_ENTRY( ptr, struct threadpool_object, u.timer.timer_entry );
            assert( timer->type == TP_OBJECT_TYPE_TIMER );

            /* Timeout didn't expire yet, nothing to do */
            if (timer->u.timer.timeout > now.QuadPart)
                break;

            /* Queue a new callback in one of the worker threads */
            list_remove( &timer->u.timer.timer_entry );
            tp_object_submit( timer );

            /* Requeue the timer, except its marked for shutdown */
            if (!timer->shutdown && timer->u.timer.period)
            {
                /* Update the timeout, make sure its at least the current time (to avoid too many work items) */
                timer->u.timer.timeout += (ULONGLONG)timer->u.timer.period * 10000;
                if (timer->u.timer.timeout <= now.QuadPart)
                    timer->u.timer.timeout = now.QuadPart + 1;

                /* Insert timer back into the timer queue */
                LIST_FOR_EACH_ENTRY( other_timer, &timerqueue.pending_timers, struct threadpool_object, u.timer.timer_entry )
                {
                    assert( other_timer->type == TP_OBJECT_TYPE_TIMER );
                    if (timer->u.timer.timeout < other_timer->u.timer.timeout)
                        break;
                }
                list_add_before( &other_timer->u.timer.timer_entry, &timer->u.timer.timer_entry );
            }
            else
            {
                /* The element is no longer queued */
                timer->u.timer.timer_pending = FALSE;
            }
        }

        /* Determine next timeout - we use the window_length arguments to optimize wakeup times */
        timeout_lower = timeout_upper = TIMEOUT_INFINITE;
        LIST_FOR_EACH_ENTRY( other_timer, &timerqueue.pending_timers, struct threadpool_object, u.timer.timer_entry )
        {
            ULONGLONG new_timeout_upper;
            assert( other_timer->type == TP_OBJECT_TYPE_TIMER );
            if (other_timer->u.timer.timeout >= timeout_upper)
                break;

            timeout_lower     = other_timer->u.timer.timeout;
            new_timeout_upper = timeout_lower + (ULONGLONG)other_timer->u.timer.window_length * 10000;

            if (timeout_upper > new_timeout_upper)
                timeout_upper = new_timeout_upper;
        }


        if (!timerqueue.num_timers)
        {
            /* All timers have been destroyed, if no new timers are created within some amount of
             * time, then we can shutdown this thread. */
            timeout.QuadPart = (ULONGLONG)THREADPOOL_WORKER_TIMEOUT * -10000;
            if (RtlSleepConditionVariableCS( &timerqueue.update_event,
                &timerqueue.cs, &timeout ) == STATUS_TIMEOUT && !timerqueue.num_timers)
            {
                break;
            }
        }
        else
        {
            /* Wait for timer update events or until the next timer expires. */
            timeout.QuadPart = timeout_lower;
            RtlSleepConditionVariableCS( &timerqueue.update_event, &timerqueue.cs, &timeout );
        }
    }

    timerqueue.thread_running = FALSE;
    RtlLeaveCriticalSection( &timerqueue.cs );
}


/***********************************************************************
 * THREADPOOL INSTANCE IMPLEMENTATION
 ***********************************************************************/

static void tp_instance_initialize( struct threadpool_instance *instance, struct threadpool_object *object )
{
    instance->object                    = object;
    instance->threadid                  = GetCurrentThreadId();
    instance->disassociated             = FALSE;
    instance->may_run_long              = object->may_run_long;
    instance->cleanup.critical_section  = NULL;
    instance->cleanup.mutex             = NULL;
    instance->cleanup.semaphore         = NULL;
    instance->cleanup.semaphore_count   = 0;
    instance->cleanup.event             = NULL;
    instance->cleanup.library           = NULL;
}

static NTSTATUS tp_instance_cleanup( struct threadpool_instance *instance )
{
    NTSTATUS status;

    /* According to [1] subsequent functions are not executed if one of the
     * cleanup steps fails. The order is also based on the description in [1]. */
    if (instance->cleanup.critical_section)
    {
        RtlLeaveCriticalSection( instance->cleanup.critical_section );
    }
    if (instance->cleanup.mutex)
    {
        status = NtReleaseMutant( instance->cleanup.mutex, NULL );
        if (status != STATUS_SUCCESS)
            return status;
    }
    if (instance->cleanup.semaphore)
    {
        status = NtReleaseSemaphore( instance->cleanup.semaphore, instance->cleanup.semaphore_count, NULL );
        if (status != STATUS_SUCCESS)
            return status;
    }
    if (instance->cleanup.event)
    {
        status = NtSetEvent( instance->cleanup.event, NULL );
        if (status != STATUS_SUCCESS)
            return status;
    }
    if (instance->cleanup.library)
    {
        status = LdrUnloadDll( instance->cleanup.library );
        if (status != STATUS_SUCCESS)
            return status;
    }

    return STATUS_SUCCESS;
}

static void tp_instance_disassociate_thread( struct threadpool_instance *instance )
{
    struct threadpool_object *object;
    struct threadpool *pool;

    if (instance->threadid != GetCurrentThreadId())
    {
        ERR("called from wrong thread, ignoring\n");
        return;
    }
    if (instance->disassociated)
        return;

    object = instance->object;
    pool   = object->pool;
    RtlEnterCriticalSection( &pool->cs );

    object->num_running_callbacks--;
    if (!object->num_pending_callbacks && !object->num_running_callbacks)
        RtlWakeAllConditionVariable( &object->finished_event );

    RtlLeaveCriticalSection( &pool->cs );
    instance->disassociated = TRUE;
}

static BOOL tp_instance_may_run_long( struct threadpool_instance *instance )
{
    struct threadpool_object *object;
    struct threadpool *pool;
    NTSTATUS status = STATUS_SUCCESS;

    if (instance->threadid != GetCurrentThreadId())
    {
        ERR("called from wrong thread, ignoring\n");
        return FALSE;
    }
    if (instance->may_run_long)
        return TRUE;

    object = instance->object;
    pool   = object->pool;
    RtlEnterCriticalSection( &pool->cs );

    if (pool->num_busy_workers >= pool->num_workers && pool->num_workers < pool->max_workers)
    {
        HANDLE thread;
        status = RtlCreateUserThread( GetCurrentProcess(), NULL, FALSE, NULL, 0, 0,
                                      threadpool_worker_proc, pool, &thread, NULL );
        if (status == STATUS_SUCCESS)
        {
            interlocked_inc( &pool->refcount );
            pool->num_workers++;
            NtClose( thread );
        }
    }

    RtlLeaveCriticalSection( &pool->cs );
    instance->may_run_long = TRUE;
    return !status;
}

/***********************************************************************
 * THREADPOOL IMPLEMENTATION
 ***********************************************************************/

static struct threadpool *default_threadpool = NULL;
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

static BOOL tp_threadpool_release( struct threadpool *pool )
{
    if (interlocked_dec( &pool->refcount ))
        return FALSE;

    TRACE("destroying threadpool %p\n", pool);

    assert( pool != default_threadpool );
    assert( pool->shutdown );
    assert( list_empty( &pool->pool ) );

    pool->cs.DebugInfo->Spare[0] = 0;
    RtlDeleteCriticalSection( &pool->cs );

    RtlFreeHeap( GetProcessHeap(), 0, pool );
    return TRUE;
}

static void tp_threadpool_shutdown( struct threadpool *pool )
{
    assert( pool != default_threadpool );

    pool->shutdown = TRUE;
    RtlWakeAllConditionVariable( &pool->update_event );
}

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
            struct threadpool_instance instance;
            assert( object->num_pending_callbacks > 0 );

            /* If further pending callbacks are queued, move the work item to
             * the end of the pool list. Otherwise remove it from the pool. */
            list_remove( &object->pool_entry );
            if (--object->num_pending_callbacks)
                list_add_tail( &pool->pool, &object->pool_entry );
            object->num_running_callbacks++;

            /* Leave critical section and do the actual callback. */
            pool->num_busy_workers++;
            RtlLeaveCriticalSection( &pool->cs );
            tp_instance_initialize( &instance, object );

            /* Execute regular worker callback */
            switch (object->type)
            {
                case TP_OBJECT_TYPE_SIMPLE:
                {
                    TP_CALLBACK_INSTANCE *cb_instance = (TP_CALLBACK_INSTANCE *)&instance;
                    TRACE( "executing callback %p(%p, %p)\n",
                           object->u.simple.callback, cb_instance, object->userdata );
                    object->u.simple.callback( cb_instance, object->userdata );
                    TRACE( "callback %p returned\n", object->u.simple.callback );
                    break;
                }

                case TP_OBJECT_TYPE_WORK:
                {
                    TP_CALLBACK_INSTANCE *cb_instance = (TP_CALLBACK_INSTANCE *)&instance;
                    TRACE( "executing callback %p(%p, %p, %p)\n",
                           object->u.work.callback, cb_instance, object->userdata, object );
                    object->u.work.callback( cb_instance, object->userdata, (TP_WORK *)object );
                    TRACE( "callback %p returned\n", object->u.work.callback );
                    break;
                }

                case TP_OBJECT_TYPE_TIMER:
                {
                    TP_CALLBACK_INSTANCE *cb_instance = (TP_CALLBACK_INSTANCE *)&instance;
                    TRACE( "executing callback %p(%p, %p, %p)\n",
                           object->u.timer.callback, cb_instance, object->userdata, object );
                    object->u.timer.callback( cb_instance, object->userdata, (TP_TIMER *)object );
                    TRACE( "callback %p returned\n", object->u.timer.callback );
                    break;
                }

                default:
                    FIXME( "callback type %u not implemented\n", object->type );
                    break;
            }

            /* Execute finalization callback */
            if (object->finalization_callback)
            {
                TP_CALLBACK_INSTANCE *cb_instance = (TP_CALLBACK_INSTANCE *)&instance;
                TRACE( "executing finalization callback %p(%p, %p)\n",
                       object->finalization_callback, cb_instance, object->userdata );
                object->finalization_callback( cb_instance, object->userdata );
                TRACE( "finalization callback %p returned\n", object->finalization_callback );
            }

            /* Clean up any other resources */
            tp_instance_cleanup( &instance );
            RtlEnterCriticalSection( &pool->cs );
            pool->num_busy_workers--;

            /* If instance was not disassociated, then wake up waiting objects. */
            if (!instance.disassociated)
            {
                object->num_running_callbacks--;
                if (!object->num_pending_callbacks && !object->num_running_callbacks)
                    RtlWakeAllConditionVariable( &object->finished_event );
            }

            tp_object_release( object );
        }

        /* Shutdown worker thread if requested. */
        if (pool->shutdown)
            break;

        /* Wait for new tasks or until timeout expires. Never terminate the last worker. */
        timeout.QuadPart = (ULONGLONG)THREADPOOL_WORKER_TIMEOUT * -10000;
        if (RtlSleepConditionVariableCS( &pool->update_event, &pool->cs,
            &timeout ) == STATUS_TIMEOUT && !list_head( &pool->pool ) && pool->num_workers > 1)
        {
            break;
        }
    }
    pool->num_workers--;
    RtlLeaveCriticalSection( &pool->cs );
    tp_threadpool_release( pool );
}

/***********************************************************************
 * THREADPOOL OBJECT IMPLEMENTATION
 ***********************************************************************/

static void tp_object_initialize( struct threadpool_object *object, struct threadpool *pool,
                                  PVOID userdata, TP_CALLBACK_ENVIRON *environment, BOOL submit_and_release )
{
    object->refcount                = 1;
    object->shutdown                = FALSE;

    /* Read-only information */
    object->pool                    = pool;
    object->group                   = NULL;
    object->userdata                = userdata;
    object->group_cancel_callback   = NULL;
    object->finalization_callback   = NULL;
    object->may_run_long            = 0;
    object->race_dll                = NULL;

    /* Information about the group */
    memset( &object->group_entry, 0, sizeof(object->group_entry) );

    /* Information about the pool */
    memset( &object->pool_entry, 0, sizeof(object->pool_entry) );
    object->num_pending_callbacks      = 0;
    object->num_running_callbacks      = 0;
    RtlInitializeConditionVariable( &object->finished_event );

    /* Set properties according to environment, if given */
    if (environment)
    {

        /* Windows doesn't abort when the version field contains garbage */
        if (environment->Version != 1)
            FIXME("unsupported environment version %d\n", environment->Version);

        /* object->pool was already set */
        object->group                   = impl_from_TP_CLEANUP_GROUP( environment->CleanupGroup );
        object->group_cancel_callback   = environment->CleanupGroupCancelCallback;
        object->finalization_callback   = environment->FinalizationCallback;
        object->may_run_long            = environment->u.s.LongFunction != 0;
        object->race_dll                = environment->RaceDll;

        if (environment->ActivationContext)
            FIXME("activation context not supported yet\n");

        if (environment->u.s.Persistent)
            FIXME("persistent thread support not supported yet\n");
    }

    /* Increase dll refcount */
    if (object->race_dll)
        LdrAddRefDll( 0, object->race_dll );

    /* Increase reference-count on the pool */
    interlocked_inc( &pool->refcount );

    TRACE("allocated object %p of type %u\n", object, object->type);

    /* For simple callbacks we have to run tp_object_submit before adding this object
     * to the cleanup group. As soon as the cleanup group members are released ->shutdown
     * will be set, and tp_object_submit would fail with an assertion. */
    if (submit_and_release)
        tp_object_submit( object );

    /* Assign this object to a specific group. Please note that this has to be done
     * as the last step before returning a pointer to the application, otherwise
     * there is a risk of having race-conditions. */
    if (object->group)
    {
        struct threadpool_group *group = object->group;
        interlocked_inc( &group->refcount );

        RtlEnterCriticalSection( &group->cs );
        list_add_tail( &group->members, &object->group_entry );
        RtlLeaveCriticalSection( &group->cs );
    }

    if (submit_and_release)
    {
        tp_object_shutdown( object );
        tp_object_release( object );
    }
}

static NTSTATUS tp_object_submit_simple( PTP_SIMPLE_CALLBACK callback, PVOID userdata,
                                         TP_CALLBACK_ENVIRON *environment )
{
    struct threadpool_object *object;
    struct threadpool *pool;

    /* determine threadpool */
    pool = environment ? (struct threadpool *)environment->Pool : NULL;
    if (!pool) pool = get_default_threadpool();
    if (!pool) return STATUS_NO_MEMORY;

    object = RtlAllocateHeap( GetProcessHeap(), 0, sizeof(*object) );
    if (!object)
        return STATUS_NO_MEMORY;

    object->type = TP_OBJECT_TYPE_SIMPLE;
    object->u.simple.callback = callback;
    tp_object_initialize( object, pool, userdata, environment, TRUE );

    return STATUS_SUCCESS;
}

static NTSTATUS tp_object_alloc_work( struct threadpool_object **out, PTP_WORK_CALLBACK callback,
                                      PVOID userdata, TP_CALLBACK_ENVIRON *environment )
{
    struct threadpool_object *object;
    struct threadpool *pool;

    /* determine threadpool */
    pool = environment ? (struct threadpool *)environment->Pool : NULL;
    if (!pool) pool = get_default_threadpool();
    if (!pool) return STATUS_NO_MEMORY;

    object = RtlAllocateHeap( GetProcessHeap(), 0, sizeof(*object) );
    if (!object)
        return STATUS_NO_MEMORY;

    object->type = TP_OBJECT_TYPE_WORK;
    object->u.work.callback = callback;
    tp_object_initialize( object, pool, userdata, environment, FALSE );

    *out = object;
    return STATUS_SUCCESS;
}

static NTSTATUS tp_object_alloc_timer( struct threadpool_object **out, PTP_TIMER_CALLBACK callback,
                                       PVOID userdata, TP_CALLBACK_ENVIRON *environment )
{
    struct threadpool_object *object;
    struct threadpool *pool;
    NTSTATUS status;

    /* determine threadpool */
    pool = environment ? (struct threadpool *)environment->Pool : NULL;
    if (!pool) pool = get_default_threadpool();
    if (!pool) return STATUS_NO_MEMORY;

    object = RtlAllocateHeap( GetProcessHeap(), 0, sizeof(*object) );
    if (!object)
        return STATUS_NO_MEMORY;

    object->type = TP_OBJECT_TYPE_TIMER;
    object->u.timer.callback = callback;

    status = tp_timerqueue_acquire( object );
    if (status)
    {
        RtlFreeHeap( GetProcessHeap(), 0, object );
        return status;
    }

    tp_object_initialize( object, pool, userdata, environment, FALSE );

    *out = object;
    return STATUS_SUCCESS;
}

static BOOL tp_object_release( struct threadpool_object *object )
{
    struct threadpool_group *group;

    if (interlocked_dec( &object->refcount ))
        return FALSE;

    TRACE("destroying object %p of type %u\n", object, object->type);

    assert( object->shutdown );
    assert( !object->num_pending_callbacks );
    assert( !object->num_running_callbacks );

    /* release reference on the group */
    if ((group = object->group))
    {
        RtlEnterCriticalSection( &group->cs );
        list_remove( &object->group_entry );
        RtlLeaveCriticalSection( &group->cs );
        tp_group_release( group );
    }

    /* release reference to library */
    if (object->race_dll)
        LdrUnloadDll( object->race_dll );

    /* release reference to threadpool */
    tp_threadpool_release( object->pool );

    RtlFreeHeap( GetProcessHeap(), 0, object );
    return TRUE;
}

static void tp_object_shutdown( struct threadpool_object *object )
{
    if (object->type == TP_OBJECT_TYPE_TIMER)
    {
        /* release reference on the timerqueue */
        tp_timerqueue_release( object );
    }

    object->shutdown = TRUE;
}

static void tp_object_cancel( struct threadpool_object *object, BOOL group_cancel, PVOID userdata )
{
    struct threadpool *pool = object->pool;
    LONG pending_callbacks = 0;

    /* Remove the pending callbacks from the pool */
    RtlEnterCriticalSection( &pool->cs );
    if (object->num_pending_callbacks)
    {
        pending_callbacks = object->num_pending_callbacks;
        list_remove( &object->pool_entry );
        object->num_pending_callbacks = 0;
    }
    RtlLeaveCriticalSection( &pool->cs );

    /* Execute group cancellation callback if defined, and if this was actually a group cancel. */
    if (pending_callbacks && group_cancel && object->group_cancel_callback)
    {
        TRACE( "executing group cancel callback %p(%p, %p)\n", object->group_cancel_callback, object, userdata );
        object->group_cancel_callback( object, userdata );
        TRACE( "group cancel callback %p returned\n", object->group_cancel_callback );
    }

    /* remove references for removed pending callbacks */
    while (pending_callbacks--)
        tp_object_release( object );
}

static void tp_object_wait( struct threadpool_object *object )
{
    struct threadpool *pool = object->pool;
    RtlEnterCriticalSection( &pool->cs );

    while (object->num_pending_callbacks || object->num_running_callbacks)
        RtlSleepConditionVariableCS( &object->finished_event, &pool->cs, NULL );

    RtlLeaveCriticalSection( &pool->cs );
}

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
        }
        else
        {
            assert( pool->num_workers > 0 );
            RtlWakeConditionVariable( &pool->update_event );
        }
    }
    else RtlWakeConditionVariable( &pool->update_event );

    /* Queue work item into pool and increment refcount */
    if (!object->num_pending_callbacks++)
        list_add_tail( &pool->pool, &object->pool_entry );

    interlocked_inc( &object->refcount );

    RtlLeaveCriticalSection( &pool->cs );
}

/***********************************************************************
 * THREADPOOL GROUP IMPLEMENTATION
 ***********************************************************************/

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

static void tp_group_shutdown( struct threadpool_group *group )
{
    group->shutdown = TRUE;
}

static void tp_group_release_members( struct threadpool_group *group, BOOL cancel_pending, PVOID userdata )
{
    struct threadpool_object *object, *next;
    struct list members;

    /* We cannot keep the group locked until all tasks have finished.
     * Create a temporary list containing all tasks which were member of the group. */

    RtlEnterCriticalSection( &group->cs );
    LIST_FOR_EACH_ENTRY( object, &group->members, struct threadpool_object, group_entry )
    {
        /* reset the group - objects do no longer to remove manually from the group on destruction. */
        assert( object->group == group );
        object->group = NULL;

        /* Simple callbacks are very special. The user doesn't hold any reference, so
         * they would be released too early. Add one additional temporary reference. */
        if (object->type == TP_OBJECT_TYPE_SIMPLE)
            interlocked_inc(&object->refcount);

        /* Do not allow to submit new tasks for this object. */
        tp_object_shutdown( object );
    }

    list_init( &members );
    list_move_tail( &members, &group->members );
    RtlLeaveCriticalSection( &group->cs );

    /* Cancel pending tasks, execute the group cancel callback. */
    if (cancel_pending)
    {
        LIST_FOR_EACH_ENTRY( object, &members, struct threadpool_object, group_entry )
            tp_object_cancel( object, TRUE, userdata );
    }

    LIST_FOR_EACH_ENTRY_SAFE( object, next, &members, struct threadpool_object, group_entry )
    {
        /* Wait for tasks to finish, afterwards release one reference. This could destroy
         * the object, so we use LIST_FOR_EACH_ENTRY_SAFE. If the object is not destroyed,
         * then ->group_entry contains garbage, but that doesn't matter. It will not be
         * used anymore because ->group == NULL. */
        tp_object_wait( object );
        tp_object_release( object );

        /* Manually release the group reference */
        tp_group_release( group );
    }
}



/***********************************************************************
 *           TpAllocCleanupGroup    (NTDLL.@)
 */
NTSTATUS WINAPI TpAllocCleanupGroup( TP_CLEANUP_GROUP **out )
{
    TRACE("%p\n", out);
    if (!out) return STATUS_ACCESS_VIOLATION;
    return tp_group_alloc( (struct threadpool_group **)out );
}

/***********************************************************************
 *           TpAllocPool    (NTDLL.@)
 */
NTSTATUS WINAPI TpAllocPool( TP_POOL **out, PVOID reserved )
{
    TRACE("%p %p\n", out, reserved);
    if (reserved) FIXME("reserved argument is nonzero (%p)", reserved);
    if (!out) return STATUS_ACCESS_VIOLATION;
    return tp_threadpool_alloc( (struct threadpool **)out );
}

/***********************************************************************
 *           TpAllocTimer    (NTDLL.@)
 */
NTSTATUS WINAPI TpAllocTimer( TP_TIMER **out, PTP_TIMER_CALLBACK callback, PVOID userdata,
                              TP_CALLBACK_ENVIRON *environment )
{
    TRACE("%p %p %p %p\n", out, callback, userdata, environment);
    return tp_object_alloc_timer( (struct threadpool_object **)out, callback, userdata, environment );
}

/***********************************************************************
 *           TpAllocWork    (NTDLL.@)
 */
NTSTATUS WINAPI TpAllocWork( TP_WORK **out, PTP_WORK_CALLBACK callback, PVOID userdata,
                             TP_CALLBACK_ENVIRON *environment )
{
    TRACE("%p %p %p %p\n", out, callback, userdata, environment);
    return tp_object_alloc_work( (struct threadpool_object **)out, callback, userdata, environment );
}

/***********************************************************************
 *           TpCallbackLeaveCriticalSectionOnCompletion    (NTDLL.@)
 */
VOID WINAPI TpCallbackLeaveCriticalSectionOnCompletion( TP_CALLBACK_INSTANCE *instance, CRITICAL_SECTION *crit )
{
    struct threadpool_instance *this = impl_from_TP_CALLBACK_INSTANCE( instance );
    TRACE("%p %p\n", instance, crit);
    if (!this) return;
    if (this->cleanup.critical_section)
        FIXME("attempt to set multiple cleanup critical sections\n");
    else
        this->cleanup.critical_section = crit;
}

/***********************************************************************
 *           TpCallbackMayRunLong    (NTDLL.@)
 */
NTSTATUS WINAPI TpCallbackMayRunLong( TP_CALLBACK_INSTANCE *instance )
{
    struct threadpool_instance *this = impl_from_TP_CALLBACK_INSTANCE( instance );
    TRACE("%p\n", instance);
    if (!this) return STATUS_ACCESS_VIOLATION;
    return tp_instance_may_run_long( this );
}

/***********************************************************************
 *           TpCallbackReleaseMutexOnCompletion    (NTDLL.@)
 */
VOID WINAPI TpCallbackReleaseMutexOnCompletion( TP_CALLBACK_INSTANCE *instance, HANDLE mutex )
{
    struct threadpool_instance *this = impl_from_TP_CALLBACK_INSTANCE( instance );
    TRACE("%p %p\n", instance, mutex);
    if (!this) return;
    if (this->cleanup.mutex)
        FIXME("attempt to set multiple cleanup mutexes\n");
    else
        this->cleanup.mutex = mutex;
}

/***********************************************************************
 *           TpCallbackReleaseSemaphoreOnCompletion    (NTDLL.@)
 */
VOID WINAPI TpCallbackReleaseSemaphoreOnCompletion( TP_CALLBACK_INSTANCE *instance, HANDLE semaphore, DWORD count )
{
    struct threadpool_instance *this = impl_from_TP_CALLBACK_INSTANCE( instance );
    TRACE("%p %p %u\n", instance, semaphore, count);
    if (!this) return;
    if (this->cleanup.semaphore)
        FIXME("attempt to set multiple cleanup semaphores\n");
    else
    {
        this->cleanup.semaphore = semaphore;
        this->cleanup.semaphore_count = count;
    }
}

/***********************************************************************
 *           TpCallbackSetEventOnCompletion    (NTDLL.@)
 */
VOID WINAPI TpCallbackSetEventOnCompletion( TP_CALLBACK_INSTANCE *instance, HANDLE event )
{
    struct threadpool_instance *this = impl_from_TP_CALLBACK_INSTANCE( instance );
    TRACE("%p %p\n", instance, event);
    if (!this) return;
    if (this->cleanup.event)
        FIXME("attempt to set multiple cleanup events\n");
    else
        this->cleanup.event = event;
}

/***********************************************************************
 *           TpCallbackUnloadDllOnCompletion    (NTDLL.@)
 */
VOID WINAPI TpCallbackUnloadDllOnCompletion( TP_CALLBACK_INSTANCE *instance, HMODULE module )
{
    struct threadpool_instance *this = impl_from_TP_CALLBACK_INSTANCE( instance );
    TRACE("%p %p\n", instance, module);
    if (!this) return;
    if (this->cleanup.library)
        FIXME("attempt to set multiple cleanup libraries\n");
    else
        this->cleanup.library = module;
}

/***********************************************************************
 *           TpDisassociateCallback    (NTDLL.@)
 */
VOID WINAPI TpDisassociateCallback( TP_CALLBACK_INSTANCE *instance )
{
    struct threadpool_instance *this = impl_from_TP_CALLBACK_INSTANCE( instance );
    TRACE("%p\n", instance);
    if (this) tp_instance_disassociate_thread( this );
}

/***********************************************************************
 *           TpIsTimerSet    (NTDLL.@)
 */
BOOL WINAPI TpIsTimerSet( TP_TIMER *timer )
{
    struct threadpool_object *this = impl_from_TP_TIMER( timer );
    TRACE("%p\n", timer);
    return this ? this->u.timer.timer_set : FALSE;
}

/***********************************************************************
 *           TpPostWork    (NTDLL.@)
 */
VOID WINAPI TpPostWork( TP_WORK *work )
{
    struct threadpool_object *this = impl_from_TP_WORK( work );
    TRACE("%p\n", work);
    if (this) tp_object_submit( this );
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
    if (this) tp_group_release_members( this, cancel_pending, userdata );
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
 *           TpReleaseTimer     (NTDLL.@)
 */
VOID WINAPI TpReleaseTimer( TP_TIMER *timer )
{
    struct threadpool_object *this = impl_from_TP_TIMER( timer );
    TRACE("%p\n", timer);
    if (this)
    {
        tp_object_shutdown( this );
        tp_object_release( this );
    }
}

/***********************************************************************
 *           TpReleaseWork    (NTDLL.@)
 */
VOID WINAPI TpReleaseWork( TP_WORK *work )
{
    struct threadpool_object *this = impl_from_TP_WORK( work );
    TRACE("%p\n", work);
    if (this)
    {
        tp_object_shutdown( this );
        tp_object_release( this );
    }
}

/***********************************************************************
 *           TpSetPoolMaxThreads    (NTDLL.@)
 */
VOID WINAPI TpSetPoolMaxThreads( TP_POOL *pool, DWORD maximum )
{
    struct threadpool *this = impl_from_TP_POOL( pool );
    TRACE("%p %d\n", pool, maximum);
    if (this) this->max_workers = max(maximum, 1);
}

/***********************************************************************
 *           TpSetPoolMinThreads    (NTDLL.@)
 */
BOOL WINAPI TpSetPoolMinThreads( TP_POOL *pool, DWORD minimum )
{
    struct threadpool *this = impl_from_TP_POOL( pool );
    FIXME("%p %d: semi-stub\n", pool, minimum);
    if (this) this->min_workers = max(minimum, 1);
    return TRUE;
}

/***********************************************************************
 *           TpSetTimer    (NTDLL.@)
 */
VOID WINAPI TpSetTimer( TP_TIMER *timer, LARGE_INTEGER *timeout, LONG period, LONG window_length )
{
    struct threadpool_object *this = impl_from_TP_TIMER( timer );
    TRACE("%p %p %u %u\n", timer, timeout, period, window_length);
    if (this) tp_timerqueue_update_timer( this, timeout, period, window_length );
}

/***********************************************************************
 *           TpSimpleTryPost    (NTDLL.@)
 */
NTSTATUS WINAPI TpSimpleTryPost( PTP_SIMPLE_CALLBACK callback, PVOID userdata, TP_CALLBACK_ENVIRON *environment )
{
    TRACE("%p %p %p\n", callback, userdata, environment);
    return tp_object_submit_simple( callback, userdata, environment );
}

/***********************************************************************
 *           TpWaitForTimer    (NTDLL.@)
 */
VOID WINAPI TpWaitForTimer( TP_TIMER *timer, BOOL cancel_pending )
{
    struct threadpool_object *this = impl_from_TP_TIMER( timer );
    TRACE("%p %d\n", timer, cancel_pending);
    if (this)
    {
        if (cancel_pending)
            tp_object_cancel( this, FALSE, NULL );
        tp_object_wait( this );
    }
}

/***********************************************************************
 *           TpWaitForWork    (NTDLL.@)
 */
VOID WINAPI TpWaitForWork( TP_WORK *work, BOOL cancel_pending )
{
    struct threadpool_object *this = impl_from_TP_WORK( work );
    TRACE("%p %d\n", work, cancel_pending);
    if (this)
    {
        if (cancel_pending)
            tp_object_cancel( this, FALSE, NULL );
        tp_object_wait( this );
    }
}
