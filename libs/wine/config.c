/*
 * Configuration parameters shared between Wine server and clients
 *
 * Copyright 2002 Alexandre Julliard
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

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#include "wine/library.h"

static const char server_config_dir[] = "/.wine";        /* config dir relative to $HOME */
static const char server_root_prefix[] = "/tmp/.wine";   /* prefix for server root dir */
static const char server_dir_prefix[] = "/server-";      /* prefix for server dir */

static char *bindir;
static char *dlldir;
static char *datadir;
static char *config_dir;
static char *server_dir;
static char *build_dir;
static char *user_name;
static char *argv0_name;

#ifdef __GNUC__
static void fatal_error( const char *err, ... )  __attribute__((noreturn,format(printf,1,2)));
static void fatal_perror( const char *err, ... )  __attribute__((noreturn,format(printf,1,2)));
#endif

#if defined(__linux__) || defined(__FreeBSD_kernel__ )
#define EXE_LINK "/proc/self/exe"
#elif defined (__FreeBSD__) || defined(__DragonFly__)
#define EXE_LINK "/proc/curproc/file"
#endif

/* die on a fatal error */
static void fatal_error( const char *err, ... )
{
    va_list args;

    va_start( args, err );
    fprintf( stderr, "wine: " );
    vfprintf( stderr, err, args );
    va_end( args );
    exit(1);
}

/* die on a fatal error */
static void fatal_perror( const char *err, ... )
{
    va_list args;

    va_start( args, err );
    fprintf( stderr, "wine: " );
    vfprintf( stderr, err, args );
    perror( " " );
    va_end( args );
    exit(1);
}

/* malloc wrapper */
static void *xmalloc( size_t size )
{
    void *res;

    if (!size) size = 1;
    if (!(res = malloc( size ))) fatal_error( "virtual memory exhausted\n");
    return res;
}

/* strdup wrapper */
static char *xstrdup( const char *str )
{
    size_t len = strlen(str) + 1;
    char *res = xmalloc( len );
    memcpy( res, str, len );
    return res;
}

/* check if a string ends in a given substring */
static inline int strendswith( const char* str, const char* end )
{
    size_t len = strlen( str );
    size_t tail = strlen( end );
    return len >= tail && !strcmp( str + len - tail, end );
}

/* remove all trailing slashes from a path name */
static inline void remove_trailing_slashes( char *path )
{
    int len = strlen( path );
    while (len > 1 && path[len-1] == '/') path[--len] = 0;
}

/* build a path from the specified dir and name */
static char *build_path( const char *dir, const char *name )
{
    size_t len = strlen(dir);
    char *ret = xmalloc( len + strlen(name) + 2 );

    memcpy( ret, dir, len );
    if (len && ret[len-1] != '/') ret[len++] = '/';
    strcpy( ret + len, name );
    return ret;
}

/* return the directory that contains the library at run-time */
static char *get_runtime_libdir(void)
{
#ifdef HAVE_DLADDR
    Dl_info info;
    char *libdir;

    if (dladdr( get_runtime_libdir, &info ) && info.dli_fname[0] == '/')
    {
        const char *p = strrchr( info.dli_fname, '/' );
        unsigned int len = p - info.dli_fname;
        if (!len) len++;  /* include initial slash */
        libdir = xmalloc( len + 1 );
        memcpy( libdir, info.dli_fname, len );
        libdir[len] = 0;
        return libdir;
    }
#endif /* HAVE_DLADDR */
    return NULL;
}

/* return the directory that contains the main exe at run-time */
static char *get_runtime_exedir(void)
{
#ifdef EXE_LINK
    char *p, *bindir;
    int size;

    for (size = 256; ; size *= 2)
    {
        int ret;
        if (!(bindir = malloc( size ))) return NULL;
        if ((ret = readlink( EXE_LINK, bindir, size )) == -1) break;
        if (ret != size)
        {
            bindir[ret] = 0;
            if (!(p = strrchr( bindir, '/' ))) break;
            if (p == bindir) p++;
            *p = 0;
            return bindir;
        }
        free( bindir );
    }
    free( bindir );
#endif
    return NULL;
}

/* return the base directory from argv0 */
static char *get_runtime_argvdir( const char *argv0 )
{
    char *p, *bindir, *cwd;
    int len, size;

    if (!(p = strrchr( argv0, '/' ))) return NULL;

    len = p - argv0;
    if (!len) len++;  /* include leading slash */

    if (argv0[0] == '/')  /* absolute path */
    {
        bindir = xmalloc( len + 1 );
        memcpy( bindir, argv0, len );
        bindir[len] = 0;
    }
    else
    {
        /* relative path, make it absolute */
        for (size = 256 + len; ; size *= 2)
        {
            if (!(cwd = malloc( size ))) return NULL;
            if (getcwd( cwd, size - len ))
            {
                bindir = cwd;
                cwd += strlen(cwd);
                *cwd++ = '/';
                memcpy( cwd, argv0, len );
                cwd[len] = 0;
                break;
            }
            free( cwd );
            if (errno != ERANGE) return NULL;
        }
    }
    return bindir;
}

/* initialize the server directory value */
static void init_server_dir( dev_t dev, ino_t ino )
{
    char *p, *root;

#ifdef __ANDROID__  /* there's no /tmp dir on Android */
    root = build_path( config_dir, ".wineserver" );
#elif defined(HAVE_GETUID)
    root = xmalloc( sizeof(server_root_prefix) + 12 );
    sprintf( root, "%s-%u", server_root_prefix, getuid() );
#else
    root = xstrdup( server_root_prefix );
#endif

    server_dir = xmalloc( strlen(root) + sizeof(server_dir_prefix) + 2*sizeof(dev) + 2*sizeof(ino) + 2 );
    strcpy( server_dir, root );
    strcat( server_dir, server_dir_prefix );
    p = server_dir + strlen(server_dir);

    if (dev != (unsigned long)dev)
        p += sprintf( p, "%lx%08lx-", (unsigned long)((unsigned long long)dev >> 32), (unsigned long)dev );
    else
        p += sprintf( p, "%lx-", (unsigned long)dev );

    if (ino != (unsigned long)ino)
        sprintf( p, "%lx%08lx", (unsigned long)((unsigned long long)ino >> 32), (unsigned long)ino );
    else
        sprintf( p, "%lx", (unsigned long)ino );
    free( root );
}

/* retrieve the default dll dir */
const char *get_dlldir( const char **default_dlldir )
{
    *default_dlldir = DLLDIR;
    return dlldir;
}

/* initialize all the paths values */
static void init_paths(void)
{
    struct stat st;

    const char *home = getenv( "HOME" );
    const char *user = NULL;
    const char *prefix = getenv( "WINEPREFIX" );

#ifdef HAVE_GETPWUID
    char uid_str[32];
    struct passwd *pwd = getpwuid( getuid() );

    if (pwd)
    {
        user = pwd->pw_name;
        if (!home) home = pwd->pw_dir;
    }
    if (!user)
    {
        sprintf( uid_str, "%lu", (unsigned long)getuid() );
        user = uid_str;
    }
#else  /* HAVE_GETPWUID */
    if (!(user = getenv( "USER" )))
        fatal_error( "cannot determine your user name, set the USER environment variable\n" );
#endif  /* HAVE_GETPWUID */
    user_name = xstrdup( user );

    /* build config_dir */

    if (prefix)
    {
        config_dir = xstrdup( prefix );
        remove_trailing_slashes( config_dir );
        if (config_dir[0] != '/')
            fatal_error( "invalid directory %s in WINEPREFIX: not an absolute path\n", prefix );
        if (stat( config_dir, &st ) == -1)
        {
            if (errno == ENOENT) return;  /* will be created later on */
            fatal_perror( "cannot open %s as specified in WINEPREFIX", config_dir );
        }
    }
    else
    {
        if (!home) fatal_error( "could not determine your home directory\n" );
        if (home[0] != '/') fatal_error( "your home directory %s is not an absolute path\n", home );
        config_dir = xmalloc( strlen(home) + sizeof(server_config_dir) );
        strcpy( config_dir, home );
        remove_trailing_slashes( config_dir );
        strcat( config_dir, server_config_dir );
        if (stat( config_dir, &st ) == -1)
        {
            if (errno == ENOENT) return;  /* will be created later on */
            fatal_perror( "cannot open %s", config_dir );
        }
    }
    if (!S_ISDIR(st.st_mode)) fatal_error( "%s is not a directory\n", config_dir );
#ifdef HAVE_GETUID
    if (st.st_uid != getuid()) fatal_error( "%s is not owned by you\n", config_dir );
#endif

    init_server_dir( st.st_dev, st.st_ino );
}

/* check if bindir is valid by checking for wineserver */
static int is_valid_bindir( const char *bindir )
{
    struct stat st;
    char *path = build_path( bindir, "wineserver" );
    int ret = (stat( path, &st ) != -1);
    free( path );
    return ret;
}

/* check if basedir is a valid build dir by checking for wineserver and ntdll */
/* helper for running_from_build_dir */
static inline int is_valid_build_dir( char *basedir, int baselen )
{
    struct stat st;

    strcpy( basedir + baselen, "/server/wineserver" );
    if (stat( basedir, &st ) == -1) return 0;  /* no wineserver found */
    /* check for ntdll too to make sure */
    strcpy( basedir + baselen, "/dlls/ntdll/ntdll.dll.so" );
    if (stat( basedir, &st ) == -1) return 0;  /* no ntdll found */

    basedir[baselen] = 0;
    return 1;
}

/* check if we are running from the build directory */
static char *running_from_build_dir( const char *basedir )
{
    const char *p;
    char *path;

    /* remove last component from basedir */
    p = basedir + strlen(basedir) - 1;
    while (p > basedir && *p == '/') p--;
    while (p > basedir && *p != '/') p--;
    if (p == basedir) return NULL;
    path = xmalloc( p - basedir + sizeof("/dlls/ntdll/ntdll.dll.so") );
    memcpy( path, basedir, p - basedir );

    if (!is_valid_build_dir( path, p - basedir ))
    {
        /* remove another component */
        while (p > basedir && *p == '/') p--;
        while (p > basedir && *p != '/') p--;
        if (p == basedir || !is_valid_build_dir( path, p - basedir ))
        {
            free( path );
            return NULL;
        }
    }
    return path;
}

/* initialize the argv0 path */
void wine_init_argv0_path( const char *argv0 )
{
    const char *basename;
    char *libdir;

    if (!(basename = strrchr( argv0, '/' ))) basename = argv0;
    else basename++;

    bindir = get_runtime_exedir();
    if (bindir && !is_valid_bindir( bindir ))
    {
        build_dir = running_from_build_dir( bindir );
        free( bindir );
        bindir = NULL;
    }

    libdir = get_runtime_libdir();
    if (libdir && !bindir && !build_dir)
    {
        build_dir = running_from_build_dir( libdir );
        if (!build_dir) bindir = build_path( libdir, LIB_TO_BINDIR );
    }

    if (!libdir && !bindir && !build_dir)
    {
        bindir = get_runtime_argvdir( argv0 );
        if (bindir && !is_valid_bindir( bindir ))
        {
            build_dir = running_from_build_dir( bindir );
            free( bindir );
            bindir = NULL;
        }
    }

    if (build_dir)
    {
        argv0_name = build_path( "loader/", basename );
    }
    else
    {
        if (libdir) dlldir = build_path( libdir, LIB_TO_DLLDIR );
        else if (bindir) dlldir = build_path( bindir, BIN_TO_DLLDIR );

        if (bindir) datadir = build_path( bindir, BIN_TO_DATADIR );
        argv0_name = xstrdup( basename );
    }
    free( libdir );
}

/* return the configuration directory ($WINEPREFIX or $HOME/.wine) */
const char *wine_get_config_dir(void)
{
    if (!config_dir) init_paths();
    return config_dir;
}

/* retrieve the wine data dir */
const char *wine_get_data_dir(void)
{
    return datadir;
}

/* retrieve the wine build dir (if we are running from there) */
const char *wine_get_build_dir(void)
{
    return build_dir;
}

