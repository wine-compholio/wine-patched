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
const char *get_dlldir( const char **default_dlldir, const char **dll_prefix )
{
    *default_dlldir = DLLDIR;
    *dll_prefix = "/" DLLPREFIX;
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
    { "Alex Henrie", "kernel32/tests: Add tests for UTF-7 conversion.", 1 },
    { "Alex Henrie", "kernel32: Support UTF-7 in MultiByteToWideChar.", 3 },
    { "Alex Henrie", "kernel32: Support UTF-7 in WideCharToMultiByte.", 3 },
    { "Alexander E. Patrakov", "dsound: Add a linear resampler for use with a large number of mixing buffers.", 1 },
    { "André Hentschel", "libwine: Use try_mmap_fixed for wine64 on FreeBSD.", 1 },
    { "André Hentschel", "wpcap: Load libpcap dynamically.", 1 },
    { "Aric Stewart", "imm32: Do not let ImmDestroyContext destroy any default contexts.", 1 },
    { "Aric Stewart", "imm32: Limit cross thread access to ImmSet* functions.", 1 },
    { "Aric Stewart", "imm32: Move thread data from TLSEntry to an internal list.", 1 },
    { "Aric Stewart", "imm32: Restrict crossthread Association and destruction.", 1 },
    { "Aric Stewart", "imm32: Use thread data from target HWND.", 1 },
    { "Austin English", "ntdll: add NtSetLdtEntries/ZwSetLdtEntries stub.", 2 },
    { "Austin English", "ntoskrnl.exe: Add a stub for IoCsqInitialize.", 1 },
    { "Bernhard Reiter", "imagehlp: Implement parts of BindImageEx to make freezing Python scripts work.", 1 },
    { "Bruno Jesus", "ws2_32: Properly implement SO_CONNECT_TIME.", 2 },
    { "Christian Costa", "d3dx9_36: Add dxtn support.", 1 },
    { "Christian Costa", "d3dx9_36: Align texture dimensions to block size for compressed textures in D3DXCheckTextureRequirements.", 1 },
    { "Christian Costa", "d3dx9_36: Filter out D3DCompile warning messages that are not present with D3DCompileShader.", 4 },
    { "Christian Costa", "d3dx9_36: Implement D3DXGetShaderInputSemantics + tests.", 1 },
    { "Christian Costa", "d3dx9_36: Implement ID3DXSkinInfoImpl_UpdateSkinnedMesh.", 1 },
    { "Christian Costa", "d3dx9_36: No need to fail if we don't support vertices reordering in D3DXMESHOPT_ATTRSORT.", 1 },
    { "Christian Costa", "shdocvw: Check precisely ParseURLFromOutsideSourceX returned values in tests and make code clearer about that.", 3 },
    { "Christian Costa", "wined3d: Improve DXTn support and export conversion functions for d3dx9_36.", 1 },
    { "Damjan Jovanovic", "winex11.drv: Import X11's \"text/html\" as \"HTML Format\".", 3 },
    { "Dan Kegel", "kernel32: ConnectNamedPort should return FALSE and set ERROR_PIPE_CONNECTED on success in overlapped mode.", 1 },
    { "Dmitry Timoshkov", "gdiplus: Implement GdipCreateRegionRgnData.", 3 },
    { "Dmitry Timoshkov", "include: Fix definition of SECTION_BASIC_INFORMATION and SECTION_IMAGE_INFORMATION.", 1 },
    { "Dmitry Timoshkov", "kernel32/tests: Add tests for NtQuerySection.", 2 },
    { "Dmitry Timoshkov", "libs: Fix most problems with CompareString.", 1 },
    { "Dmitry Timoshkov", "ntdll: Implement NtQuerySection.", 2 },
    { "Dmitry Timoshkov", "server: Add support for setting file disposition information.", 1 },
    { "Dmitry Timoshkov", "server: Keep a pointer to parent's fd unix_name in the closed_fd structure.", 1 },
    { "Dmitry Timoshkov", "shell32: Implement SHCreateSessionKey.", 1 },
    { "Erich E. Hoover", "Appease the blessed version of gcc (4.5) when -Werror is enabled.", 1 },
    { "Erich E. Hoover", "Fix TOOLTIPS_GetTipText when a NULL instance is used.", 1 },
    { "Erich E. Hoover", "Fix TOOLTIPS_GetTipText when a resource cannot be found.", 1 },
    { "Erich E. Hoover", "fonts: Add Courier Prime as a Courier New replacement.", 1 },
    { "Erich E. Hoover", "fonts: Add WenQuanYi Micro Hei as a Microsoft Yahei replacement.", 1 },
    { "Erich E. Hoover", "iphlpapi: Implement AllocateAndGetTcpExTableFromStack.", 1 },
    { "Erich E. Hoover", "kernel32,ntdll: Add support for deleting junction points with RemoveDirectory.", 1 },
    { "Erich E. Hoover", "kernel32: Add a bunch more GetVolumePathName tests.", 1 },
    { "Erich E. Hoover", "kernel32: Advertise junction point support.", 1 },
    { "Erich E. Hoover", "kernel32: Convert GetVolumePathName tests into a list.", 1 },
    { "Erich E. Hoover", "kernel32: Implement GetSystemTimes.", 3 },
    { "Erich E. Hoover", "kernel32: Implement GetVolumePathName.", 1 },
    { "Erich E. Hoover", "libport: Add support for FreeBSD style extended attributes.", 1 },
    { "Erich E. Hoover", "libport: Add support for Mac OS X style extended attributes.", 1 },
    { "Erich E. Hoover", "ntdll/tests: Add test for deleting junction point target.", 1 },
    { "Erich E. Hoover", "ntdll: Add a test for junction point advertisement.", 1 },
    { "Erich E. Hoover", "ntdll: Add support for deleting junction points.", 1 },
    { "Erich E. Hoover", "ntdll: Add support for junction point creation.", 1 },
    { "Erich E. Hoover", "ntdll: Add support for reading junction points.", 1 },
    { "Erich E. Hoover", "ntdll: Implement retrieving DOS attributes in NtQueryInformationFile.", 1 },
    { "Erich E. Hoover", "ntdll: Implement retrieving DOS attributes in NtQuery[Full]AttributesFile and NtQueryDirectoryFile.", 1 },
    { "Erich E. Hoover", "ntdll: Implement storing DOS attributes in NtCreateFile.", 1 },
    { "Erich E. Hoover", "ntdll: Implement storing DOS attributes in NtSetInformationFile.", 1 },
    { "Erich E. Hoover", "ntdll: Perform the Unix-style hidden file check within the unified file info grabbing routine.", 1 },
    { "Erich E. Hoover", "ntdll: Unify retrieving the attributes of a file.", 1 },
    { "Erich E. Hoover", "quartz: Implement MediaSeeking_GetCurrentPosition on top of MediaSeeking_GetPositions.", 1 },
    { "Erich E. Hoover", "quartz: Implement MediaSeeking_GetStopPosition on top of MediaSeeking_GetPositions.", 1 },
    { "Erich E. Hoover", "quartz: Include the stream position in addition to the reference clock offset in the time returned by MediaSeeking_GetPositions.", 1 },
    { "Erich E. Hoover", "quartz: Remove unused cache of MediaSeeking stop position.", 1 },
    { "Erich E. Hoover", "server: Add blocked support for SIO_ADDRESS_LIST_CHANGE ioctl().", 1 },
    { "Erich E. Hoover", "server: Add compatibility code for handling the old method of storing ACLs.", 6 },
    { "Erich E. Hoover", "server: Add default security descriptor DACL for processes.", 1 },
    { "Erich E. Hoover", "server: Add default security descriptor ownership for processes.", 1 },
    { "Erich E. Hoover", "server: Add socket-side support for the interface change notification object.", 1 },
    { "Erich E. Hoover", "server: Convert return of file security masks with generic access mappings.", 7 },
    { "Erich E. Hoover", "server: Do not permit FileDispositionInformation to delete a file without write access.", 1 },
    { "Erich E. Hoover", "server: Implement socket-specific ioctl() routine.", 1 },
    { "Erich E. Hoover", "server: Implement the interface change notification object.", 2 },
    { "Erich E. Hoover", "server: Inherit security attributes from parent directories on SetSecurityInfo.", 7 },
    { "Erich E. Hoover", "server: Inherit security attributes from parent directories on creation.", 7 },
    { "Erich E. Hoover", "server: Retrieve file security attributes with extended file attributes.", 7 },
    { "Erich E. Hoover", "server: Store file security attributes with extended file attributes.", 7 },
    { "Erich E. Hoover", "server: Store user and group inside stored extended file attribute information.", 7 },
    { "Erich E. Hoover", "server: Unify the retrieval of security attributes for files and directories.", 7 },
    { "Erich E. Hoover", "server: Unify the storage of security attributes for files and directories.", 7 },
    { "Erich E. Hoover", "wined3d: Silence repeated resource_check_usage FIXME.", 2 },
    { "Erich E. Hoover", "winex11.drv: Update the check for broken nVidia RandR to test for the number of resolutions instead of the number of modes.", 1 },
    { "Erich E. Hoover", "ws2_32: Add an interactive test for interface change notifications.", 1 },
    { "Erich E. Hoover", "ws2_32: Add asynchronous support for TransmitFile.", 1 },
    { "Erich E. Hoover", "ws2_32: Add stub for TransmitFile.", 1 },
    { "Erich E. Hoover", "ws2_32: Add support for TF_DISCONNECT and TF_REUSE_SOCKET to TransmitFile.", 1 },
    { "Erich E. Hoover", "ws2_32: Check for invalid parameters in TransmitFile.", 1 },
    { "Erich E. Hoover", "ws2_32: Implement a basic synchronous TransmitFile.", 1 },
    { "Felix Yan", "winex11.drv: Update a candidate window's position with over-the-spot style.", 2 },
    { "Henri Verbeet", "d3d9: Don't decrease surface refcount when its already zero.", 1 },
    { "Jactry Zeng", "riched20: Fix ME_RunOfsFromCharOfs() when nCharOfs > strlen().", 1 },
    { "Jactry Zeng", "riched20: Implement ITextRange::GetStoryLength.", 1 },
    { "Jactry Zeng", "riched20: Implement ITextRange::GetText.", 1 },
    { "Jactry Zeng", "riched20: Implement ITextRange::IsEqual.", 1 },
    { "Jactry Zeng", "riched20: Implement ITextRange::SetRange.", 1 },
    { "Jactry Zeng", "riched20: Implement ITextSelection::GetStoryLength.", 1 },
    { "Jactry Zeng", "riched20: Implement IText{Selection, Range}::Set{Start, End}.", 1 },
    { "Jactry Zeng", "riched20: Stub for ITextFont interface and implement ITextRange::GetFont and ITextSelection::GetFont.", 1 },
    { "Jactry Zeng", "riched20: Stub for ITextPara interface and implement ITextRange::GetPara.", 1 },
    { "Joris van der Wel", "advapi32/tests: Add additional tests for passing a thread sd to CreateProcess.", 1 },
    { "Juergen Tretthahn", "winepulse: API Compatibility with 1.5.2 onward.", 2 },
    { "Ken Thomases", "Revert \"wined3d: Don't call GetPixelFormat() to set a flag that's already set.\".", 1 },
    { "Ken Thomases", "Revert \"wined3d: Restore the pixel format of the window whose pixel format was actually changed.\".", 1 },
    { "Ken Thomases", "Revert \"wined3d: Track if a context's hdc is private so we never need to restore its pixel format.\".", 1 },
    { "Ken Thomases", "Revert \"wined3d: Track if a context's private hdc has had its pixel format set, so we don't need to check it.\".", 1 },
    { "Ken Thomases", "Revert \"wined3d: When restoring pixel format in context_release(), mark the context as needing to be set on the next context_acquire().\".", 1 },
    { "Ken Thomases", "d3d8: Mark tests which no longer pass due to reverts as todo_wine.", 1 },
    { "Ken Thomases", "d3d9: Mark tests which no longer pass due to reverts as todo_wine.", 1 },
    { "Ken Thomases", "ddraw: Mark tests which no longer pass due to reverts as todo_wine.", 1 },
    { "Ken Thomases", "gdi32: Also accept \"\\\\.\\DISPLAY<n>\" devices names with <n> other than 1 as display devices.", 1 },
    { "Ken Thomases", "user32: Implement EnumDisplayDevicesW() based on EnumDisplayMonitors() and GetMonitorInfoW().", 1 },
    { "Ken Thomases", "winex11: Make GetMonitorInfo() give a different device name (\\.\\DISPLAY<n>) to each monitor.", 1 },
    { "Louis Lenders", "kernel32: Add tests for GetSystemTimes.", 1 },
    { "Maarten Lankhorst", "fix fdels trailing whitespaces.", 1 },
    { "Maarten Lankhorst", "winepulse.", 12 },
    { "Maarten Lankhorst", "winepulse: Add IAudioClock and IAudioClock2.", 1 },
    { "Maarten Lankhorst", "winepulse: Add IAudioRenderClient and IAudioCaptureClient.", 1 },
    { "Maarten Lankhorst", "winepulse: Add audioclient.", 1 },
    { "Maarten Lankhorst", "winepulse: Add audiostreamvolume.", 1 },
    { "Maarten Lankhorst", "winepulse: Add format and period probing.", 1 },
    { "Maarten Lankhorst", "winepulse: Add initial stub for pulseaudio support.", 1 },
    { "Maarten Lankhorst", "winepulse: Add official warning wine doesn't want to support winepulse.", 16 },
    { "Maarten Lankhorst", "winepulse: Add session support.", 1 },
    { "Maarten Lankhorst", "winepulse: Add support for missing formats, and silence an error for missing format tags.", 15 },
    { "Maarten Lankhorst", "winepulse: Fix low latency support.", 1 },
    { "Maarten Lankhorst", "winepulse: Fix winmm tests.", 17 },
    { "Maarten Lankhorst", "winepulse: Latency and compilation improvements.", 18 },
    { "Maarten Lankhorst", "winepulse: add support for IMarshal.", 1 },
    { "Maarten Lankhorst", "winepulse: disable the setevent part of the latency hack.", 1 },
    { "Maarten Lankhorst", "winepulse: drop realtime priority before thread destruction.", 1 },
    { "Maarten Lankhorst", "winepulse: fix the checks in IsFormatSupported.", 20 },
    { "Maarten Lankhorst", "winepulse: fix unneeded free in write.", 1 },
    { "Maarten Lankhorst", "winepulse: fixup IsFormatSupported calls.", 1 },
    { "Maarten Lankhorst", "winepulse: fixup a invalid free in mmdevapi.", 23 },
    { "Maarten Lankhorst", "winepulse: remove bogus SetEvent from pulse_started_callback.", 1 },
    { "Maarten Lankhorst", "winepulse: return early if padding didn't update.", 21 },
    { "Maarten Lankhorst", "winepulse: use a pi-mutex for serialization.", 1 },
    { "Maarten Lankhorst", "winmm: Load winealsa if winepulse is found.", 1 },
    { "Mark Harmstone", "winepulse: expose audio devices directly to programs.", 1 },
    { "Mark Harmstone", "winepulse: handle stream create failing correctly.", 1 },
    { "Michael Müller", "comctl32/tests: Add tests for LoadIconMetric function.", 1 },
    { "Michael Müller", "comctl32: Implement LoadIconMetric function.", 1 },
    { "Michael Müller", "kernel32/tests: Add tests for GetFinalPathNameByHandle.", 1 },
    { "Michael Müller", "kernel32/tests: Add tests for GetNumaProcessorNode.", 1 },
    { "Michael Müller", "kernel32: Implement GetFinalPathNameByHandle.", 1 },
    { "Michael Müller", "kernel32: Implement GetNumaProcessorNode.", 1 },
    { "Michael Müller", "loader: Add commandline option --check-libs.", 1 },
    { "Michael Müller", "msvcp90/tests: Add tests to check that basic_string_wchar_dtor returns NULL.", 1 },
    { "Michael Müller", "msvcp90: basic_string_wchar_dtor needs to return NULL.", 1 },
    { "Michael Müller", "msvcrt: Avoid crash when NULL pointer is passed to atof / strtod functions.", 1 },
    { "Michael Müller", "ntdll: Add support for Dynamic DST (daylight saving time) information in registry.", 1 },
    { "Michael Müller", "ntdll: Allow special characters in pipe names.", 1 },
    { "Michael Müller", "ntdll: Move NtProtectVirtualMemory and NtCreateSection to separate pages on x86.", 2 },
    { "Michael Müller", "ntdll: Only enable true WRITECOPY protection when a special environment variable is set.", 1 },
    { "Michael Müller", "ntdll: Properly handle PAGE_WRITECOPY protection.", 4 },
    { "Michael Müller", "ntdll: Setup a temporary signal handler during process startup to handle page faults.", 1 },
    { "Michael Müller", "ntoskrnl: Add stub for KeSetSystemAffinityThread.", 1 },
    { "Michael Müller", "server: Return error when opening a terminating process.", 3 },
    { "Michael Müller", "setupapi/tests: Add test for IDF_CHECKFIRST and SetupPromptForDiskA/W.", 1 },
    { "Michael Müller", "setupapi: Add support for IDF_CHECKFIRST flag in SetupPromptForDiskW.", 1 },
    { "Michael Müller", "shell32: Add support for extra large and jumbo icon lists.", 1 },
    { "Michael Müller", "shell32: Choose return value for SHFileOperationW depending on windows version.", 1 },
    { "Michael Müller", "shell32: Use manual redirection for RunDLL_CallEntry16.", 1 },
    { "Michael Müller", "shlwapi: Correctly treat '.' when enumerating files in PathIsDirectoryEmptyW.", 1 },
    { "Michael Müller", "user32: Allow changing the tablet / media center status via wine registry key.", 1 },
    { "Michael Müller", "user32: Decrease minimum SetTimer interval to 5 ms.", 2 },
    { "Michael Müller", "wineboot: Add some generic hardware in HKEY_DYN_DATA\\Config Manager\\Enum.", 1 },
    { "Michael Müller", "winebuild: Set a valid major and minor linker version.", 1 },
    { "Michael Müller", "wined3d: Add support for DXTn software decoding through libtxc_dxtn.", 1 },
    { "Michael Müller", "wined3d: allow changing strict drawing through an exported function.", 1 },
    { "Michael Müller", "winex11.drv: Indicate direct rendering through OpenGL extension.", 1 },
    { "Michael Müller", "winex11.drv: Only warn about used contexts in wglShareLists.", 1 },
    { "Michael Müller", "winex11: Prevent window managers from grouping all wine programs together.", 1 },
    { "Qian Hong", "atl: Don't use GWLP_USERDATA to store IOCS to avoid conflict with Apps.", 1 },
    { "Sebastian Lackner", "advapi: Trigger write watches before passing userdata pointer to read syscall.", 1 },
    { "Sebastian Lackner", "configure: Also add the absolute RPATH when linking against libwine.", 1 },
    { "Sebastian Lackner", "configure: Fix detection of gnutls on Ubuntu 14.10.", 3 },
    { "Sebastian Lackner", "dbghelp: Don't fill KdHelp structure for usermode applications.", 1 },
    { "Sebastian Lackner", "gdi32: Return maximum number of pixel formats when NULL pointer is passed to wglDescribePixelFormat.", 1 },
    { "Sebastian Lackner", "kernel32/tests: Add a lot of picky GetVolumePathName tests.", 1 },
    { "Sebastian Lackner", "kernel32: Add winediag message to show warning, that this isn't vanilla wine.", 1 },
    { "Sebastian Lackner", "kernel32: Fix leaking directory handle in RemoveDirectoryW.", 2 },
    { "Sebastian Lackner", "kernel32: Implement passing security descriptors from CreateProcess to the wineserver.", 2 },
    { "Sebastian Lackner", "kernel32: Silence repeated CompareStringEx FIXME.", 1 },
    { "Sebastian Lackner", "loader: Add commandline option --patches to show the patch list.", 1 },
    { "Sebastian Lackner", "ntdll/tests: Add tests for Rtl[Decompress|Compress]Buffer and RtlGetCompressionWorkSpaceSize.", 1 },
    { "Sebastian Lackner", "ntdll: Implement LZNT1 algorithm for RtlDecompressBuffer.", 1 },
    { "Sebastian Lackner", "ntdll: Implement emulation of SIDT instruction when using Exagear.", 1 },
    { "Sebastian Lackner", "ntdll: Implement semi-stub for RtlCompressBuffer.", 1 },
    { "Sebastian Lackner", "ntdll: Implement semi-stub for RtlGetCompressionWorkSpaceSize.", 1 },
    { "Sebastian Lackner", "ntdll: OutputDebugString should throw the exception a second time, if a debugger is attached.", 1 },
    { "Sebastian Lackner", "ntdll: Return correct values in GetThreadTimes() for all threads.", 1 },
    { "Sebastian Lackner", "ntdll: Throw exception if invalid handle is passed to NtClose and debugger enabled.", 1 },
    { "Sebastian Lackner", "ntdll: Trigger write watches before passing userdata pointer to wait_reply.", 1 },
    { "Sebastian Lackner", "ntdll: Use lockfree implementation for get_cached_fd.", 4 },
    { "Sebastian Lackner", "ntoskrnl: Add TRACEs for instruction emulator on x86_64 to simplify debugging.", 1 },
    { "Sebastian Lackner", "ntoskrnl: Emulate 'mov Eb, Gb' instruction on x86 processor architecture.", 1 },
    { "Sebastian Lackner", "ntoskrnl: Emulate memory access to KI_USER_SHARED_DATA on x86_64.", 1 },
    { "Sebastian Lackner", "ntoskrnl: Handle issues when driver returns two different status codes from dispatcher.", 1 },
    { "Sebastian Lackner", "ntoskrnl: Initialize irp.Tail.Overlay.OriginalFileObject with stub file object.", 1 },
    { "Sebastian Lackner", "ole32/tests: Add tests for CoWaitForMultipleHandles.", 1 },
    { "Sebastian Lackner", "ole32: Don't process window events when APC calls are queued.", 1 },
    { "Sebastian Lackner", "ole32: Verify arguments for CoWaitForMultipleHandles, always initialize index to zero.", 1 },
    { "Sebastian Lackner", "riched20: Fix invalid memory access when parent object was destroyed earlier than child object.", 1 },
    { "Sebastian Lackner", "riched20: Implement ITextSelection_fnGetDuplicate.", 1 },
    { "Sebastian Lackner", "riched20: Silence repeated FIXMEs triggered by Adobe Reader.", 1 },
    { "Sebastian Lackner", "secur32: Return more context attributes in schan_InitializeSecurityContextW.", 1 },
    { "Sebastian Lackner", "server: Close fd if there is no space in thread inflight fd list.", 1 },
    { "Sebastian Lackner", "server: Support for thread and process security descriptors in new_process wineserver call.", 2 },
    { "Sebastian Lackner", "shell32: Implement KF_FLAG_DEFAULT_PATH flag for SHGetKnownFolderPath.", 1 },
    { "Sebastian Lackner", "shell32: Set the default security attributes for user shell folders.", 7 },
    { "Sebastian Lackner", "shlwapi/tests: Add additional tests for UrlCombine and UrlCanonicalize.", 1 },
    { "Sebastian Lackner", "shlwapi: UrlCombineW workaround for relative paths.", 1 },
    { "Sebastian Lackner", "shlwapi: Workaround for bugs in CompareStringW.", 1 },
    { "Sebastian Lackner", "user32/tests: Add tests for [Set|Get]WindowTextA with multiple threads.", 1 },
    { "Sebastian Lackner", "user32/tests: Add tests to demonstrate FindWindow doesn't send WM_GETTEXT messages.", 1 },
    { "Sebastian Lackner", "user32: Avoid sending window messages in FindWindowExW.", 1 },
    { "Sebastian Lackner", "user32: Call UpdateWindow() during DIALOG_CreateIndirect.", 1 },
    { "Sebastian Lackner", "user32: Fix handling of invert_y in DrawTextExW.", 1 },
    { "Sebastian Lackner", "user32: Increase MAX_WINPROCS to 16384.", 1 },
    { "Sebastian Lackner", "wine.inf: Add Dynamic DST exceptions for Israel Standard Time.", 1 },
    { "Sebastian Lackner", "wined3d: Silence repeated wined3d_swapchain_present FIXME.", 1 },
    { "Sebastian Lackner", "winedevice: Avoid invalid memory access when relocation block addresses memory outside of the current page.", 1 },
    { "Sebastian Lackner", "winelib: Append '(Staging)' at the end of the version string.", 1 },
    { "Sebastian Lackner", "winemenubuilder: Create desktop shortcuts with absolute wine path.", 1 },
    { "Sebastian Lackner", "winex11: Enable/disable windows when they are (un)mapped by foreign applications.", 1 },
    { "Sebastian Lackner", "winex11: Implement X11DRV_FLUSH_GDI_DISPLAY ExtEscape command.", 1 },
    { "Sebastian Lackner", "wtsapi32: Partial implementation of WTSEnumerateProcessesW.", 1 },
    { "Steaphan Greene", "ntdll: Improve heap allocation performance by using more fine-grained free lists.", 1 },
    { "Stefan Leichter", "ntoskrnl.exe: Emulate write to CR4 register.", 1 },
    { "Torsten Kurbad", "fonts: Add Liberation Sans as an Arial replacement.", 2 },
    { "Yanis Lukes", "wine.inf: Add fake ProductId to HKLM\\CurrentVersionNT.", 1 },
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
