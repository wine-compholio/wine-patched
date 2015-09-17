/*
 * AUTHZ Implementation
 *
 * Copyright 2009 Austin English
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
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(authz);

DECLARE_HANDLE(AUTHZ_ACCESS_CHECK_RESULTS_HANDLE);
DECLARE_HANDLE(AUTHZ_CLIENT_CONTEXT_HANDLE);
DECLARE_HANDLE(AUTHZ_RESOURCE_MANAGER_HANDLE);
DECLARE_HANDLE(AUTHZ_AUDIT_EVENT_HANDLE);

typedef AUTHZ_ACCESS_CHECK_RESULTS_HANDLE *PAUTHZ_ACCESS_CHECK_RESULTS_HANDLE;
typedef AUTHZ_CLIENT_CONTEXT_HANDLE *PAUTHZ_CLIENT_CONTEXT_HANDLE;
typedef struct _AUTHZ_ACCESS_REQUEST *PAUTHZ_ACCESS_REQUEST;
typedef struct _AUTHZ_ACCESS_REPLY *PAUTHZ_ACCESS_REPLY;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    TRACE("(0x%p, %d, %p)\n", hinstDLL, fdwReason, lpvReserved);

    switch (fdwReason)
    {
        case DLL_WINE_PREATTACH:
            return FALSE;    /* prefer native version */
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
            break;
        default:
            break;
    }

    return TRUE;
}

/***********************************************************************
 *              AuthzInitializeResourceManager (AUTHZ.@)
 */
BOOL WINAPI AuthzInitializeResourceManager(DWORD flags, LPVOID pfnAccessChecker,
    LPVOID pfnComputeDynGroups, LPVOID pfnFreeDynGroups,
    LPCWSTR managerName, LPVOID lpManagerHandle )
{
    FIXME("(0x%X,%p,%p,%p,%s,%p): stub\n", flags, pfnAccessChecker,
        pfnComputeDynGroups, pfnFreeDynGroups,
        debugstr_w(managerName), lpManagerHandle);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}


/***********************************************************************
 *              AuthzInstallSecurityEventSource (AUTHZ.@)
 */
BOOL WINAPI AuthzInstallSecurityEventSource(DWORD dwFlags, LPVOID pRegistration)
{
    FIXME("(0x%X,%p): stub\n", dwFlags, pRegistration);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}


/***********************************************************************
 *              AuthzAccessCheck (AUTHZ.@)
 */
BOOL WINAPI AuthzAccessCheck(DWORD flags, AUTHZ_CLIENT_CONTEXT_HANDLE client_context,
        PAUTHZ_ACCESS_REQUEST request, AUTHZ_AUDIT_EVENT_HANDLE audit_event,
        PSECURITY_DESCRIPTOR security, PSECURITY_DESCRIPTOR *optional_security,
        DWORD optional_security_count, PAUTHZ_ACCESS_REPLY reply,
        PAUTHZ_ACCESS_CHECK_RESULTS_HANDLE access_check_result)
{
    FIXME("(0x%x,%p,%p,%p,%p,%p,0x%x,%p,%p): stub\n", flags, client_context,
            request, audit_event, security, optional_security,
            optional_security_count, reply, access_check_result);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}


/***********************************************************************
 *              AuthzFreeContext (AUTHZ.@)
 */
BOOL WINAPI AuthzFreeContext(AUTHZ_CLIENT_CONTEXT_HANDLE client_context)
{
    FIXME("(%p): stub\n", client_context);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}


/***********************************************************************
 *              AuthzInitializeContextFromSid (AUTHZ.@)
 */
BOOL WINAPI AuthzInitializeContextFromSid(DWORD flags, PSID sid,
        AUTHZ_RESOURCE_MANAGER_HANDLE resource_manager, PLARGE_INTEGER expire_time,
        LUID id, PVOID dynamic_group, PAUTHZ_CLIENT_CONTEXT_HANDLE client_context)
{
    FIXME("(0x%x,%p,%p,%p,%08x:%08x,%p,%p): stub\n", flags, sid, resource_manager,
            expire_time, id.HighPart, id.LowPart, dynamic_group, client_context);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}


/***********************************************************************
 *              AuthzInitializeContextFromToken (AUTHZ.@)
 */
BOOL WINAPI AuthzInitializeContextFromToken(DWORD flags, HANDLE token_handle,
        AUTHZ_RESOURCE_MANAGER_HANDLE resource_manager, PLARGE_INTEGER expire_time,
        LUID id, PVOID dynamic_group, PAUTHZ_CLIENT_CONTEXT_HANDLE client_context)
{
    FIXME("(0x%x,%p,%p,%p,%08x:%08x,%p,%p): stub\n", flags, token_handle, resource_manager,
            expire_time, id.HighPart, id.LowPart, dynamic_group, client_context);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}