const char *wine_libs[] = {
#ifdef SONAME_LIBCAPI20
    SONAME_LIBCAPI20,
#endif
#ifdef SONAME_LIBCUPS
    SONAME_LIBCUPS,
#endif
#ifdef SONAME_LIBCURSES
    SONAME_LIBCURSES,
#endif
#ifdef SONAME_LIBDBUS_1
    SONAME_LIBDBUS_1,
#endif
#ifdef SONAME_LIBFONTCONFIG
    SONAME_LIBFONTCONFIG,
#endif
#ifdef SONAME_LIBFREETYPE
    SONAME_LIBFREETYPE,
#endif
#ifdef SONAME_LIBGL
    SONAME_LIBGL,
#endif
#ifdef SONAME_LIBGNUTLS
    SONAME_LIBGNUTLS,
#endif
#ifdef SONAME_LIBGSM
    SONAME_LIBGSM,
#endif
#ifdef SONAME_LIBHAL
    SONAME_LIBHAL,
#endif
#ifdef SONAME_LIBJPEG
    SONAME_LIBJPEG,
#endif
#ifdef SONAME_LIBNCURSES
    SONAME_LIBNCURSES,
#endif
#ifdef SONAME_LIBNETAPI
    SONAME_LIBNETAPI,
#endif
#ifdef SONAME_LIBODBC
    SONAME_LIBODBC,
#endif
#ifdef SONAME_LIBOSMESA
    SONAME_LIBOSMESA,
#endif
#ifdef SONAME_LIBPCAP
    SONAME_LIBPCAP,
#endif
#ifdef SONAME_LIBPNG
    SONAME_LIBPNG,
#endif
#ifdef SONAME_LIBSANE
    SONAME_LIBSANE,
#endif
#ifdef SONAME_LIBTIFF
    SONAME_LIBTIFF,
#endif
#ifdef SONAME_LIBTXC_DXTN
    SONAME_LIBTXC_DXTN,
#endif
#ifdef SONAME_LIBV4L1
    SONAME_LIBV4L1,
#endif
#ifdef SONAME_LIBVA
    SONAME_LIBVA,
#endif
#ifdef SONAME_LIBVA_DRM
    SONAME_LIBVA_DRM,
#endif
#ifdef SONAME_LIBVA_X11
    SONAME_LIBVA_X11,
#endif
#ifdef SONAME_LIBX11
    SONAME_LIBX11,
#endif
#ifdef SONAME_LIBXCOMPOSITE
    SONAME_LIBXCOMPOSITE,
#endif
#ifdef SONAME_LIBXCURSOR
    SONAME_LIBXCURSOR,
#endif
#ifdef SONAME_LIBXEXT
    SONAME_LIBXEXT,
#endif
#ifdef SONAME_LIBXI
    SONAME_LIBXI,
#endif
#ifdef SONAME_LIBXINERAMA
    SONAME_LIBXINERAMA,
#endif
#ifdef SONAME_LIBXRANDR
    SONAME_LIBXRANDR,
#endif
#ifdef SONAME_LIBXRENDER
    SONAME_LIBXRENDER,
#endif
#ifdef SONAME_LIBXSLT
    SONAME_LIBXSLT,
#endif
#ifdef SONAME_LIBXXF86VM
    SONAME_LIBXXF86VM,
#endif
    NULL
};

/* return the list of shared libs used by wine */
const char **wine_get_libs(void)
{
    return &wine_libs[0];
}

/* return the full name of the server directory (the one containing the socket) */
const char *wine_get_server_dir(void)
{
    if (!server_dir)
    {
        if (!config_dir) init_paths();
        else
        {
            struct stat st;

            if (stat( config_dir, &st ) == -1)
            {
                if (errno != ENOENT) fatal_error( "cannot stat %s\n", config_dir );
                return NULL;  /* will have to try again once config_dir has been created */
            }
            init_server_dir( st.st_dev, st.st_ino );
        }
    }
    return server_dir;
}

/* return the current user name */
const char *wine_get_user_name(void)
{
    if (!user_name) init_paths();
    return user_name;
}

/* return the standard version string */
const char *wine_get_version(void)
{
    return PACKAGE_VERSION;
}

