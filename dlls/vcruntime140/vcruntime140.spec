@ stub _CreateFrameInfo
@ stdcall _CxxThrowException(long long)
@ cdecl -arch=i386 -norelay _EH_prolog()
@ stub _FindAndUnlinkFrame
@ stub _IsExceptionObjectToBeDestroyed
@ stub _NLG_Dispatch2
@ stub _NLG_Return
@ stub _NLG_Return2
@ stub _SetWinRTOutOfMemoryExceptionCallback
@ cdecl __AdjustPointer(ptr ptr)
@ stub __BuildCatchObject
@ stub __BuildCatchObjectHelper
@ cdecl -arch=i386,x86_64,arm __CxxDetectRethrow(ptr)
@ stub __CxxExceptionFilter
@ cdecl -arch=i386,x86_64,arm -norelay __CxxFrameHandler(ptr ptr ptr ptr)
@ cdecl -arch=i386,x86_64,arm -norelay __CxxFrameHandler2(ptr ptr ptr ptr) __CxxFrameHandler
@ cdecl -arch=i386,x86_64,arm -norelay __CxxFrameHandler3(ptr ptr ptr ptr) __CxxFrameHandler
@ stdcall -arch=i386 __CxxLongjmpUnwind(ptr)
@ cdecl -arch=i386,x86_64,arm __CxxQueryExceptionSize()
@ stub __CxxRegisterExceptionObject
@ stub __CxxUnregisterExceptionObject
@ stub __DestructExceptionObject
@ stub __FrameUnwindFilter
@ stub __GetPlatformExceptionInfo
@ cdecl __RTCastToVoid(ptr) MSVCRT___RTCastToVoid
@ cdecl __RTDynamicCast(ptr long ptr ptr long) MSVCRT___RTDynamicCast
@ cdecl __RTtypeid(ptr) MSVCRT___RTtypeid
@ stub __TypeMatch
@ stub __current_exception
@ stub __current_exception_context
@ stub __intrinsic_setjmp
@ stub __processing_throw
@ stub __report_gsfailure
@ stub __std_exception_copy
@ stub __std_exception_destroy
@ stub __std_terminate
@ stub __std_type_info_compare
@ stub __std_type_info_destroy_list
@ stub __std_type_info_hash
@ stub __std_type_info_name
@ stub __telemetry_main_invoke_trigger
@ stub __telemetry_main_return_trigger
@ cdecl __unDName(ptr str long ptr ptr long)
@ cdecl __unDNameEx(ptr str long ptr ptr ptr long)
@ cdecl __uncaught_exception() MSVCRT___uncaught_exception
@ stub __uncaught_exceptions
@ stub __vcrt_GetModuleFileNameW
@ stub __vcrt_GetModuleHandleW
@ cdecl -arch=i386,win64 __vcrt_InitializeCriticalSectionEx(ptr long long) MSVCR110__crtInitializeCriticalSectionEx
@ stub __vcrt_LoadLibraryExW
@ cdecl -arch=i386 -norelay _chkesp()
@ cdecl -arch=i386 _except_handler2(ptr ptr ptr ptr)
@ cdecl -arch=i386 _except_handler3(ptr ptr ptr ptr)
@ cdecl -arch=i386 _except_handler4_common(ptr ptr ptr ptr ptr ptr)
@ stub _get_purecall_handler
@ cdecl _get_unexpected() MSVCRT__get_unexpected
@ cdecl -arch=i386 _global_unwind2(ptr)
@ stub _is_exception_typeof
@ cdecl -arch=i386 _local_unwind2(ptr long)
@ cdecl -arch=i386 _local_unwind4(ptr ptr long)
@ cdecl -arch=i386 _longjmpex(ptr long) MSVCRT_longjmp
@ cdecl _purecall()
@ stdcall -arch=i386 _seh_longjmp_unwind4(ptr)
@ stdcall -arch=i386 _seh_longjmp_unwind(ptr)
@ cdecl _set_purecall_handler(ptr)
@ stub -arch=win32 ?_set_se_translator@@YAP6AXIPAU_EXCEPTION_POINTERS@@@ZH@Z  # void(__cdecl*__cdecl _set_se_translator(int))(unsigned int,struct _EXCEPTION_POINTERS *)
@ stub -arch=win64 ?_set_se_translator@@YAP6AXIPEAU_EXCEPTION_POINTERS@@@ZH@Z  # void(__cdecl*__cdecl _set_se_translator(int))(unsigned int,struct _EXCEPTION_POINTERS * __ptr64)
@ cdecl -arch=i386 -norelay _setjmp3(ptr long) MSVCRT__setjmp3
@ cdecl -arch=i386,x86_64,arm longjmp(ptr long) MSVCRT_longjmp
@ cdecl memchr(ptr long long) MSVCRT_memchr
@ cdecl memcmp(ptr ptr long) MSVCRT_memcmp
@ cdecl memcpy(ptr ptr long) MSVCRT_memcpy
@ cdecl memmove(ptr ptr long) MSVCRT_memmove
@ cdecl memset(ptr long long) MSVCRT_memset
@ stub set_unexpected
@ cdecl strchr(str long) MSVCRT_strchr
@ cdecl strrchr(str long) MSVCRT_strrchr
@ cdecl strstr(str str) MSVCRT_strstr
@ stub unexpected
@ cdecl wcschr(wstr long) MSVCRT_wcschr
@ cdecl wcsrchr(wstr long) ntdll.wcsrchr
@ cdecl wcsstr(wstr wstr) MSVCRT_wcsstr