static const struct
{
    const char *author;
    const char *subject;
    int revision;
}
wine_patch_data[] =
{
    { "Alex Henrie", "kernel32: Set error if dstlen < 0 in codepage conversion functions.", 1 },
    { "Alex Henrie", "ntdll: Do a device check before returning a default serial port name.", 1 },
    { "Alexander E. Patrakov", "dsound: Add a linear resampler for use with a large number of mixing buffers.", 2 },
    { "Alexander Morozov", "ntoskrnl.exe: Add stub for IoGetAttachedDeviceReference.", 1 },
    { "Alexander Morozov", "ntoskrnl.exe: Add stub for KeDelayExecutionThread.", 1 },
    { "Alexander Morozov", "ntoskrnl.exe: Add stubs for ExAcquireFastMutexUnsafe and ExReleaseFastMutexUnsafe.", 1 },
    { "Alexander Morozov", "ntoskrnl.exe: Add stubs for ObReferenceObjectByPointer and ObDereferenceObject.", 1 },
    { "Alexander Morozov", "ntoskrnl.exe: Implement KeInitializeMutex.", 1 },
    { "Alexander Morozov", "ntoskrnl.exe: Improve KeInitializeSemaphore stub.", 1 },
    { "Alexander Morozov", "ntoskrnl.exe: Improve KeInitializeTimerEx stub.", 1 },
    { "Alexander Morozov", "ntoskrnl.exe: Improve KeReleaseMutex stub.", 1 },
    { "Alistair Leslie-Hughes", "comctl32/tooltip: Protect TTM_ADDTOOLW from invalid text pointers.", 1 },
    { "Alistair Leslie-Hughes", "d3dx9_33: Share the source with d3dx9_36.", 1 },
    { "Alistair Leslie-Hughes", "d3dx9_36: ID3DXFont_DrawText calc_rect can be null.", 1 },
    { "Alistair Leslie-Hughes", "d3dx9_36: Implement D3DXGetShaderOutputSemantics.", 2 },
    { "Alistair Leslie-Hughes", "d3dx9_36: Return a mesh in D3DXCreateTeapot.", 1 },
    { "Alistair Leslie-Hughes", "d3dx9_36: Support NULL terminated strings in ID3DXFont_DrawText.", 1 },
    { "Andrey Gusev", "d3dx9_36: Add D3DXFrameFind stub.", 1 },
    { "André Hentschel", "sfnt2fon: Don't leak output name if specified multiple times (Coverity).", 1 },
    { "André Hentschel", "winedump: Free debug string in case it was not freed in for-loop (Coverity).", 1 },
    { "André Hentschel", "wpcap: Load libpcap dynamically.", 1 },
    { "Anton Baskanov", "user32: Set correct caret state in the server in SetCaretPos.", 5 },
    { "Aric Stewart", "hid: Stub HidP_TranslateUsagesToI8042ScanCodes.", 1 },
    { "Austin English", "kernel32: Add FreeUserPhysicalPages stub.", 2 },
    { "Austin English", "ntdll: Add NtSetLdtEntries/ZwSetLdtEntries stub.", 2 },
    { "Austin English", "ntoskrnl.exe: Add KeWaitForMultipleObjects stub.", 1 },
    { "Austin English", "setupapi: Add SetupDiSetDeviceInstallParamsW stub.", 1 },
    { "Austin English", "user32: Add SetCoalescableTimer stub.", 1 },
    { "Austin English", "wined3d: Allow to specify multisampling AA quality levels via registry.", 1 },
    { "Austin English", "wininet: Add ParseX509EncodedCertificateForListBoxEntry stub.", 2 },
    { "Austin English", "winscard: Add stubs for SCardListReadersA/W.", 1 },
    { "Austin English", "winsta: Add stub for WinStationEnumerateW.", 2 },
    { "Bernhard Reiter", "imagehlp: Implement parts of BindImageEx to make freezing Python scripts work.", 1 },
    { "Bernhard Übelacker", "dinput: Implement device property DIPROP_USERNAME.", 1 },
    { "Bernhard Übelacker", "msvcr120: Implement strtof and _strtof_l.", 3 },
    { "Bruno Jesus", "shlwapi/tests: Test NULL handle duplication in SHMapHandle().", 1 },
    { "Bruno Jesus", "ws2_32: Ensure default route IP addresses are returned first in gethostbyname.", 1 },
    { "Charles Davis", "crypt32: Skip unknown item when decoding a CMS certificate.", 1 },
    { "Christian Costa", "crypt32: Print CryptUnprotectMemory FIXME only once.", 1 },
    { "Christian Costa", "d3d9/tests: Avoid crash when surface and texture creation fails.", 1 },
    { "Christian Costa", "d3dx9_36: Add dxtn support.", 1 },
    { "Christian Costa", "d3dx9_36: Add stub for D3DXComputeNormalMap.", 1 },
    { "Christian Costa", "d3dx9_36: Add support for FOURCC surface to save_dds_surface_to_memory.", 1 },
    { "Christian Costa", "d3dx9_36: Align texture dimensions to block size for compressed textures in D3DXCheckTextureRequirements.", 1 },
    { "Christian Costa", "d3dx9_36: Filter out D3DCompile warning messages that are not present with D3DCompileShader.", 4 },
    { "Christian Costa", "d3dx9_36: Fix horizontal centering in ID3DXFont_DrawText.", 1 },
    { "Christian Costa", "d3dx9_36: Implement D3DXGetShaderInputSemantics + tests.", 3 },
    { "Christian Costa", "d3dx9_36: Implement ID3DXEffect_FindNextValidTechnique + add tests.", 1 },
    { "Christian Costa", "d3dx9_36: Implement ID3DXFontImpl_DrawText.", 1 },
    { "Christian Costa", "d3dx9_36: Implement ID3DXSkinInfoImpl_UpdateSkinnedMesh.", 1 },
    { "Christian Costa", "d3dx9_36: Improve D3DXSaveTextureToFile to save simple texture to dds file.", 1 },
    { "Christian Costa", "d3dx9_36: No need to fail if we don't support vertices reordering in D3DXMESHOPT_ATTRSORT.", 1 },
    { "Christian Costa", "ddraw: Don't call IDirect3DDevice7_DrawIndexedPrimitive if there is no primitive.", 1 },
    { "Christian Costa", "mmdevapi: Improve AEV_GetVolumeRange stub.", 1 },
    { "Christian Costa", "mmdevapi: Improve AEV_SetMasterVolumeLevel and AEV_GetMasterVolumeLevel stubs.", 1 },
    { "Christian Costa", "mmdevapi: Improve AEV_SetMute and AEV_GetMute stubs.", 1 },
    { "Christian Costa", "ntdll: Add dll override default rule for purist mode.", 1 },
    { "Christian Costa", "ntoskrnl.exe: Fix IoReleaseCancelSpinLock argument.", 1 },
    { "Christian Costa", "ntoskrnl.exe: Implement MmMapLockedPages and MmUnmapLockedPages.", 1 },
    { "Christian Costa", "shdocvw: Check precisely ParseURLFromOutsideSourceX returned values in tests and make code clearer about that.", 3 },
    { "Christian Costa", "shell32: Implement FolderImpl_Items and stubbed FolderItems interface.", 1 },
    { "Christian Costa", "wined3d: Improve DXTn support and export conversion functions for d3dx9_36.", 1 },
    { "Christian Costa", "wined3d: Print FIXME only once in surface_cpu_blt.", 1 },
    { "Christopher Thielen", "user32: Also send WM_CAPTURECHANGE when capture has not changed.", 1 },
    { "Claudio Fontana", "kernel32: Allow empty profile section and key name strings.", 1 },
    { "Damjan Jovanovic", "winex11.drv: Import X11's \"text/html\" as \"HTML Format\".", 3 },
    { "Dan Kegel", "kernel32: ConnectNamedPort should return FALSE and set ERROR_PIPE_CONNECTED on success in overlapped mode.", 1 },
    { "Daniel Jelinski", "wine.inf: Add registry keys for Windows Performance Library.", 1 },
    { "David Woodhouse", "secur32: Fix handling of ANSI NTLM credentials.", 1 },
    { "Dmitry Timoshkov", "gdi32: Improve detection of symbol charset for old truetype fonts.", 1 },
    { "Dmitry Timoshkov", "include: Make stdole32.idl a public component.", 1 },
    { "Dmitry Timoshkov", "kernel32/tests: Add tests for NtQuerySection.", 2 },
    { "Dmitry Timoshkov", "kernel32: CompareStringW should abort on the first nonmatching character to avoid invalid memory access.", 2 },
    { "Dmitry Timoshkov", "libs: Fix most problems with CompareString.", 1 },
    { "Dmitry Timoshkov", "ntdll: Avoid race-conditions with write watches in NtReadFile.", 1 },
    { "Dmitry Timoshkov", "ntdll: Implement NtQuerySection.", 2 },
    { "Dmitry Timoshkov", "oleaut32: Fix logic for deciding whether type description follows the name.", 1 },
    { "Dmitry Timoshkov", "olepro32: Add typelib resource.", 1 },
    { "Dmitry Timoshkov", "shell32: Implement SHCreateSessionKey.", 1 },
    { "Dmitry Timoshkov", "user32: Change value for WM_MDICALCCHILDSCROLL to 0x003f.", 1 },
    { "Dmitry Timoshkov", "user32: Fix return value of ScrollWindowEx for invisible windows.", 1 },
    { "Dmitry Timoshkov", "user32: Try harder to find a target for mouse messages.", 1 },
    { "Dmitry Timoshkov", "widl: Add initial implementation of SLTG typelib generator.", 1 },
    { "Dmitry Timoshkov", "widl: Add support for VT_USERDEFINED to SLTG typelib generator.", 1 },
    { "Dmitry Timoshkov", "widl: Add support for VT_VOID and VT_VARIANT to SLTG typelib generator.", 1 },
    { "Dmitry Timoshkov", "widl: Add support for inherited interfaces to SLTG typelib generator.", 1 },
    { "Dmitry Timoshkov", "widl: Add support for interfaces to SLTG typelib generator.", 1 },
    { "Dmitry Timoshkov", "widl: Add support for recursive type references to SLTG typelib generator.", 1 },
    { "Dmitry Timoshkov", "widl: Add support for structures.", 1 },
    { "Dmitry Timoshkov", "widl: Avoid relying on side effects when marking function index as the last one.", 1 },
    { "Dmitry Timoshkov", "widl: Calculate size of instance for structures.", 1 },
    { "Dmitry Timoshkov", "widl: Create library block index right after the CompObj one.", 1 },
    { "Dmitry Timoshkov", "widl: Factor out SLTG tail initialization.", 1 },
    { "Dmitry Timoshkov", "widl: Fix generation of resources containing an old typelib.", 1 },
    { "Dmitry Timoshkov", "widl: Make automatic dispid generation scheme better match what midl does.", 1 },
    { "Dmitry Timoshkov", "widl: More accurately report variable descriptions data size.", 1 },
    { "Dmitry Timoshkov", "widl: Properly align name table entries.", 1 },
    { "Dmitry Timoshkov", "widl: Write SLTG blocks according to the index order.", 1 },
    { "Dmitry Timoshkov", "widl: Write correct syskind by SLTG typelib generator.", 1 },
    { "Dmitry Timoshkov", "widl: Write correct typekind to the SLTG typeinfo block.", 1 },
    { "Dmitry Timoshkov", "winex11: Fix handling of window attributes for WS_EX_LAYERED | WS_EX_COMPOSITED.", 1 },
    { "Erich E. Hoover", "Appease the blessed version of gcc (4.5) when -Werror is enabled.", 1 },
    { "Erich E. Hoover", "advapi32: Fix the initialization of combined DACLs when the new DACL is empty.", 1 },
    { "Erich E. Hoover", "advapi32: Move the DACL combining code into a separate routine.", 1 },
    { "Erich E. Hoover", "dsound: Add stub support for DSPROPSETID_EAX20_BufferProperties.", 1 },
    { "Erich E. Hoover", "dsound: Add stub support for DSPROPSETID_EAX20_ListenerProperties.", 1 },
    { "Erich E. Hoover", "fonts: Add WenQuanYi Micro Hei as a Microsoft Yahei replacement.", 1 },
    { "Erich E. Hoover", "iphlpapi: Implement AllocateAndGetTcpExTableFromStack.", 1 },
    { "Erich E. Hoover", "kernel32,ntdll: Add support for deleting junction points with RemoveDirectory.", 1 },
    { "Erich E. Hoover", "kernel32: Add SearchPath test demonstrating the priority of the working directory.", 1 },
    { "Erich E. Hoover", "kernel32: Advertise junction point support.", 1 },
    { "Erich E. Hoover", "kernel32: Consider the working directory first when launching executables with CreateProcess.", 1 },
    { "Erich E. Hoover", "kernel32: NeedCurrentDirectoryForExePath does not use the registry.", 1 },
    { "Erich E. Hoover", "libport: Add support for FreeBSD style extended attributes.", 1 },
    { "Erich E. Hoover", "libport: Add support for Mac OS X style extended attributes.", 1 },
    { "Erich E. Hoover", "msi: Add support for deleting streams from an MSI database.", 1 },
    { "Erich E. Hoover", "msi: Add support for exporting binary streams (Binary/Icon tables).", 1 },
    { "Erich E. Hoover", "msi: Add support for exporting the _SummaryInformation table.", 1 },
    { "Erich E. Hoover", "msi: Break out field exporting into a separate routine.", 1 },
    { "Erich E. Hoover", "msi: Return an error when MsiDatabaseImport is passed an invalid pathname.", 1 },
    { "Erich E. Hoover", "msidb: Add stub tool for manipulating MSI databases.", 1 },
    { "Erich E. Hoover", "msidb: Add support for adding stream/cabinet files to MSI databases.", 1 },
    { "Erich E. Hoover", "msidb: Add support for exporting database tables.", 1 },
    { "Erich E. Hoover", "msidb: Add support for exporting with short (DOS) filenames.", 1 },
    { "Erich E. Hoover", "msidb: Add support for extracting stream/cabinet files from MSI databases.", 1 },
    { "Erich E. Hoover", "msidb: Add support for importing database tables.", 1 },
    { "Erich E. Hoover", "msidb: Add support for removing stream/cabinet files from MSI databases.", 1 },
    { "Erich E. Hoover", "msidb: Add support for wildcard (full database) export.", 1 },
    { "Erich E. Hoover", "ntdll/tests: Add test for deleting junction point target.", 1 },
    { "Erich E. Hoover", "ntdll: Add a test for junction point advertisement.", 1 },
    { "Erich E. Hoover", "ntdll: Add stubs for WinSqmStartSession / WinSqmEndSession.", 1 },
    { "Erich E. Hoover", "ntdll: Add support for deleting junction points.", 1 },
    { "Erich E. Hoover", "ntdll: Add support for junction point creation.", 1 },
    { "Erich E. Hoover", "ntdll: Add support for reading junction points.", 1 },
    { "Erich E. Hoover", "ntdll: Implement retrieving DOS attributes in NtQueryInformationFile.", 1 },
    { "Erich E. Hoover", "ntdll: Implement retrieving DOS attributes in NtQuery[Full]AttributesFile and NtQueryDirectoryFile.", 1 },
    { "Erich E. Hoover", "ntdll: Implement storing DOS attributes in NtCreateFile.", 1 },
    { "Erich E. Hoover", "ntdll: Implement storing DOS attributes in NtSetInformationFile.", 1 },
    { "Erich E. Hoover", "ntdll: Perform the Unix-style hidden file check within the unified file info grabbing routine.", 1 },
    { "Erich E. Hoover", "quartz: Implement MediaSeeking_GetCurrentPosition on top of MediaSeeking_GetPositions.", 1 },
    { "Erich E. Hoover", "quartz: Implement MediaSeeking_GetStopPosition on top of MediaSeeking_GetPositions.", 1 },
    { "Erich E. Hoover", "quartz: Include the stream position in addition to the reference clock offset in the time returned by MediaSeeking_GetPositions.", 1 },
    { "Erich E. Hoover", "quartz: Remove unused cache of MediaSeeking stop position.", 1 },
    { "Erich E. Hoover", "server: Add default security descriptor DACL for processes.", 1 },
    { "Erich E. Hoover", "server: Add default security descriptor ownership for processes.", 1 },
    { "Erich E. Hoover", "server: Convert return of file security masks with generic access mappings.", 7 },
    { "Erich E. Hoover", "server: Inherit security attributes from parent directories on creation.", 7 },
    { "Erich E. Hoover", "server: Retrieve file security attributes with extended file attributes.", 7 },
    { "Erich E. Hoover", "server: Store file security attributes with extended file attributes.", 8 },
    { "Erich E. Hoover", "server: Unify the retrieval of security attributes for files and directories.", 7 },
    { "Erich E. Hoover", "server: Unify the storage of security attributes for files and directories.", 7 },
    { "Erich E. Hoover", "strmbase: Fix MediaSeekingPassThru_GetPositions return when the pins are unconnected.", 1 },
    { "Erich E. Hoover", "wined3d: Silence repeated resource_check_usage FIXME.", 2 },
    { "Erich E. Hoover", "ws2_32: Add support for TF_DISCONNECT to TransmitFile.", 1 },
    { "Erich E. Hoover", "ws2_32: Add support for TF_REUSE_SOCKET to TransmitFile.", 1 },
    { "Felix Yan", "winex11.drv: Update a candidate window's position with over-the-spot style.", 2 },
    { "Hao Peng", "winecfg: Double click in dlls list to edit item's overides.", 3 },
    { "Henri Verbeet", "d3d9: Don't decrease surface refcount when its already zero.", 1 },
    { "Henri Verbeet", "wined3d: Wait for resource updates to finish when using the multithreaded command stream.", 1 },
    { "Ivan Akulinchev", "uxthemegtk: Initial implementation.", 1 },
    { "Jacek Caban", "mshtml: Wine Gecko 2.44 release.", 1 },
    { "Jactry Zeng", "riched20: Fix ME_RunOfsFromCharOfs() when nCharOfs > strlen().", 1 },
    { "Jactry Zeng", "riched20: Implement ITextRange::GetStoryLength.", 1 },
    { "Jactry Zeng", "riched20: Implement ITextRange::GetText.", 1 },
    { "Jactry Zeng", "riched20: Implement ITextRange::IsEqual.", 1 },
    { "Jactry Zeng", "riched20: Implement ITextRange::SetRange.", 1 },
    { "Jactry Zeng", "riched20: Implement ITextSelection::GetStoryLength.", 1 },
    { "Jactry Zeng", "riched20: Implement IText{Selection, Range}::Set{Start, End}.", 1 },
    { "Jactry Zeng", "riched20: Stub for ITextFont interface and implement ITextRange::GetFont and ITextSelection::GetFont.", 1 },
    { "Jactry Zeng", "riched20: Stub for ITextPara interface and implement ITextRange::GetPara.", 1 },
    { "Jared Smudde", "inetcpl: Implement default page button.", 1 },
    { "Jared Smudde", "shell32: Add caption to Run dialog.", 1 },
    { "Jarkko Korpi", "kernel32: Silence repeated LocaleNameToLCID unsupported flags message.", 1 },
    { "Jarkko Korpi", "wined3d: Add detection for NVIDIA GeForce 425M.", 1 },
    { "Jarkko Korpi", "winhttp: Silence repeated \"no support on this platform\" message.", 1 },
    { "Jarkko Korpi", "wininet: Silence wininet no support on this platform message.", 1 },
    { "Jarkko Korpi", "winspool.drv Add case 8 for SetPrinterW.", 1 },
    { "Jianqiu Zhang", "ntdll: Add support for FileFsFullSizeInformation class in NtQueryVolumeInformationFile.", 2 },
    { "Jianqiu Zhang", "wpcap: Fix crash on pcap_loop.", 1 },
    { "Jianqiu Zhang", "wpcap: Implement pcap_dump_open and pcap_dump.", 1 },
    { "Joakim Hernberg", "wineserver: Draft to implement priority levels through POSIX scheduling policies on linux.", 1 },
    { "Joris van der Wel", "advapi32/tests: Add additional tests for passing a thread sd to CreateProcess.", 1 },
    { "Józef Kucia", "wined3d: Ignore invalid render states.", 1 },
    { "Ken Thomases", "Revert \"wined3d: Don't call GetPixelFormat() to set a flag that's already set.\".", 1 },
    { "Ken Thomases", "Revert \"wined3d: Restore the pixel format of the window whose pixel format was actually changed.\".", 1 },
    { "Ken Thomases", "Revert \"wined3d: Track if a context's hdc is private so we never need to restore its pixel format.\".", 1 },
    { "Ken Thomases", "Revert \"wined3d: Track if a context's private hdc has had its pixel format set, so we don't need to check it.\".", 1 },
    { "Ken Thomases", "Revert \"wined3d: When restoring pixel format in context_release(), mark the context as needing to be set on the next context_acquire().\".", 1 },
    { "Ken Thomases", "d3d8: Mark tests which no longer pass due to reverts as todo_wine.", 1 },
    { "Ken Thomases", "d3d9: Mark tests which no longer pass due to reverts as todo_wine.", 1 },
    { "Ken Thomases", "ddraw: Mark tests which no longer pass due to reverts as todo_wine.", 1 },
    { "Ken Thomases", "gdi32: Also accept \"\\\\\\\\.\\\\DISPLAY<n>\" devices names with <n> other than 1 as display devices.", 1 },
    { "Ken Thomases", "user32: Implement EnumDisplayDevicesW() based on EnumDisplayMonitors() and GetMonitorInfoW().", 1 },
    { "Ken Thomases", "winemac: Make GetMonitorInfo() give a different device name (\\\\\\\\.\\\\DISPLAY<n>) to each monitor.", 1 },
    { "Ken Thomases", "winex11: Make GetMonitorInfo() give a different device name (\\\\.\\\\DISPLAY<n>) to each monitor.", 1 },
    { "Kira Backes", "user32: Add MOUSEHOOKSTRUCTEX to fix mouse wheel support for JA2 1.13 and other apps which use it.", 1 },
    { "Mark Harmstone", "dsound: Add EAX VerbPass stub.", 1 },
    { "Mark Harmstone", "dsound: Add EAX init and free stubs.", 1 },
    { "Mark Harmstone", "dsound: Add EAX presets.", 1 },
    { "Mark Harmstone", "dsound: Add EAX propset stubs.", 1 },
    { "Mark Harmstone", "dsound: Add EAX v1 constants and structs.", 1 },
    { "Mark Harmstone", "dsound: Add delay line EAX functions.", 1 },
    { "Mark Harmstone", "dsound: Allocate EAX delay lines.", 1 },
    { "Mark Harmstone", "dsound: Feed data through EAX function.", 1 },
    { "Mark Harmstone", "dsound: Implement EAX decorrelator.", 1 },
    { "Mark Harmstone", "dsound: Implement EAX early reflections.", 1 },
    { "Mark Harmstone", "dsound: Implement EAX late all-pass filter.", 1 },
    { "Mark Harmstone", "dsound: Implement EAX late reverb.", 1 },
    { "Mark Harmstone", "dsound: Implement EAX lowpass filter.", 1 },
    { "Mark Harmstone", "dsound: Report that we support EAX.", 1 },
    { "Mark Harmstone", "dsound: Support getting and setting EAX buffer properties.", 1 },
    { "Mark Harmstone", "dsound: Support getting and setting EAX properties.", 1 },
    { "Mark Harmstone", "winecfg: Add checkbox to enable/disable EAX support.", 1 },
    { "Mark Harmstone", "winepulse: Expose audio devices directly to programs.", 1 },
    { "Mark Harmstone", "winepulse: Fetch actual program name if possible.", 1 },
    { "Mark Harmstone", "winepulse: Fix segfault in pulse_rd_loop.", 1 },
    { "Mark Harmstone", "winepulse: Implement GetPropValue.", 1 },
    { "Mark Harmstone", "winepulse: Implement exclusive mode.", 1 },
    { "Mark Harmstone", "winepulse: Return PKEY_AudioEndpoint_PhysicalSpeakers device prop.", 1 },
    { "Mark Jansen", "imagehlp/tests: Add tests for ImageLoad, ImageUnload, GetImageUnusedHeaderBytes.", 1 },
    { "Mark Jansen", "imagehlp/tests: Msvc compatibility fixes.", 1 },
    { "Mark Jansen", "ntdll/tests: Add tests for RtlIpv6AddressToString and RtlIpv6AddressToStringEx.", 1 },
    { "Mark Jansen", "ntdll/tests: Tests for RtlIpv4StringToAddressEx (try 5, resend).", 1 },
    { "Mark Jansen", "ntdll/tests: Tests for RtlIpv6StringToAddress.", 6 },
    { "Mark Jansen", "ntdll/tests: Tests for RtlIpv6StringToAddressEx.", 6 },
    { "Mark Jansen", "shlwapi/tests: Add tests for AssocGetPerceivedType.", 1 },
    { "Mark Jansen", "shlwapi: Implement AssocGetPerceivedType.", 2 },
    { "Mark Jansen", "version: Test for VerQueryValueA.", 2 },
    { "Martin Storsjo", "ucrtbase: Hook up some functions with new names to existing implementations.", 1 },
    { "Matt Durgavich", "ws2_32: Proper WSACleanup implementation using wineserver function.", 2 },
    { "Matteo Bruni", "wined3d: Avoid calling wined3d_surface_blt() from surface_upload_from_surface().", 1 },
    { "Michael Müller", "Add licenses for fonts as separate files.", 1 },
    { "Michael Müller", "amstream: Implement IAMMediaStream::GetMultiMediaStream.", 1 },
    { "Michael Müller", "api-ms-win-appmodel-runtime-l1-1-1: Add new dll.", 1 },
    { "Michael Müller", "api-ms-win-core-apiquery-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-core-com-l1-1-1: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-core-delayload-l1-1-1: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-core-heap-l2-1-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-core-kernel32-legacy-l1-1-1: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-core-libraryloader-l1-2-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-core-memory-l1-1-2: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-core-quirks-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-core-shlwapi-obsolete-l1-2-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-core-threadpool-l1-2-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-core-winrt-registration-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-core-wow64-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-eventing-classicprovider-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-rtcore-ntuser-draw-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-rtcore-ntuser-window-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-shcore-obsolete-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-shcore-stream-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "api-ms-win-shcore-thread-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "browseui: Implement IProgressDialog::SetAnimation.", 1 },
    { "Michael Müller", "browseui: Implement PROGDLG_AUTOTIME flag for IProgressDialog.", 1 },
    { "Michael Müller", "combase: Add stub for CleanupTlsOleState.", 1 },
    { "Michael Müller", "combase: Add stub for RoGetApartmentIdentifier.", 1 },
    { "Michael Müller", "combase: Add stub for RoGetServerActivatableClasses.", 1 },
    { "Michael Müller", "combase: Add stub for RoRegisterActivationFactories.", 1 },
    { "Michael Müller", "combase: Add stub for RoRegisterForApartmentShutdown.", 1 },
    { "Michael Müller", "combase: Implement RoActivateInstance.", 1 },
    { "Michael Müller", "combase: Implement RoGetActivationFactory.", 1 },
    { "Michael Müller", "d3dx9_36/tests: Add initial tests for dummy skininfo interface.", 1 },
    { "Michael Müller", "d3dx9_36: Return dummy skininfo interface in D3DXLoadSkinMeshFromXof when skin information is unavailable.", 1 },
    { "Michael Müller", "ddraw/tests: Add more tests for IDirect3DTexture2::Load.", 1 },
    { "Michael Müller", "ddraw/tests: Add more tests for IDirectDraw7::EnumSurfaces.", 1 },
    { "Michael Müller", "ddraw: Allow size and format conversions in IDirect3DTexture2::Load.", 1 },
    { "Michael Müller", "ddraw: Create rendering targets in video memory if possible.", 1 },
    { "Michael Müller", "ddraw: Fix arguments to IDirectDraw7::EnumSurfaces in DllMain.", 1 },
    { "Michael Müller", "ddraw: Implement DDENUMSURFACES_CANBECREATED flag in ddraw7_EnumSurfaces.", 1 },
    { "Michael Müller", "ddraw: Remove const from ddraw1_vtbl and ddraw_surface1_vtbl.", 1 },
    { "Michael Müller", "ddraw: Set dwZBufferBitDepth in ddraw7_GetCaps.", 1 },
    { "Michael Müller", "dxdiagn: Calling GetChildContainer with an empty string on a leaf container returns the object itself.", 1 },
    { "Michael Müller", "dxdiagn: Enumerate DirectSound devices and add some basic properties.", 1 },
    { "Michael Müller", "dxgi: Improve stubs for MakeWindowAssociation and GetWindowAssociation.", 1 },
    { "Michael Müller", "dxva2/tests: Add tests for dxva2 decoder.", 1 },
    { "Michael Müller", "dxva2: Add DRM mode for vaapi.", 1 },
    { "Michael Müller", "dxva2: Always destroy buffers when calling vaRenderPicture.", 1 },
    { "Michael Müller", "dxva2: Fill h264 luma and chroma weights / offsets with default values in case they are not specified.", 1 },
    { "Michael Müller", "dxva2: Implement h264 decoder.", 1 },
    { "Michael Müller", "dxva2: Implement stubbed DirectX Software VideoProcessor interface.", 1 },
    { "Michael Müller", "dxva2: Implement stubbed interfaces for IDirectXVideo{Acceleration,Decoder,Processor}Service.", 1 },
    { "Michael Müller", "dxva2: Initial implementation of MPEG2 decoder using vaapi backend.", 1 },
    { "Michael Müller", "explorer: Create CurrentControlSet\\\\Control\\\\Video registry key as non-volatile.", 1 },
    { "Michael Müller", "ext-ms-win-appmodel-usercontext-l1-1-0: Add dll and add stub for UserContextExtInitialize.", 1 },
    { "Michael Müller", "ext-ms-win-kernel32-package-current-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "ext-ms-win-ntuser-mouse-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "ext-ms-win-rtcore-ntuser-syscolors-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "ext-ms-win-rtcore-ntuser-sysparams-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "ext-ms-win-uxtheme-themes-l1-1-0: Add dll.", 1 },
    { "Michael Müller", "ext-ms-win-xaml-pal-l1-1-0: Add dll and add stub for XamlBehaviorEnabled.", 1 },
    { "Michael Müller", "ext-ms-win-xaml-pal-l1-1-0: Add stub for GetThemeServices.", 1 },
    { "Michael Müller", "hal: Implement KeQueryPerformanceCounter.", 1 },
    { "Michael Müller", "hnetcfg: Improve INetFwAuthorizedApplication::get_ProcessImageFileName stub.", 1 },
    { "Michael Müller", "ieframe: Return S_OK in IViewObject::Draw stub.", 1 },
    { "Michael Müller", "iertutil: Add dll and add stub for ordinal 811.", 1 },
    { "Michael Müller", "imagehlp: Catch invalid memory access in CheckSumMappedFile and add tests.", 1 },
    { "Michael Müller", "imagehlp: Fix checksum calculation for odd sizes.", 1 },
    { "Michael Müller", "imagehlp: Implement ImageLoad and cleanup ImageUnload.", 1 },
    { "Michael Müller", "imm32: Add stub for ImmDisableLegacyIME.", 1 },
    { "Michael Müller", "include/objidl.idl: Add IApartmentShutdown interface.", 1 },
    { "Michael Müller", "include/roapi.h: Add further typedefs.", 1 },
    { "Michael Müller", "include: Add IApplicationActivationManager interface declaration.", 1 },
    { "Michael Müller", "include: Add activation.idl with IActivationFactory interface.", 1 },
    { "Michael Müller", "include: Add dxva.h header file.", 1 },
    { "Michael Müller", "include: Add more constants to SYSTEM_INFORMATION_CLASS.", 1 },
    { "Michael Müller", "include: Declare a couple more file information class structures.", 1 },
    { "Michael Müller", "include: Fix an invalid UUID in dxva2api.idl.", 1 },
    { "Michael Müller", "kernel32: Add stub for GetCurrentPackageFamilyName and add related functions to spec file.", 1 },
    { "Michael Müller", "kernel32: Add support for progress callback in CopyFileEx.", 1 },
    { "Michael Müller", "kernel32: Implement GetFinalPathNameByHandle.", 1 },
    { "Michael Müller", "kernelbase: Add dll and add stub for QuirkIsEnabled.", 1 },
    { "Michael Müller", "libwine: Add process specific debug channels.", 1 },
    { "Michael Müller", "loader: Add commandline option --check-libs.", 1 },
    { "Michael Müller", "loader: Print library paths for --check-libs on Mac OS X.", 1 },
    { "Michael Müller", "mfplat/tests: Add tests.", 1 },
    { "Michael Müller", "mfplat: Implement MFTEnum.", 1 },
    { "Michael Müller", "mfplat: Implement MFTRegister.", 2 },
    { "Michael Müller", "mfplat: Implement MFTUnregister.", 1 },
    { "Michael Müller", "mountmgr.sys: Write usable device paths into HKLM\\\\SYSTEM\\\\MountedDevices.", 1 },
    { "Michael Müller", "mpr: Return correct error code for non network paths and REMOTE_NAME_INFO_LEVEL in WNetGetUniversalName.", 1 },
    { "Michael Müller", "mscoree: Implement semi-stub for _CorValidateImage.", 1 },
    { "Michael Müller", "msvcr120: Add stub for _SetWinRTOutOfMemoryExceptionCallback.", 1 },
    { "Michael Müller", "ntdll/tests: Add basic tests for RtlQueryPackageIdentity.", 1 },
    { "Michael Müller", "ntdll: Add stub for ApiSetQueryApiSetPresence.", 1 },
    { "Michael Müller", "ntdll: Add stub for RtlIpv6StringToAddressExW.", 1 },
    { "Michael Müller", "ntdll: Add stub for RtlQueryPackageIdentity.", 1 },
    { "Michael Müller", "ntdll: Allow special characters in pipe names.", 1 },
    { "Michael Müller", "ntdll: Check architecture before loading module.", 1 },
    { "Michael Müller", "ntdll: Fix parameters for RtlIpv4StringToAddressExW stub.", 1 },
    { "Michael Müller", "ntdll: Implement SystemRecommendedSharedDataAlignment class in NtQuerySystemInformation.", 1 },
    { "Michael Müller", "ntdll: Implement get_redirect function.", 1 },
    { "Michael Müller", "ntdll: Implement loader redirection scheme.", 1 },
    { "Michael Müller", "ntdll: Load CLI/.NET images in the same way as Windows XP and above.", 1 },
    { "Michael Müller", "ntdll: Move EventRegister from advapi32 to ntdll.", 1 },
    { "Michael Müller", "ntdll: Move EventSetInformation from advapi32 to ntdll.", 1 },
    { "Michael Müller", "ntdll: Move NtProtectVirtualMemory and NtCreateSection to separate pages on x86.", 2 },
    { "Michael Müller", "ntdll: Move RegisterTraceGuids from advapi32 to ntdll.", 1 },
    { "Michael Müller", "ntdll: Move code to determine module basename into separate function.", 1 },
    { "Michael Müller", "ntdll: Move logic to determine loadorder HKCU/app key into separate functions.", 1 },
    { "Michael Müller", "ntdll: Move logic to read loadorder registry values into separate function.", 1 },
    { "Michael Müller", "ntdll: Only enable true WRITECOPY protection when a special environment variable is set.", 1 },
    { "Michael Müller", "ntdll: Properly handle PAGE_WRITECOPY protection.", 5 },
    { "Michael Müller", "ntdll: Setup a temporary signal handler during process startup to handle page faults.", 2 },
    { "Michael Müller", "ntoskrnl.exe/tests: Add kernel compliant test functions.", 1 },
    { "Michael Müller", "ntoskrnl.exe: Add stub for PsRemoveLoadImageNotifyRoutine.", 1 },
    { "Michael Müller", "nvapi/tests: Use structure to list imports.", 1 },
    { "Michael Müller", "nvapi: Add NvAPI_GetPhysicalGPUsFromLogicalGPU.", 1 },
    { "Michael Müller", "nvapi: Add stub for EnumNvidiaDisplayHandle.", 1 },
    { "Michael Müller", "nvapi: Add stub for NvAPI_D3D9_RegisterResource.", 1 },
    { "Michael Müller", "nvapi: Add stub for NvAPI_D3D_GetCurrentSLIState.", 1 },
    { "Michael Müller", "nvapi: Add stub for NvAPI_D3D_GetObjectHandleForResource.", 1 },
    { "Michael Müller", "nvapi: Add stub for NvAPI_DISP_GetGDIPrimaryDisplayId.", 1 },
    { "Michael Müller", "nvapi: Add stub for NvAPI_EnumPhysicalGPUs.", 1 },
    { "Michael Müller", "nvapi: Add stub for NvAPI_GetLogicalGPUFromDisplay.", 1 },
    { "Michael Müller", "nvapi: Add stub for NvAPI_SYS_GetDriverAndBranchVersion.", 1 },
    { "Michael Müller", "nvapi: Add stub for NvAPI_Unload.", 1 },
    { "Michael Müller", "nvapi: Add stubs for NvAPI_EnumLogicalGPUs and undocumented equivalent.", 1 },
    { "Michael Müller", "nvapi: Add stubs for NvAPI_GPU_GetFullName.", 1 },
    { "Michael Müller", "nvapi: Explicity return NULL for 0x33c7358c and 0x593e8644.", 1 },
    { "Michael Müller", "nvapi: First implementation.", 1 },
    { "Michael Müller", "nvapi: Print fixme message for NvAPI_D3D9_StretchRectEx.", 1 },
    { "Michael Müller", "nvcuda: Emulate two d3d9 initialization functions.", 1 },
    { "Michael Müller", "nvcuda: First implementation.", 2 },
    { "Michael Müller", "nvcuda: Properly wrap undocumented 'ContextStorage' interface and add tests.", 1 },
    { "Michael Müller", "nvcuda: Search for dylib library on Mac OS X.", 1 },
    { "Michael Müller", "nvcuvid: First implementation.", 2 },
    { "Michael Müller", "nvencodeapi: Add debian specific paths to native library.", 1 },
    { "Michael Müller", "nvencodeapi: Add support for version 6.0.", 1 },
    { "Michael Müller", "nvencodeapi: First implementation.", 1 },
    { "Michael Müller", "ole32: Implement CoGetApartmentType.", 1 },
    { "Michael Müller", "openal32: Export EFX extension functions.", 1 },
    { "Michael Müller", "server: Compatibility with Wine Staging format for high precision registry timestamps.", 1 },
    { "Michael Müller", "server: Implement support for global and local shared memory blocks based on memfd.", 1 },
    { "Michael Müller", "server: Implement support for pseudo tokens CurrentProcessToken, CurrentThreadToken, CurrentThreadEffectiveToken.", 1 },
    { "Michael Müller", "setupapi/tests: Add test for IDF_CHECKFIRST and SetupPromptForDiskA/W.", 1 },
    { "Michael Müller", "setupapi: Add support for IDF_CHECKFIRST flag in SetupPromptForDiskW.", 1 },
    { "Michael Müller", "setupapi: Check handle type for HSPFILEQ handles.", 1 },
    { "Michael Müller", "sfc_os: Set an error code in SfcGetNextProtectedFile stub.", 1 },
    { "Michael Müller", "shell32: Add IDragSourceHelper stub interface.", 1 },
    { "Michael Müller", "shell32: Add general tab in file property dialog.", 1 },
    { "Michael Müller", "shell32: Add placeholder icons to match icon offset with XP.", 1 },
    { "Michael Müller", "shell32: Add support for extra large and jumbo icon lists.", 2 },
    { "Michael Müller", "shell32: Choose return value for SHFileOperationW depending on windows version.", 1 },
    { "Michael Müller", "shell32: Cleanup IDropTargetHelper and preparation for IDragSourceHelper.", 1 },
    { "Michael Müller", "shell32: Correct indentation in shfileop.c.", 1 },
    { "Michael Müller", "shell32: Do not use unixfs for devices without mountpoint.", 1 },
    { "Michael Müller", "shell32: Implement NewMenu with new folder item.", 1 },
    { "Michael Müller", "shell32: Implement file operation progress dialog.", 1 },
    { "Michael Müller", "shell32: Pass FILE_INFORMATION into SHNotify* functions.", 1 },
    { "Michael Müller", "shell32: Set SFGAO_HASSUBFOLDER correctly for normal shellfolders.", 1 },
    { "Michael Müller", "shell32: Set SFGAO_HASSUBFOLDER correctly for unixfs.", 1 },
    { "Michael Müller", "shell32: Show animation during SHFileOperation.", 1 },
    { "Michael Müller", "shell32: Use manual redirection for RunDLL_CallEntry16.", 1 },
    { "Michael Müller", "user32: Allow changing the tablet / media center status via wine registry key.", 1 },
    { "Michael Müller", "user32: Decrease minimum SetTimer interval to 5 ms.", 2 },
    { "Michael Müller", "user32: Fix calculation of listbox size when horizontal scrollbar is present.", 1 },
    { "Michael Müller", "user32: Get rid of wineserver call for GetLastInputInfo.", 1 },
    { "Michael Müller", "uxthemegtk: Add configure check and stub dll.", 1 },
    { "Michael Müller", "uxthemegtk: Implement enumeration of themes, color and sizes.", 1 },
    { "Michael Müller", "uxthemegtk: Print class name before calling vtable functions.", 1 },
    { "Michael Müller", "uxthemegtk: Reset FPU flags before calling GTK3 functions.", 1 },
    { "Michael Müller", "wbemdisp: Add ISWbemSecurity stub interface.", 1 },
    { "Michael Müller", "wineboot: Add some generic hardware in HKEY_DYN_DATA\\\\Config Manager\\\\Enum.", 1 },
    { "Michael Müller", "winecfg: Add option to enable/disable GTK3 theming.", 1 },
    { "Michael Müller", "winecfg: Add staging tab for CSMT.", 1 },
    { "Michael Müller", "winecfg: Show unmounted devices and allow changing the device value.", 1 },
    { "Michael Müller", "wined3d: Add DXT1 to B4G4R4A4, DXT1 to B5G5R5A1 and DXT3 to B4G4R4A4 conversion.", 1 },
    { "Michael Müller", "wined3d: Add support for DXTn software decoding through libtxc_dxtn.", 3 },
    { "Michael Müller", "wined3d: Allow changing strict drawing through an exported function.", 1 },
    { "Michael Müller", "wined3d: Load dxtn dylib library on Mac OS X.", 1 },
    { "Michael Müller", "wined3d: Use pci and memory information from MESA if possible.", 2 },
    { "Michael Müller", "wined3d: Use real values for memory accounting on NVIDIA cards.", 1 },
    { "Michael Müller", "winex11.drv: Allow changing the opengl pixel format on the desktop window.", 1 },
    { "Michael Müller", "winex11.drv: Allow to select default display frequency in registry key.", 1 },
    { "Michael Müller", "winex11.drv: Indicate direct rendering through OpenGL extension.", 1 },
    { "Michael Müller", "winex11.drv: Only warn about used contexts in wglShareLists.", 1 },
    { "Michael Müller", "winex11: Prevent window managers from grouping all wine programs together.", 1 },
    { "Michael Müller", "wininet/tests: Add more tests for cookies.", 1 },
    { "Michael Müller", "wininet/tests: Check cookie behaviour when overriding host.", 1 },
    { "Michael Müller", "wininet/tests: Test auth credential reusage with host override.", 1 },
    { "Michael Müller", "wininet: Add support for INTERNET_OPTION_SETTINGS_CHANGED in InternetSetOption.", 1 },
    { "Michael Müller", "wininet: Allow INTERNET_OPTION_SETTINGS_CHANGED on connections.", 1 },
    { "Michael Müller", "wininet: Replacing header fields should fail if they do not exist yet.", 1 },
    { "Michael Müller", "wininet: Strip filename if no path is set in cookie.", 1 },
    { "Michael Müller", "winmm: Delay import ole32 msacm32 to workaround bug when loading multiple winmm versions.", 1 },
    { "Michael Müller", "ws2_32: Ignore socket type for protocol IPPROTO_IPV6 in getaddrinfo.", 1 },
    { "Michael Müller", "wusa: Ignore systemProtection subkey of registry key.", 1 },
    { "Michael Müller", "wusa: Implement WOW64 support.", 1 },
    { "Michael Müller", "wusa: Implement basic installation logic.", 1 },
    { "Michael Müller", "wusa: Print warning when encountering msdelta compressed files.", 1 },
    { "Michael Müller", "wusa: Treat empty update list as error.", 1 },
    { "Olivier F. R. Dierick", "shell32: Check IsWoW64Process before calling Wow64 functions.", 2 },
    { "Peter Hater", "comctl32: Implement PROPSHEET_InsertPage based on PROPSHEET_AddPage.", 1 },
    { "Qian Hong", "advapi32/tests: Test prefix and use of TokenPrimaryGroup Sid.", 1 },
    { "Qian Hong", "advapi32: Fallback to Sid string when LookupAccountSid fails.", 1 },
    { "Qian Hong", "advapi32: Fix name and use of DOMAIN_GROUP_RID_USERS.", 1 },
    { "Qian Hong", "advapi32: Initialize buffer length to zero in LsaLookupSids to prevent crash.", 2 },
    { "Qian Hong", "advapi32: Prepend a hidden LSA_TRUST_INFORMATION in LsaLookupNames2 to avoid crash when Domains[-1] incorrectly accessed by application.", 2 },
    { "Qian Hong", "advapi32: Prepend a hidden LSA_TRUST_INFORMATION in LsaLookupSids to avoid crash when Domains[-1] incorrectly accessed by application.", 2 },
    { "Qian Hong", "kernel32: Fallback to default comspec when %COMSPEC% is not set.", 1 },
    { "Qian Hong", "kernel32: Init TimezoneInformation registry.", 1 },
    { "Qian Hong", "msvcrt/tests: Add tests for stdout and stderr refcount.", 1 },
    { "Qian Hong", "msvcrt: Implemenent refcount check for stdout and stderr.", 1 },
    { "Qian Hong", "ntdll/tests: Added tests for open behaviour on readonly files.", 1 },
    { "Qian Hong", "ntdll/tests: Added tests to set disposition on file which is mapped to memory.", 1 },
    { "Qian Hong", "ntdll: Add fake data implementation for ProcessQuotaLimits class.", 1 },
    { "Qian Hong", "ntdll: Implement FileNamesInformation class support.", 1 },
    { "Qian Hong", "ntdll: Improve ReadDataAvailable handling in FilePipeLocalInformation class support.", 1 },
    { "Qian Hong", "ntdll: Initialize mod_name to zero.", 1 },
    { "Qian Hong", "ntdll: Set EOF on file which has a memory mapping should fail.", 1 },
    { "Qian Hong", "server: Create primary group using DOMAIN_GROUP_RID_USERS.", 1 },
    { "Qian Hong", "server: Do not allow to set disposition on file which has a file mapping.", 1 },
    { "Qian Hong", "server: Map EXDEV to STATUS_NOT_SAME_DEVICE.", 1 },
    { "Rodrigo Rivas", "user32: Fix error handling in {Begin,End,}DeferWindowPos() to match Windows behavior.", 1 },
    { "Samuel Kim", "comctl32: Fix buttons becoming unthemed when pressed/released.", 1 },
    { "Sebastian Lackner", "Revert \"dsound: Simplify error handling when creating a sound buffer.\".", 1 },
    { "Sebastian Lackner", "Revert \"dsound: Use a better name for IDirectSoundBufferImpl_Create().\".", 1 },
    { "Sebastian Lackner", "Revert \"iexplore: Sync registry and program resource values.\".", 1 },
    { "Sebastian Lackner", "Revert \"opengl32: Return a NULL pointer for functions requiring unsupported or disabled extensions.\".", 1 },
    { "Sebastian Lackner", "Revert \"wined3d: Call wglGetPixelFormat() through the gl_ops table.\".", 1 },
    { "Sebastian Lackner", "advapi32/tests: Add ACL inheritance tests for creating subdirectories with NtCreateFile.", 1 },
    { "Sebastian Lackner", "advapi32/tests: Add tests for ACL inheritance in CreateDirectoryA.", 1 },
    { "Sebastian Lackner", "advapi: Trigger write watches before passing userdata pointer to read syscall.", 1 },
    { "Sebastian Lackner", "combase/tests: Add tests for WindowsCompareStringOrdinal.", 1 },
    { "Sebastian Lackner", "combase/tests: Add tests for WindowsTrimString{Start,End}.", 1 },
    { "Sebastian Lackner", "combase: Implement WindowsCompareStringOrdinal.", 2 },
    { "Sebastian Lackner", "combase: Implement WindowsTrimStringEnd.", 1 },
    { "Sebastian Lackner", "combase: Implement WindowsTrimStringStart.", 1 },
    { "Sebastian Lackner", "comctl32/tests: Add tests for PROPSHEET_InsertPage.", 1 },
    { "Sebastian Lackner", "configure: Also add the absolute RPATH when linking against libwine.", 1 },
    { "Sebastian Lackner", "d3dx9_24: Add an interface wrapper for different version of ID3DXEffect.", 1 },
    { "Sebastian Lackner", "d3dx9_25: Add an interface wrapper for different version of ID3DXEffect.", 1 },
    { "Sebastian Lackner", "d3dx9_36: Allow to query for d3dx9_26 specific ID3DXEffect interface.", 1 },
    { "Sebastian Lackner", "d3dx9_36: Improve stub for ID3DXEffectImpl_CloneEffect.", 1 },
    { "Sebastian Lackner", "dbghelp: Always check for debug symbols in BINDIR.", 1 },
    { "Sebastian Lackner", "dinput: Do not wait for hook thread startup in IDirectInput8::Initialize.", 1 },
    { "Sebastian Lackner", "dsound: Allow disabling of EAX support in the registry.", 1 },
    { "Sebastian Lackner", "dsound: Apply filters before sound is multiplied to speakers.", 1 },
    { "Sebastian Lackner", "dsound: Various improvements to EAX support.", 1 },
    { "Sebastian Lackner", "dxva2: Implement semi-stub for Direct3DDeviceManager9 interface.", 1 },
    { "Sebastian Lackner", "fonts: Add Liberation Mono as an Courier New replacement.", 1 },
    { "Sebastian Lackner", "fonts: Add Liberation Serif as an Times New Roman replacement.", 1 },
    { "Sebastian Lackner", "gdi32: Perform lazy initialization of fonts to improve startup performance.", 1 },
    { "Sebastian Lackner", "include: Add cuda.h.", 1 },
    { "Sebastian Lackner", "iphlpapi: Fallback to system ping when ICMP permissions are not present.", 1 },
    { "Sebastian Lackner", "kenrel32/tests: Add further tests for comparing strings ending with multiple \\\\0 characters.", 1 },
    { "Sebastian Lackner", "kernel32/tests: Add additional tests for PIPE_NOWAIT in overlapped mode.", 1 },
    { "Sebastian Lackner", "kernel32/tests: Add additional tests for condition mask of VerifyVersionInfoA.", 1 },
    { "Sebastian Lackner", "kernel32/tests: Add more tests with overlapped IO and partial reads from named pipes.", 1 },
    { "Sebastian Lackner", "kernel32/tests: Add some more tests for NORM_IGNORESYMBOLS.", 1 },
    { "Sebastian Lackner", "kernel32/tests: Add tests for PIPE_NOWAIT in message mode.", 1 },
    { "Sebastian Lackner", "kernel32/tests: Add tests for PeekNamedPipe with partial received messages.", 1 },
    { "Sebastian Lackner", "kernel32/tests: Add tests for sending and receiving large messages.", 1 },
    { "Sebastian Lackner", "kernel32/tests: Only allow one test result.", 1 },
    { "Sebastian Lackner", "kernel32/tests: Test sending, peeking and receiving an empty message.", 1 },
    { "Sebastian Lackner", "kernel32: Add winediag message to show warning, that this isn't vanilla wine.", 1 },
    { "Sebastian Lackner", "kernel32: Allow non-nullterminated string as working directory in create_startup_info.", 1 },
    { "Sebastian Lackner", "kernel32: Fake success in SetFileCompletionNotificationModes.", 1 },
    { "Sebastian Lackner", "kernel32: Fix leaking directory handle in RemoveDirectoryW.", 2 },
    { "Sebastian Lackner", "kernel32: Forward InterlockedPushListSList to ntdll.", 1 },
    { "Sebastian Lackner", "kernel32: Implement passing security descriptors from CreateProcess to the wineserver.", 2 },
    { "Sebastian Lackner", "loader: Add commandline option --patches to show the patch list.", 1 },
    { "Sebastian Lackner", "makedep: Add support for PARENTSPEC Makefile variable.", 1 },
    { "Sebastian Lackner", "mshtml: Fix some prototypes.", 1 },
    { "Sebastian Lackner", "msvcrt: Calculate sinh/cosh/exp/pow with higher precision.", 2 },
    { "Sebastian Lackner", "msvcrt: Use constants instead of hardcoded values.", 1 },
    { "Sebastian Lackner", "ntdll: APCs should call the implementation instead of the syscall thunk.", 1 },
    { "Sebastian Lackner", "ntdll: Add handling for partially received messages in NtReadFile.", 1 },
    { "Sebastian Lackner", "ntdll: Add semi-stub for FileFsVolumeInformation information class.", 1 },
    { "Sebastian Lackner", "ntdll: Add special handling for \\\\SystemRoot to satisfy MSYS2 case-insensitive system check.", 1 },
    { "Sebastian Lackner", "ntdll: Add support for hiding wine version information from applications.", 1 },
    { "Sebastian Lackner", "ntdll: Add support for nonblocking pipes.", 1 },
    { "Sebastian Lackner", "ntdll: Allow to set PIPE_NOWAIT on byte-mode pipes.", 1 },
    { "Sebastian Lackner", "ntdll: Always store SAMBA_XATTR_DOS_ATTRIB when path could be interpreted as hidden.", 1 },
    { "Sebastian Lackner", "ntdll: Always use 64-bit registry view on WOW64 setups.", 1 },
    { "Sebastian Lackner", "ntdll: Block signals while executing system APCs.", 2 },
    { "Sebastian Lackner", "ntdll: Do not allow to deallocate thread stack for current thread.", 1 },
    { "Sebastian Lackner", "ntdll: Expose wine_uninterrupted_[read|write]_memory as exports.", 1 },
    { "Sebastian Lackner", "ntdll: Fix condition mask handling in RtlVerifyVersionInfo.", 1 },
    { "Sebastian Lackner", "ntdll: Fix issues with write watches when using Exagear.", 1 },
    { "Sebastian Lackner", "ntdll: Fix race-condition when threads are killed during shutdown.", 1 },
    { "Sebastian Lackner", "ntdll: Fix return value for missing ACTIVATION_CONTEXT_SECTION_ASSEMBLY_INFORMATION key.", 1 },
    { "Sebastian Lackner", "ntdll: Fix some tests for overlapped partial reads.", 1 },
    { "Sebastian Lackner", "ntdll: Implement emulation of SIDT instruction when using Exagear.", 1 },
    { "Sebastian Lackner", "ntdll: Implement virtual_map_shared_memory.", 1 },
    { "Sebastian Lackner", "ntdll: Improve stub of NtQueryEaFile.", 1 },
    { "Sebastian Lackner", "ntdll: Move code to update user shared data into a separate function.", 1 },
    { "Sebastian Lackner", "ntdll: Move logic to check for broken pipe into a separate function.", 1 },
    { "Sebastian Lackner", "ntdll: Only enable wineserver shared memory communication when a special environment variable is set.", 1 },
    { "Sebastian Lackner", "ntdll: OutputDebugString should throw the exception a second time, if a debugger is attached.", 1 },
    { "Sebastian Lackner", "ntdll: Pre-cache file descriptors after opening a file.", 1 },
    { "Sebastian Lackner", "ntdll: Process APC calls before starting process.", 1 },
    { "Sebastian Lackner", "ntdll: Return STATUS_INVALID_DEVICE_REQUEST when trying to call NtReadFile on directory.", 1 },
    { "Sebastian Lackner", "ntdll: Return STATUS_SUCCESS from NtQuerySystemInformationEx.", 1 },
    { "Sebastian Lackner", "ntdll: Return buffer filled with random values from SystemInterruptInformation.", 1 },
    { "Sebastian Lackner", "ntdll: Return correct values in GetThreadTimes() for all threads.", 1 },
    { "Sebastian Lackner", "ntdll: Return fake device type when systemroot is located on virtual disk.", 1 },
    { "Sebastian Lackner", "ntdll: Reuse old async fileio structures if possible.", 1 },
    { "Sebastian Lackner", "ntdll: Run directory initialization function early during the process startup.", 1 },
    { "Sebastian Lackner", "ntdll: Set NamedPipeState to FILE_PIPE_CLOSING_STATE on broken pipe in NtQueryInformationFile.", 1 },
    { "Sebastian Lackner", "ntdll: Skip unused import descriptors when loading libraries.", 1 },
    { "Sebastian Lackner", "ntdll: Syscalls should not call Nt*Ex thunk wrappers.", 1 },
    { "Sebastian Lackner", "ntdll: Throw exception if invalid handle is passed to NtClose and debugger enabled.", 1 },
    { "Sebastian Lackner", "ntdll: Trigger write watches before passing userdata pointer to wait_reply.", 1 },
    { "Sebastian Lackner", "ntdll: Unify similar code in NtReadFile and FILE_AsyncReadService.", 1 },
    { "Sebastian Lackner", "ntdll: Unify similar code in NtWriteFile and FILE_AsyncWriteService.", 1 },
    { "Sebastian Lackner", "ntdll: Use POSIX implementation to enumerate directory content.", 1 },
    { "Sebastian Lackner", "ntdll: Use close_handle instead of NtClose for internal memory management functions.", 1 },
    { "Sebastian Lackner", "ntdll: Use wrapper functions for syscalls.", 1 },
    { "Sebastian Lackner", "ntoskrnl.exe/tests: Add initial driver testing framework and corresponding changes to Makefile system.", 2 },
    { "Sebastian Lackner", "ntoskrnl: Update USER_SHARED_DATA before accessing memory.", 1 },
    { "Sebastian Lackner", "nvcuda: Add stub dll.", 1 },
    { "Sebastian Lackner", "nvcuda: Add support for CUDA 7.0.", 1 },
    { "Sebastian Lackner", "nvcuda: Implement cuModuleLoad wrapper function.", 1 },
    { "Sebastian Lackner", "nvcuda: Implement new functions added in CUDA 6.5.", 1 },
    { "Sebastian Lackner", "nvcuda: Properly wrap stream callbacks by forwarding them to a worker thread.", 1 },
    { "Sebastian Lackner", "oleaut32/tests: Add a test for TKIND_COCLASS in proxy/stub marshalling.", 1 },
    { "Sebastian Lackner", "oleaut32: Handle TKIND_COCLASS in proxy/stub marshalling.", 1 },
    { "Sebastian Lackner", "oleaut32: Implement ITypeInfo_fnInvoke for TKIND_COCLASS in arguments.", 1 },
    { "Sebastian Lackner", "oleaut32: Implement TMStubImpl_Invoke on x86_64.", 1 },
    { "Sebastian Lackner", "oleaut32: Implement asm proxys for x86_64.", 1 },
    { "Sebastian Lackner", "oleaut32: Initial preparation to make marshalling compatible with x86_64.", 1 },
    { "Sebastian Lackner", "oleaut32: Pass a HREFTYPE to get_iface_guid.", 1 },
    { "Sebastian Lackner", "rasapi32: Set *lpcDevices in RasEnumDevicesA.", 1 },
    { "Sebastian Lackner", "riched20: Silence repeated FIXMEs triggered by Adobe Reader.", 1 },
    { "Sebastian Lackner", "rpcrt4: Fix prototype of RpcBindingServerFromClient.", 1 },
    { "Sebastian Lackner", "rpcrt4: Restore original error code when ReadFile fails with ERROR_MORE_DATA.", 1 },
    { "Sebastian Lackner", "server: Add a helper function set_sd_from_token_internal to merge two security descriptors.", 1 },
    { "Sebastian Lackner", "server: Add missing check for objattr variable in load_registry wineserver call (Coverity).", 1 },
    { "Sebastian Lackner", "server: Allow multiple registry notifications for the same key.", 1 },
    { "Sebastian Lackner", "server: Allow to open files without any permission bits.", 2 },
    { "Sebastian Lackner", "server: Avoid invalid memory access if creation of namespace fails in create_directory (Coverity).", 1 },
    { "Sebastian Lackner", "server: Do not hold reference on parent process.", 1 },
    { "Sebastian Lackner", "server: Do not signal thread until it is really gone.", 1 },
    { "Sebastian Lackner", "server: Don't attempt to use ptrace when running with Exagear.", 1 },
    { "Sebastian Lackner", "server: FILE_WRITE_ATTRIBUTES should succeed for readonly files.", 1 },
    { "Sebastian Lackner", "server: Fix handling of GetMessage after previous PeekMessage call.", 2 },
    { "Sebastian Lackner", "server: Growing files which are mapped to memory should still work.", 1 },
    { "Sebastian Lackner", "server: Implement locking and synchronization of keystate buffer.", 3 },
    { "Sebastian Lackner", "server: Increase size of PID table to 512 to reduce risk of collisions.", 1 },
    { "Sebastian Lackner", "server: Introduce a helper function to update the thread_input key state.", 1 },
    { "Sebastian Lackner", "server: Introduce a new alloc_handle object callback.", 2 },
    { "Sebastian Lackner", "server: Introduce refcounting for registry notifications.", 1 },
    { "Sebastian Lackner", "server: Link named pipes to their device.", 1 },
    { "Sebastian Lackner", "server: Return correct error codes for NtWriteFile when pipes are closed without disconnecting.", 1 },
    { "Sebastian Lackner", "server: Show warning if message mode is not supported.", 1 },
    { "Sebastian Lackner", "server: Store a list of associated queues for each thread input.", 1 },
    { "Sebastian Lackner", "server: Store a reference to the parent object for pipe servers.", 2 },
    { "Sebastian Lackner", "server: Support for thread and process security descriptors in new_process wineserver call.", 2 },
    { "Sebastian Lackner", "server: Temporarily store the full security descriptor for file objects.", 1 },
    { "Sebastian Lackner", "server: Track desktop handle count more correctly.", 1 },
    { "Sebastian Lackner", "server: Use SOCK_SEQPACKET socket in combination with SO_PEEK_OFF to implement message mode on Unix.", 6 },
    { "Sebastian Lackner", "server: When combining root and name, make sure there is only one slash.", 2 },
    { "Sebastian Lackner", "server: When creating new directories temporarily give read-permissions until they are opened.", 1 },
    { "Sebastian Lackner", "services: Start SERVICE_FILE_SYSTEM_DRIVER services with winedevice.", 1 },
    { "Sebastian Lackner", "shcore: Add dll.", 1 },
    { "Sebastian Lackner", "shell32: Create Microsoft\\\\Windows\\\\Themes directory during Wineprefix creation.", 1 },
    { "Sebastian Lackner", "shell32: Implement KF_FLAG_DEFAULT_PATH flag for SHGetKnownFolderPath.", 1 },
    { "Sebastian Lackner", "shlwapi/tests: Add additional tests for UrlCombine and UrlCanonicalize.", 1 },
    { "Sebastian Lackner", "shlwapi: SHMapHandle should not set error when NULL is passed as hShared.", 1 },
    { "Sebastian Lackner", "shlwapi: UrlCombineW workaround for relative paths.", 1 },
    { "Sebastian Lackner", "stdole32.tlb: Compile typelib with --oldtlb.", 1 },
    { "Sebastian Lackner", "user32: Avoid unnecessary wineserver calls in PeekMessage/GetMessage.", 1 },
    { "Sebastian Lackner", "user32: Cache the result of GetForegroundWindow.", 1 },
    { "Sebastian Lackner", "user32: Call UpdateWindow() during DIALOG_CreateIndirect.", 1 },
    { "Sebastian Lackner", "user32: Fix handling of invert_y in DrawTextExW.", 1 },
    { "Sebastian Lackner", "user32: Get rid of wineserver call for GetActiveWindow, GetFocus, GetCapture.", 1 },
    { "Sebastian Lackner", "user32: Get rid of wineserver call for GetInputState.", 1 },
    { "Sebastian Lackner", "user32: Globally invalidate key state on changes in other threads.", 1 },
    { "Sebastian Lackner", "user32: Increase MAX_WINPROCS to 16384.", 2 },
    { "Sebastian Lackner", "user32: Refresh MDI menus when DefMDIChildProc(WM_SETTEXT) is called.", 1 },
    { "Sebastian Lackner", "uxthemegtk: Correctly render buttons with GTK >= 3.14.0.", 1 },
    { "Sebastian Lackner", "vcomp/tests: Add tests for 64-bit atomic instructions.", 1 },
    { "Sebastian Lackner", "vcomp/tests: Reenable architecture dependent tests.", 1 },
    { "Sebastian Lackner", "vcomp: Implement 64-bit atomic instructions.", 1 },
    { "Sebastian Lackner", "vmm.vxd: Fix protection flags passed to VirtualAlloc.", 1 },
    { "Sebastian Lackner", "widl: Add --oldtlb switch in usage message.", 1 },
    { "Sebastian Lackner", "wine.inf: Add a ProfileList\\\\<UserSID> registry subkey.", 1 },
    { "Sebastian Lackner", "wineboot: Assign a drive serial number during prefix creation/update.", 1 },
    { "Sebastian Lackner", "wineboot: Init system32/drivers/etc/{host,networks,protocol,services}.", 1 },
    { "Sebastian Lackner", "winecfg: Add checkbox to enable/disable HideWineExports registry key.", 1 },
    { "Sebastian Lackner", "winecfg: Add checkbox to enable/disable vaapi GPU decoder.", 1 },
    { "Sebastian Lackner", "wined3d: Add second dll with STAGING_CSMT definition set.", 1 },
    { "Sebastian Lackner", "wined3d: Enable CSMT by default, print a winediag message informing about this patchset.", 1 },
    { "Sebastian Lackner", "wined3d: Rename wined3d_resource_(un)map to wined3d_resource_sub_resource_(un)map.", 1 },
    { "Sebastian Lackner", "wined3d: Silence repeated 'Unhandled blend factor 0' messages.", 1 },
    { "Sebastian Lackner", "wined3d: Silence repeated wined3d_swapchain_present FIXME.", 1 },
    { "Sebastian Lackner", "winedevice: Avoid invalid memory access when relocation block addresses memory outside of the current page.", 1 },
    { "Sebastian Lackner", "winegcc: Pass '-read_only_relocs suppress' to the linker on OSX.", 1 },
    { "Sebastian Lackner", "winelib: Append '(Staging)' at the end of the version string.", 1 },
    { "Sebastian Lackner", "winemenubuilder: Create desktop shortcuts with absolute wine path.", 1 },
    { "Sebastian Lackner", "winepulse.drv: Use a separate mainloop and ctx for pulse_test_connect.", 1 },
    { "Sebastian Lackner", "winepulse.drv: Use delay import for winealsa.drv.", 1 },
    { "Sebastian Lackner", "winex11: Enable/disable windows when they are (un)mapped by foreign applications.", 1 },
    { "Sebastian Lackner", "winex11: Forward all clipping requests to the right thread (including fullscreen clipping).", 1 },
    { "Sebastian Lackner", "winex11: Implement X11DRV_FLUSH_GDI_DISPLAY ExtEscape command.", 1 },
    { "Sebastian Lackner", "ws2_32: Avoid race-conditions of async WSARecv() operations with write watches.", 2 },
    { "Sebastian Lackner", "ws2_32: Implement returning the proper time with SO_CONNECT_TIME.", 1 },
    { "Sebastian Lackner", "ws2_32: Invalidate client-side file descriptor cache in WSACleanup.", 1 },
    { "Sebastian Lackner", "ws2_32: Reuse old async ws2_async_io structures if possible.", 1 },
    { "Sebastian Lackner", "wtsapi32: Partial implementation of WTSEnumerateProcessesW.", 1 },
    { "Sebastian Lackner", "wusa: Add workaround to be compatible with Vista packages.", 1 },
    { "Sebastian Lackner", "wusa: Improve tracing of installation process.", 1 },
    { "Steaphan Greene", "ntdll: Improve heap allocation performance by using more fine-grained free lists.", 1 },
    { "Stefan Dösinger", "Winex11: Complain about glfinish.", 1 },
    { "Stefan Dösinger", "d3d8/tests: D3DLOCK_NO_DIRTY_UPDATE on managed textures is temporarily broken.", 1 },
    { "Stefan Dösinger", "d3d9/tests: D3DLOCK_NO_DIRTY_UPDATE on managed textures is temporarily broken.", 1 },
    { "Stefan Dösinger", "wined3d: Accelerate DISCARD buffer maps.", 1 },
    { "Stefan Dösinger", "wined3d: Accelerate READONLY buffer maps.", 1 },
    { "Stefan Dösinger", "wined3d: Access the buffer dirty areas through the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Add a comment about worker thread lag.", 1 },
    { "Stefan Dösinger", "wined3d: Add query support to the command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Alloc the buffer map array before mapping the buffer.", 1 },
    { "Stefan Dösinger", "wined3d: Check our CS state to find out if a query is done.", 1 },
    { "Stefan Dösinger", "wined3d: Clean up buffer resource data through the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Clean up resource data through the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Clean up surfaces through the cs.", 1 },
    { "Stefan Dösinger", "wined3d: Clean up texture resources through the cs.", 1 },
    { "Stefan Dösinger", "wined3d: Clean up volume resource data through the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Completely reset the state on reset.", 1 },
    { "Stefan Dösinger", "wined3d: Create VBOs through the command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Create buffers before mapping them.", 1 },
    { "Stefan Dösinger", "wined3d: Create dummy textures through the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Create the initial context through the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Delete GL contexts through the CS in reset.", 1 },
    { "Stefan Dösinger", "wined3d: Delete GL contexts through the CS in uninit_3d.", 1 },
    { "Stefan Dösinger", "wined3d: Destroy queries through the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Destroy samplers through the command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Destroy shaders through the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Destroy vertex declarations through the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Destroy views through the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Dirtify changed textures through the command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Dirtify resources on unmap.", 1 },
    { "Stefan Dösinger", "wined3d: Discard implicit surfaces on unload.", 1 },
    { "Stefan Dösinger", "wined3d: Discard the backbuffer in discard presents.", 1 },
    { "Stefan Dösinger", "wined3d: Don't access the stateblock in find_draw_buffers_mask.", 1 },
    { "Stefan Dösinger", "wined3d: Don't call glFinish after clears.", 1 },
    { "Stefan Dösinger", "wined3d: Don't call glFinish after draws.", 1 },
    { "Stefan Dösinger", "wined3d: Don't call glFinish before swapping.", 1 },
    { "Stefan Dösinger", "wined3d: Don't call the public map function in surface_convert_format.", 1 },
    { "Stefan Dösinger", "wined3d: Don't call the public map function in surface_cpu_blt.", 1 },
    { "Stefan Dösinger", "wined3d: Don't delete the buffer in surface_cleanup.", 1 },
    { "Stefan Dösinger", "wined3d: Don't discard new buffers.", 1 },
    { "Stefan Dösinger", "wined3d: Don't force strict draw ordering for multithreaded CS.", 1 },
    { "Stefan Dösinger", "wined3d: Don't glFinish after a depth buffer blit.", 1 },
    { "Stefan Dösinger", "wined3d: Don't incref / decref textures in color / depth fill blits.", 1 },
    { "Stefan Dösinger", "wined3d: Don't lock the src volume in device_update_volume.", 1 },
    { "Stefan Dösinger", "wined3d: Don't poll queries that failed to start.", 1 },
    { "Stefan Dösinger", "wined3d: Don't preload buffers on unmap.", 1 },
    { "Stefan Dösinger", "wined3d: Don't put rectangle pointers into wined3d_cs_clear.", 1 },
    { "Stefan Dösinger", "wined3d: Don't request the frontbuffer to create dummy textures.", 1 },
    { "Stefan Dösinger", "wined3d: Don't reset the query state if it doesn't have a ctx.", 1 },
    { "Stefan Dösinger", "wined3d: Don't store pointers in struct wined3d_cs_present.", 1 },
    { "Stefan Dösinger", "wined3d: Don't store viewport pointers in the command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Don't sync on redundant discard calls.", 1 },
    { "Stefan Dösinger", "wined3d: Don't synchronize NOOVERWRITE buffer maps.", 1 },
    { "Stefan Dösinger", "wined3d: Don't try to flip sysmem copies in swapchain_present.", 1 },
    { "Stefan Dösinger", "wined3d: Don't try to sync VBOs manually on OSX with CSMT.", 1 },
    { "Stefan Dösinger", "wined3d: Fence blit operations.", 1 },
    { "Stefan Dösinger", "wined3d: Fence clear calls.", 1 },
    { "Stefan Dösinger", "wined3d: Fence color_fill operations.", 1 },
    { "Stefan Dösinger", "wined3d: Fence preload operations.", 1 },
    { "Stefan Dösinger", "wined3d: Fence present calls.", 1 },
    { "Stefan Dösinger", "wined3d: Fence render targets and depth stencils.", 1 },
    { "Stefan Dösinger", "wined3d: Fence texture reads in draws.", 1 },
    { "Stefan Dösinger", "wined3d: Fence update_texture and update_surface calls.", 1 },
    { "Stefan Dösinger", "wined3d: Finish the cs before changing the texture lod.", 1 },
    { "Stefan Dösinger", "wined3d: Get rid of WINED3D_BUFFER_FLUSH.", 1 },
    { "Stefan Dösinger", "wined3d: Get rid of state access in shader_generate_glsl_declarations.", 1 },
    { "Stefan Dösinger", "wined3d: Get rid of the end_scene flush and finish.", 1 },
    { "Stefan Dösinger", "wined3d: Get rid of the surface_upload_data glFinish.", 1 },
    { "Stefan Dösinger", "wined3d: Give the cs its own state.", 1 },
    { "Stefan Dösinger", "wined3d: Hack to reject unsupported color fills.", 1 },
    { "Stefan Dösinger", "wined3d: Hackily introduce a multithreaded command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Handle LOCATION_DISCARDED in surface_load_drawable.", 1 },
    { "Stefan Dösinger", "wined3d: Handle WINED3D_LOCATION_DISCARDED for sysmem loads.", 1 },
    { "Stefan Dösinger", "wined3d: Handle WINED3D_LOCATION_DISCARDED in surface_load_texture.", 1 },
    { "Stefan Dösinger", "wined3d: Handle evit_managed_resources through the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Ignore WINED3D_MAP_NO_DIRTY_UPDATE in resource_map.", 1 },
    { "Stefan Dösinger", "wined3d: Ignore buffer->resource.map_count in the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Implement DISCARD resource maps with buffers.", 1 },
    { "Stefan Dösinger", "wined3d: Implement DISCARD resource maps with heap memory.", 1 },
    { "Stefan Dösinger", "wined3d: Introduce a function to retrieve resource memory.", 1 },
    { "Stefan Dösinger", "wined3d: Introduce a separate queue for priority commands.", 1 },
    { "Stefan Dösinger", "wined3d: Introduce helper functions for mapping volumes.", 1 },
    { "Stefan Dösinger", "wined3d: Introduce resource fencing.", 1 },
    { "Stefan Dösinger", "wined3d: Invalidate containers via callback.", 1 },
    { "Stefan Dösinger", "wined3d: Invoke surface_unload through the CS in wined3d_surface_update_desc.", 1 },
    { "Stefan Dösinger", "wined3d: Keep track of the onscreen depth stencil in the command stream instead of the device.", 1 },
    { "Stefan Dösinger", "wined3d: Make resource maps and unmaps a priority command.", 1 },
    { "Stefan Dösinger", "wined3d: Make sure the new window is set up before setting up a context.", 1 },
    { "Stefan Dösinger", "wined3d: Make surface_load_location return nothing.", 1 },
    { "Stefan Dösinger", "wined3d: Make surface_ops->unmap specific for front buffers.", 1 },
    { "Stefan Dösinger", "wined3d: Merge get_pitch functions.", 1 },
    { "Stefan Dösinger", "wined3d: Move FBO destruction into the worker thread.", 1 },
    { "Stefan Dösinger", "wined3d: Move bitmap_data and user_memory into the resource.", 1 },
    { "Stefan Dösinger", "wined3d: Move buffer creation into the resource.", 1 },
    { "Stefan Dösinger", "wined3d: Move check_block_align to resource.c.", 1 },
    { "Stefan Dösinger", "wined3d: Move invalidate_location to resource.c.", 1 },
    { "Stefan Dösinger", "wined3d: Move load_location into the resource.", 1 },
    { "Stefan Dösinger", "wined3d: Move most of volume_map to resource.c.", 1 },
    { "Stefan Dösinger", "wined3d: Move simple location copying to the resource.", 1 },
    { "Stefan Dösinger", "wined3d: Move surface locations into the resource.", 1 },
    { "Stefan Dösinger", "wined3d: Move the framebuffer into wined3d_state.", 1 },
    { "Stefan Dösinger", "wined3d: Move validate_location to resource.c.", 1 },
    { "Stefan Dösinger", "wined3d: Move volume PBO infrastructure into the resource.", 1 },
    { "Stefan Dösinger", "wined3d: Only discard buffers that are in use.", 1 },
    { "Stefan Dösinger", "wined3d: Pass a context to surface_load_location.", 1 },
    { "Stefan Dösinger", "wined3d: Pass the depth stencil to swapchain->present.", 1 },
    { "Stefan Dösinger", "wined3d: Pass the state to draw_primitive.", 1 },
    { "Stefan Dösinger", "wined3d: Poll queries automatically in the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Preload buffers if streamsrc is not dirty.", 1 },
    { "Stefan Dösinger", "wined3d: Prevent the command stream from running ahead too far.", 1 },
    { "Stefan Dösinger", "wined3d: Put GL_APPLE_flush_buffer_range syncing back in place.", 1 },
    { "Stefan Dösinger", "wined3d: Put this into the query poll patch.", 1 },
    { "Stefan Dösinger", "wined3d: Put update_surface checks back in place.", 1 },
    { "Stefan Dösinger", "wined3d: Recreate ctx and dummy textures through the CS after resets.", 1 },
    { "Stefan Dösinger", "wined3d: Remove another glFinish.", 1 },
    { "Stefan Dösinger", "wined3d: Remove restated queries from the poll list.", 1 },
    { "Stefan Dösinger", "wined3d: Remove software cursor support.", 1 },
    { "Stefan Dösinger", "wined3d: Remove surface->pbo.", 1 },
    { "Stefan Dösinger", "wined3d: Remove surface_invalidate_location.", 1 },
    { "Stefan Dösinger", "wined3d: Remove surface_validate_location.", 1 },
    { "Stefan Dösinger", "wined3d: Remove the device_reset CS sync fixme.", 1 },
    { "Stefan Dösinger", "wined3d: Remove the texture destroy glFinish.", 1 },
    { "Stefan Dösinger", "wined3d: Render target lock hack.", 1 },
    { "Stefan Dösinger", "wined3d: Replace surface alloc functions with resource ones.", 1 },
    { "Stefan Dösinger", "wined3d: Replace surface_load_location with resource_load_location.", 1 },
    { "Stefan Dösinger", "wined3d: Replace the linked lists with a ringbuffer.", 1 },
    { "Stefan Dösinger", "wined3d: Request a glFinish before modifying resources outside the cs.", 1 },
    { "Stefan Dösinger", "wined3d: Run the cs asynchronously.", 1 },
    { "Stefan Dösinger", "wined3d: Send base vertex index updates through the cs.", 1 },
    { "Stefan Dösinger", "wined3d: Send blits through the command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Send bool constant updates through the command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Send buffer preloads through the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Send float constant updates through the command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Send getdc and releasedc through the command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Send int constant updates through the command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Send light updates through the command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Send primitive type updates through the command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Send render target view clears through the command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Send resource maps through the command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Send surface preloads through the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Send texture preloads through the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Send update_sub_resource calls through the command stream.", 1 },
    { "Stefan Dösinger", "wined3d: Send update_surface commands through the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Send update_texture calls through the CS.", 1 },
    { "Stefan Dösinger", "wined3d: Separate GL buffer discard control from ignoring MAP_DISCARD.", 1 },
    { "Stefan Dösinger", "wined3d: Separate buffer map write and draw read memory pointers.", 1 },
    { "Stefan Dösinger", "wined3d: Separate main and worker thread query state.", 1 },
    { "Stefan Dösinger", "wined3d: Separate resource map and draw buffers.", 1 },
    { "Stefan Dösinger", "wined3d: Set map_heap_memory = NULL when allocating a PBO.", 1 },
    { "Stefan Dösinger", "wined3d: Shadow device->offscreenBuffer in the context.", 1 },
    { "Stefan Dösinger", "wined3d: Store the color in clear ops instead of a pointer.", 1 },
    { "Stefan Dösinger", "wined3d: Store volume locations in the resource.", 1 },
    { "Stefan Dösinger", "wined3d: Unload resources through the CS in device_reset.", 1 },
    { "Stefan Dösinger", "wined3d: Unload resources through the CS in uninit_3d.", 1 },
    { "Stefan Dösinger", "wined3d: Unset some objects in state_init_default.", 1 },
    { "Stefan Dösinger", "wined3d: Use an event to block the worker thread when it is idle.", 1 },
    { "Stefan Dösinger", "wined3d: Use double-buffered buffers for multithreaded CS.", 1 },
    { "Stefan Dösinger", "wined3d: Use glBufferSubData instead of glMapBufferRange.", 1 },
    { "Stefan Dösinger", "wined3d: Use resource buffer mapping facilities in surfaces.", 1 },
    { "Stefan Dösinger", "wined3d: Use resource facilities to destroy PBOs.", 1 },
    { "Stefan Dösinger", "wined3d: Use resource_map for surface_map.", 1 },
    { "Stefan Dösinger", "wined3d: Wait for the CS in GetDC.", 1 },
    { "Stefan Dösinger", "wined3d: Wait for the cs before destroying objects.", 1 },
    { "Stefan Dösinger", "wined3d: Wait for the cs to finish before destroying the device.", 1 },
    { "Stefan Dösinger", "wined3d: Wait for the resource to be idle when destroying user memory surfaces.", 1 },
    { "Stefan Dösinger", "wined3d: Wait only for the buffer to be idle.", 1 },
    { "Stefan Dösinger", "wined3d: Wined3d_*_query_issue never fails.", 1 },
    { "Stefan Dösinger", "wined3d: Wrap GL BOs in a structure.", 1 },
    { "Torsten Kurbad", "fonts: Add Liberation Sans as an Arial replacement.", 2 },
    { "Wine Staging Team", "Autogenerated #ifdef patch for wined3d-CSMT_Main.", 1 },
    { "Zhenbo Li", "authz: Added additional stub functions.", 1 },
    { "Zhenbo Li", "mshtml: Add IHTMLLocation::hash property's getter implementation.", 1 },
    { "Zhenbo Li", "shell32: Fix SHFileOperation(FO_MOVE) for creating subdirectories.", 1 },
    { NULL, NULL, 0 }
};

/* return the applied non-standard patches */
const void *wine_get_patches(void)
{
    return &wine_patch_data[0];
}

/* return the build id string */
const char *wine_get_build_id(void)
{
    extern const char wine_build[];
    return wine_build;
}

/* exec a binary using the preloader if requested; helper for wine_exec_wine_binary */
static void preloader_exec( char **argv, int use_preloader )
{
    if (use_preloader)
    {
        static const char preloader[] = "wine-preloader";
        static const char preloader64[] = "wine64-preloader";
        char *p, *full_name;
        char **last_arg = argv, **new_argv;

        if (!(p = strrchr( argv[0], '/' ))) p = argv[0];
        else p++;

        full_name = xmalloc( p - argv[0] + sizeof(preloader64) );
        memcpy( full_name, argv[0], p - argv[0] );
        if (strendswith( p, "64" ))
            memcpy( full_name + (p - argv[0]), preloader64, sizeof(preloader64) );
        else
            memcpy( full_name + (p - argv[0]), preloader, sizeof(preloader) );

        /* make a copy of argv */
        while (*last_arg) last_arg++;
        new_argv = xmalloc( (last_arg - argv + 2) * sizeof(*argv) );
        memcpy( new_argv + 1, argv, (last_arg - argv + 1) * sizeof(*argv) );
        new_argv[0] = full_name;
        execv( full_name, new_argv );
        free( new_argv );
        free( full_name );
    }
    execv( argv[0], argv );
}

/* exec a wine internal binary (either the wine loader or the wine server) */
void wine_exec_wine_binary( const char *name, char **argv, const char *env_var )
{
    const char *path, *pos, *ptr;
    int use_preloader;

    if (!name) name = argv0_name;  /* no name means default loader */

#ifdef linux
    use_preloader = !strendswith( name, "wineserver" );
#else
    use_preloader = 0;
#endif

    if ((ptr = strrchr( name, '/' )))
    {
        /* if we are in build dir and name contains a path, try that */
        if (build_dir)
        {
            argv[0] = build_path( build_dir, name );
            preloader_exec( argv, use_preloader );
            free( argv[0] );
        }
        name = ptr + 1;  /* get rid of path */
    }

    /* first, bin directory from the current libdir or argv0 */
    if (bindir)
    {
        argv[0] = build_path( bindir, name );
        preloader_exec( argv, use_preloader );
        free( argv[0] );
    }

    /* then specified environment variable */
    if (env_var)
    {
        argv[0] = (char *)env_var;
        preloader_exec( argv, use_preloader );
    }

    /* now search in the Unix path */
    if ((path = getenv( "PATH" )))
    {
        argv[0] = xmalloc( strlen(path) + strlen(name) + 2 );
        pos = path;
        for (;;)
        {
            while (*pos == ':') pos++;
            if (!*pos) break;
            if (!(ptr = strchr( pos, ':' ))) ptr = pos + strlen(pos);
            memcpy( argv[0], pos, ptr - pos );
            strcpy( argv[0] + (ptr - pos), "/" );
            strcat( argv[0] + (ptr - pos), name );
            preloader_exec( argv, use_preloader );
            pos = ptr;
        }
        free( argv[0] );
    }

    /* and finally try BINDIR */
    argv[0] = build_path( BINDIR, name );
    preloader_exec( argv, use_preloader );
    free( argv[0] );
}
