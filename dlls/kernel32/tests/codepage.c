/*
 * Unit tests for code page to/from unicode translations
 *
 * Copyright (c) 2002 Dmitry Timoshkov
 * Copyright (c) 2008 Colin Finck
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
#include <limits.h>

#include "wine/test.h"
#include "windef.h"
#include "winbase.h"
#include "winnls.h"

static const WCHAR foobarW[] = {'f','o','o','b','a','r',0};

static void test_destination_buffer(void)
{
    LPSTR   buffer;
    INT     maxsize;
    INT     needed;
    INT     len;

    SetLastError(0xdeadbeef);
    needed = WideCharToMultiByte(CP_ACP, 0, foobarW, -1, NULL, 0, NULL, NULL);
    ok( (needed > 0), "returned %d with %u (expected '> 0')\n",
        needed, GetLastError());

    maxsize = needed*2;
    buffer = HeapAlloc(GetProcessHeap(), 0, maxsize);
    if (buffer == NULL) return;

    maxsize--;
    memset(buffer, 'x', maxsize);
    buffer[maxsize] = '\0';
    SetLastError(0xdeadbeef);
    len = WideCharToMultiByte(CP_ACP, 0, foobarW, -1, buffer, needed+1, NULL, NULL);
    ok( (len > 0), "returned %d with %u and '%s' (expected '> 0')\n",
        len, GetLastError(), buffer);

    memset(buffer, 'x', maxsize);
    buffer[maxsize] = '\0';
    SetLastError(0xdeadbeef);
    len = WideCharToMultiByte(CP_ACP, 0, foobarW, -1, buffer, needed, NULL, NULL);
    ok( (len > 0), "returned %d with %u and '%s' (expected '> 0')\n",
        len, GetLastError(), buffer);

    memset(buffer, 'x', maxsize);
    buffer[maxsize] = '\0';
    SetLastError(0xdeadbeef);
    len = WideCharToMultiByte(CP_ACP, 0, foobarW, -1, buffer, needed-1, NULL, NULL);
    ok( !len && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
        "returned %d with %u and '%s' (expected '0' with "
        "ERROR_INSUFFICIENT_BUFFER)\n", len, GetLastError(), buffer);

    memset(buffer, 'x', maxsize);
    buffer[maxsize] = '\0';
    SetLastError(0xdeadbeef);
    len = WideCharToMultiByte(CP_ACP, 0, foobarW, -1, buffer, 1, NULL, NULL);
    ok( !len && (GetLastError() == ERROR_INSUFFICIENT_BUFFER),
        "returned %d with %u and '%s' (expected '0' with "
        "ERROR_INSUFFICIENT_BUFFER)\n", len, GetLastError(), buffer);

    SetLastError(0xdeadbeef);
    len = WideCharToMultiByte(CP_ACP, 0, foobarW, -1, buffer, 0, NULL, NULL);
    ok( (len > 0), "returned %d with %u (expected '> 0')\n",
        len, GetLastError());

    SetLastError(0xdeadbeef);
    len = WideCharToMultiByte(CP_ACP, 0, foobarW, -1, NULL, needed, NULL, NULL);
    ok( !len && (GetLastError() == ERROR_INVALID_PARAMETER),
        "returned %d with %u (expected '0' with "
        "ERROR_INVALID_PARAMETER)\n", len, GetLastError());

    HeapFree(GetProcessHeap(), 0, buffer);
}


static void test_null_source(void)
{
    int len;
    DWORD GLE;

    SetLastError(0);
    len = WideCharToMultiByte(CP_ACP, 0, NULL, 0, NULL, 0, NULL, NULL);
    GLE = GetLastError();
    ok(!len && GLE == ERROR_INVALID_PARAMETER,
        "WideCharToMultiByte returned %d with GLE=%u (expected 0 with ERROR_INVALID_PARAMETER)\n",
        len, GLE);

    SetLastError(0);
    len = WideCharToMultiByte(CP_ACP, 0, NULL, -1, NULL, 0, NULL, NULL);
    GLE = GetLastError();
    ok(!len && GLE == ERROR_INVALID_PARAMETER,
        "WideCharToMultiByte returned %d with GLE=%u (expected 0 with ERROR_INVALID_PARAMETER)\n",
        len, GLE);
}

static void test_negative_source_length(void)
{
    int len;
    char buf[10];
    WCHAR bufW[10];

    /* Test, whether any negative source length works as strlen() + 1 */
    SetLastError( 0xdeadbeef );
    memset(buf,'x',sizeof(buf));
    len = WideCharToMultiByte(CP_ACP, 0, foobarW, -2002, buf, 10, NULL, NULL);
    ok(len == 7 && GetLastError() == 0xdeadbeef,
       "WideCharToMultiByte(-2002): len=%d error=%u\n", len, GetLastError());
    ok(!lstrcmpA(buf, "foobar"),
       "WideCharToMultiByte(-2002): expected \"foobar\" got \"%s\"\n", buf);

    SetLastError( 0xdeadbeef );
    memset(bufW,'x',sizeof(bufW));
    len = MultiByteToWideChar(CP_ACP, 0, "foobar", -2002, bufW, 10);
    ok(len == 7 && !lstrcmpW(bufW, foobarW) && GetLastError() == 0xdeadbeef,
       "MultiByteToWideChar(-2002): len=%d error=%u\n", len, GetLastError());

    SetLastError(0xdeadbeef);
    memset(bufW, 'x', sizeof(bufW));
    len = MultiByteToWideChar(CP_ACP, 0, "foobar", -1, bufW, 6);
    ok(len == 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "MultiByteToWideChar(-1): len=%d error=%u\n", len, GetLastError());
}

#define LONGBUFLEN 100000
static void test_negative_dest_length(void)
{
    int len, i;
    static char buf[LONGBUFLEN];
    static WCHAR originalW[LONGBUFLEN];
    static char originalA[LONGBUFLEN];
    DWORD theError;

    /* Test return on -1 dest length */
    SetLastError( 0xdeadbeef );
    memset(buf,'x',sizeof(buf));
    len = WideCharToMultiByte(CP_ACP, 0, foobarW, -1, buf, -1, NULL, NULL);
    todo_wine {
      ok(len == 0 && GetLastError() == ERROR_INVALID_PARAMETER,
         "WideCharToMultiByte(destlen -1): len=%d error=%x\n", len, GetLastError());
    }

    /* Test return on -1000 dest length */
    SetLastError( 0xdeadbeef );
    memset(buf,'x',sizeof(buf));
    len = WideCharToMultiByte(CP_ACP, 0, foobarW, -1, buf, -1000, NULL, NULL);
    todo_wine {
      ok(len == 0 && GetLastError() == ERROR_INVALID_PARAMETER,
         "WideCharToMultiByte(destlen -1000): len=%d error=%x\n", len, GetLastError());
    }

    /* Test return on INT_MAX dest length */
    SetLastError( 0xdeadbeef );
    memset(buf,'x',sizeof(buf));
    len = WideCharToMultiByte(CP_ACP, 0, foobarW, -1, buf, INT_MAX, NULL, NULL);
    ok(len == 7 && !lstrcmpA(buf, "foobar") && GetLastError() == 0xdeadbeef,
       "WideCharToMultiByte(destlen INT_MAX): len=%d error=%x\n", len, GetLastError());

    /* Test return on INT_MAX dest length and very long input */
    SetLastError( 0xdeadbeef );
    memset(buf,'x',sizeof(buf));
    for (i=0; i < LONGBUFLEN - 1; i++) {
        originalW[i] = 'Q';
        originalA[i] = 'Q';
    }
    originalW[LONGBUFLEN-1] = 0;
    originalA[LONGBUFLEN-1] = 0;
    len = WideCharToMultiByte(CP_ACP, 0, originalW, -1, buf, INT_MAX, NULL, NULL);
    theError = GetLastError();
    ok(len == LONGBUFLEN && !lstrcmpA(buf, originalA) && theError == 0xdeadbeef,
       "WideCharToMultiByte(srclen %d, destlen INT_MAX): len %d error=%x\n", LONGBUFLEN, len, theError);

}

static void test_other_invalid_parameters(void)
{
    char c_string[] = "Hello World";
    size_t c_string_len = sizeof(c_string);
    WCHAR w_string[] = {'H','e','l','l','o',' ','W','o','r','l','d',0};
    size_t w_string_len = sizeof(w_string) / sizeof(WCHAR);
    BOOL used;
    INT len;

    /* srclen=0 => ERROR_INVALID_PARAMETER */
    SetLastError(0xdeadbeef);
    len = WideCharToMultiByte(CP_ACP, 0, w_string, 0, c_string, c_string_len, NULL, NULL);
    ok(len == 0 && GetLastError() == ERROR_INVALID_PARAMETER, "len=%d error=%x\n", len, GetLastError());

    SetLastError(0xdeadbeef);
    len = MultiByteToWideChar(CP_ACP, 0, c_string, 0, w_string, w_string_len);
    ok(len == 0 && GetLastError() == ERROR_INVALID_PARAMETER, "len=%d error=%x\n", len, GetLastError());


    /* dst=NULL but dstlen not 0 => ERROR_INVALID_PARAMETER */
    SetLastError(0xdeadbeef);
    len = WideCharToMultiByte(CP_ACP, 0, w_string, w_string_len, NULL, c_string_len, NULL, NULL);
    ok(len == 0 && GetLastError() == ERROR_INVALID_PARAMETER, "len=%d error=%x\n", len, GetLastError());

    SetLastError(0xdeadbeef);
    len = MultiByteToWideChar(CP_ACP, 0, c_string, c_string_len, NULL, w_string_len);
    ok(len == 0 && GetLastError() == ERROR_INVALID_PARAMETER, "len=%d error=%x\n", len, GetLastError());


    /* CP_UTF7, CP_UTF8, or CP_SYMBOL and defchar not NULL => ERROR_INVALID_PARAMETER */
    /* CP_SYMBOL's behavior here is undocumented */
    SetLastError(0xdeadbeef);
    len = WideCharToMultiByte(CP_UTF7, 0, w_string, w_string_len, c_string, c_string_len, c_string, NULL);
    ok(len == 0 && GetLastError() == ERROR_INVALID_PARAMETER, "len=%d error=%x\n", len, GetLastError());

    SetLastError(0xdeadbeef);
    len = WideCharToMultiByte(CP_UTF8, 0, w_string, w_string_len, c_string, c_string_len, c_string, NULL);
    ok(len == 0 && GetLastError() == ERROR_INVALID_PARAMETER, "len=%d error=%x\n", len, GetLastError());

    SetLastError(0xdeadbeef);
    len = WideCharToMultiByte(CP_SYMBOL, 0, w_string, w_string_len, c_string, c_string_len, c_string, NULL);
    ok(len == 0 && GetLastError() == ERROR_INVALID_PARAMETER, "len=%d error=%x\n", len, GetLastError());


    /* CP_UTF7, CP_UTF8, or CP_SYMBOL and used not NULL => ERROR_INVALID_PARAMETER */
    /* CP_SYMBOL's behavior here is undocumented */
    SetLastError(0xdeadbeef);
    len = WideCharToMultiByte(CP_UTF7, 0, w_string, w_string_len, c_string, c_string_len, NULL, &used);
    ok(len == 0 && GetLastError() == ERROR_INVALID_PARAMETER, "len=%d error=%x\n", len, GetLastError());

    SetLastError(0xdeadbeef);
    len = WideCharToMultiByte(CP_UTF8, 0, w_string, w_string_len, c_string, c_string_len, NULL, &used);
    ok(len == 0 && GetLastError() == ERROR_INVALID_PARAMETER, "len=%d error=%x\n", len, GetLastError());

    SetLastError(0xdeadbeef);
    len = WideCharToMultiByte(CP_SYMBOL, 0, w_string, w_string_len, c_string, c_string_len, NULL, &used);
    ok(len == 0 && GetLastError() == ERROR_INVALID_PARAMETER, "len=%d error=%x\n", len, GetLastError());


    /* CP_UTF7, flags not 0 and used not NULL => ERROR_INVALID_PARAMETER */
    /* (tests precedence of ERROR_INVALID_PARAMETER over ERROR_INVALID_FLAGS) */
    /* The same test with CP_SYMBOL instead of CP_UTF7 gives ERROR_INVALID_FLAGS
       instead except on Windows NT4 */
    SetLastError(0xdeadbeef);
    len = WideCharToMultiByte(CP_UTF7, 1, w_string, w_string_len, c_string, c_string_len, NULL, &used);
    ok(len == 0 && GetLastError() == ERROR_INVALID_PARAMETER, "len=%d error=%x\n", len, GetLastError());
}

static void test_overlapped_buffers(void)
{
    static const WCHAR strW[] = {'j','u','s','t',' ','a',' ','t','e','s','t',0};
    static const char strA[] = "just a test";
    char buf[256];
    int ret;

    SetLastError(0xdeadbeef);
    memcpy(buf + 1, strW, sizeof(strW));
    ret = WideCharToMultiByte(CP_ACP, 0, (WCHAR *)(buf + 1), -1, buf, sizeof(buf), NULL, NULL);
    ok(ret == sizeof(strA), "unexpected ret %d\n", ret);
    ok(!memcmp(buf, strA, sizeof(strA)), "conversion failed: %s\n", buf);
    ok(GetLastError() == 0xdeadbeef, "GetLastError() is %u\n", GetLastError());
}

static void test_string_conversion(LPBOOL bUsedDefaultChar)
{
    char mbc;
    char mbs[5];
    int ret;
    WCHAR wc1 = 228;                           /* Western Windows-1252 character */
    WCHAR wc2 = 1088;                          /* Russian Windows-1251 character not displayable for Windows-1252 */
    static const WCHAR wcs[] = {'T', 'h', 1088, 'i', 0}; /* String with ASCII characters and a Russian character */
    static const WCHAR dbwcs[] = {28953, 25152, 0}; /* String with Chinese (codepage 950) characters */

    SetLastError(0xdeadbeef);
    ret = WideCharToMultiByte(1252, 0, &wc1, 1, &mbc, 1, NULL, bUsedDefaultChar);
    ok(ret == 1, "ret is %d\n", ret);
    ok(mbc == '\xe4', "mbc is %d\n", mbc);
    if(bUsedDefaultChar) ok(*bUsedDefaultChar == FALSE, "bUsedDefaultChar is %d\n", *bUsedDefaultChar);
    ok(GetLastError() == 0xdeadbeef, "GetLastError() is %u\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = WideCharToMultiByte(1252, 0, &wc2, 1, &mbc, 1, NULL, bUsedDefaultChar);
    ok(ret == 1, "ret is %d\n", ret);
    ok(mbc == 63, "mbc is %d\n", mbc);
    if(bUsedDefaultChar) ok(*bUsedDefaultChar == TRUE, "bUsedDefaultChar is %d\n", *bUsedDefaultChar);
    ok(GetLastError() == 0xdeadbeef, "GetLastError() is %u\n", GetLastError());

    if (IsValidCodePage(1251))
    {
        SetLastError(0xdeadbeef);
        ret = WideCharToMultiByte(1251, 0, &wc2, 1, &mbc, 1, NULL, bUsedDefaultChar);
        ok(ret == 1, "ret is %d\n", ret);
        ok(mbc == '\xf0', "mbc is %d\n", mbc);
        if(bUsedDefaultChar) ok(*bUsedDefaultChar == FALSE, "bUsedDefaultChar is %d\n", *bUsedDefaultChar);
        ok(GetLastError() == 0xdeadbeef ||
           broken(GetLastError() == 0), /* win95 */
           "GetLastError() is %u\n", GetLastError());

        SetLastError(0xdeadbeef);
        ret = WideCharToMultiByte(1251, 0, &wc1, 1, &mbc, 1, NULL, bUsedDefaultChar);
        ok(ret == 1, "ret is %d\n", ret);
        ok(mbc == 97, "mbc is %d\n", mbc);
        if(bUsedDefaultChar) ok(*bUsedDefaultChar == FALSE, "bUsedDefaultChar is %d\n", *bUsedDefaultChar);
        ok(GetLastError() == 0xdeadbeef, "GetLastError() is %u\n", GetLastError());
    }
    else
        skip("Codepage 1251 not available\n");

    /* This call triggers the last Win32 error */
    SetLastError(0xdeadbeef);
    ret = WideCharToMultiByte(1252, 0, wcs, -1, &mbc, 1, NULL, bUsedDefaultChar);
    ok(ret == 0, "ret is %d\n", ret);
    ok(mbc == 84, "mbc is %d\n", mbc);
    if(bUsedDefaultChar) ok(*bUsedDefaultChar == FALSE, "bUsedDefaultChar is %d\n", *bUsedDefaultChar);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "GetLastError() is %u\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = WideCharToMultiByte(1252, 0, wcs, -1, mbs, sizeof(mbs), NULL, bUsedDefaultChar);
    ok(ret == 5, "ret is %d\n", ret);
    ok(!strcmp(mbs, "Th?i"), "mbs is %s\n", mbs);
    if(bUsedDefaultChar) ok(*bUsedDefaultChar == TRUE, "bUsedDefaultChar is %d\n", *bUsedDefaultChar);
    ok(GetLastError() == 0xdeadbeef, "GetLastError() is %u\n", GetLastError());
    mbs[0] = 0;

    /* WideCharToMultiByte mustn't add any null character automatically.
       So in this case, we should get the same string again, even if we only copied the first three bytes. */
    SetLastError(0xdeadbeef);
    ret = WideCharToMultiByte(1252, 0, wcs, 3, mbs, sizeof(mbs), NULL, bUsedDefaultChar);
    ok(ret == 3, "ret is %d\n", ret);
    ok(!strcmp(mbs, "Th?i"), "mbs is %s\n", mbs);
    if(bUsedDefaultChar) ok(*bUsedDefaultChar == TRUE, "bUsedDefaultChar is %d\n", *bUsedDefaultChar);
    ok(GetLastError() == 0xdeadbeef, "GetLastError() is %u\n", GetLastError());
    ZeroMemory(mbs, 5);

    /* Now this shouldn't be the case like above as we zeroed the complete string buffer. */
    SetLastError(0xdeadbeef);
    ret = WideCharToMultiByte(1252, 0, wcs, 3, mbs, sizeof(mbs), NULL, bUsedDefaultChar);
    ok(ret == 3, "ret is %d\n", ret);
    ok(!strcmp(mbs, "Th?"), "mbs is %s\n", mbs);
    if(bUsedDefaultChar) ok(*bUsedDefaultChar == TRUE, "bUsedDefaultChar is %d\n", *bUsedDefaultChar);
    ok(GetLastError() == 0xdeadbeef, "GetLastError() is %u\n", GetLastError());

    /* Double-byte tests */
    ret = WideCharToMultiByte(1252, 0, dbwcs, 3, mbs, sizeof(mbs), NULL, bUsedDefaultChar);
    ok(ret == 3, "ret is %d\n", ret);
    ok(!strcmp(mbs, "??"), "mbs is %s\n", mbs);
    if(bUsedDefaultChar) ok(*bUsedDefaultChar == TRUE, "bUsedDefaultChar is %d\n", *bUsedDefaultChar);

    /* Length-only tests */
    SetLastError(0xdeadbeef);
    ret = WideCharToMultiByte(1252, 0, &wc2, 1, NULL, 0, NULL, bUsedDefaultChar);
    ok(ret == 1, "ret is %d\n", ret);
    if(bUsedDefaultChar) ok(*bUsedDefaultChar == TRUE, "bUsedDefaultChar is %d\n", *bUsedDefaultChar);
    ok(GetLastError() == 0xdeadbeef, "GetLastError() is %u\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = WideCharToMultiByte(1252, 0, wcs, -1, NULL, 0, NULL, bUsedDefaultChar);
    ok(ret == 5, "ret is %d\n", ret);
    if(bUsedDefaultChar) ok(*bUsedDefaultChar == TRUE, "bUsedDefaultChar is %d\n", *bUsedDefaultChar);
    ok(GetLastError() == 0xdeadbeef, "GetLastError() is %u\n", GetLastError());

    if (!IsValidCodePage(950))
    {
        skip("Codepage 950 not available\n");
        return;
    }

    /* Double-byte tests */
    SetLastError(0xdeadbeef);
    ret = WideCharToMultiByte(950, 0, dbwcs, -1, mbs, sizeof(mbs), NULL, bUsedDefaultChar);
    ok(ret == 5, "ret is %d\n", ret);
    ok(!strcmp(mbs, "\xb5H\xa9\xd2"), "mbs is %s\n", mbs);
    if(bUsedDefaultChar) ok(*bUsedDefaultChar == FALSE, "bUsedDefaultChar is %d\n", *bUsedDefaultChar);
    ok(GetLastError() == 0xdeadbeef, "GetLastError() is %u\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = WideCharToMultiByte(950, 0, dbwcs, 1, &mbc, 1, NULL, bUsedDefaultChar);
    ok(ret == 0, "ret is %d\n", ret);
    if(bUsedDefaultChar) ok(*bUsedDefaultChar == FALSE, "bUsedDefaultChar is %d\n", *bUsedDefaultChar);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "GetLastError() is %u\n", GetLastError());
    ZeroMemory(mbs, 5);

    SetLastError(0xdeadbeef);
    ret = WideCharToMultiByte(950, 0, dbwcs, 1, mbs, sizeof(mbs), NULL, bUsedDefaultChar);
    ok(ret == 2, "ret is %d\n", ret);
    ok(!strcmp(mbs, "\xb5H"), "mbs is %s\n", mbs);
    if(bUsedDefaultChar) ok(*bUsedDefaultChar == FALSE, "bUsedDefaultChar is %d\n", *bUsedDefaultChar);
    ok(GetLastError() == 0xdeadbeef, "GetLastError() is %u\n", GetLastError());

    /* Length-only tests */
    SetLastError(0xdeadbeef);
    ret = WideCharToMultiByte(950, 0, dbwcs, 1, NULL, 0, NULL, bUsedDefaultChar);
    ok(ret == 2, "ret is %d\n", ret);
    if(bUsedDefaultChar) ok(*bUsedDefaultChar == FALSE, "bUsedDefaultChar is %d\n", *bUsedDefaultChar);
    ok(GetLastError() == 0xdeadbeef, "GetLastError() is %u\n", GetLastError());

    SetLastError(0xdeadbeef);
    ret = WideCharToMultiByte(950, 0, dbwcs, -1, NULL, 0, NULL, bUsedDefaultChar);
    ok(ret == 5, "ret is %d\n", ret);
    if(bUsedDefaultChar) ok(*bUsedDefaultChar == FALSE, "bUsedDefaultChar is %d\n", *bUsedDefaultChar);
    ok(GetLastError() == 0xdeadbeef, "GetLastError() is %u\n", GetLastError());
}

static void test_utf7_encoding(void)
{
    struct utf16_to_utf7_test
    {
        WCHAR utf16[1024];
        int utf16_len;
        char utf7[1024];
        int utf7_len;
    };

    static const struct utf16_to_utf7_test utf16_to_utf7_tests[] = {
        /* tests some valid UTF-16 */
        {
            {0x4F60,0x597D,0x5417,0},
            4,
            "+T2BZfVQX-",
            11
        },
        /* tests some invalid UTF-16 */
        /* (stray lead surrogate) */
        {
            {0xD801,0},
            2,
            "+2AE-",
            6
        },
        /* tests some more invalid UTF-16 */
        /* (codepoint does not exist) */
        {
            {0xFF00,0},
            2,
            "+/wA-",
            6
        }
    };

    struct wcstombs_test
    {
        /* inputs */
        WCHAR src[1024];
        int srclen;
        int dstlen;
        /* expected outputs */
        char dst[1024];
        int chars_written;
        int len;
        DWORD error;
    };

    static const struct wcstombs_test wcstombs_tests[] = {
        /* tests srclen > strlenW(src) */
        {
            {'a',0,'b',0},
            4,
            1023,
            "a\0b",
            4,
            4,
            0xdeadbeef
        },
        /* tests srclen < strlenW(src) with directly encodable chars */
        {
            {'h','e','l','l','o',0},
            2,
            1023,
            "he",
            2,
            2,
            0xdeadbeef
        },
        /* tests srclen < strlenW(src) with non-directly encodable chars */
        {
            {0x4F60,0x597D,0x5417,0},
            2,
            1023,
            "+T2BZfQ-",
            8,
            8,
            0xdeadbeef
        },
        /* tests a buffer that runs out while not encoding a UTF-7 sequence */
        {
            {'h','e','l','l','o',0},
            -1,
            2,
            "he",
            2,
            0,
            ERROR_INSUFFICIENT_BUFFER
        },
        /* tests a buffer that runs out after writing 1 base64 character */
        {
            {0x4F60,0x0001,0},
            -1,
            2,
            "+T",
            2,
            0,
            ERROR_INSUFFICIENT_BUFFER
        },
        /* tests a buffer that runs out after writing 2 base64 characters */
        {
            {0x4F60,0x0001,0},
            -1,
            3,
            "+T2",
            3,
            0,
            ERROR_INSUFFICIENT_BUFFER
        },
        /* tests a buffer that runs out after writing 3 base64 characters */
        {
            {0x4F60,0x0001,0},
            -1,
            4,
            "+T2A",
            4,
            0,
            ERROR_INSUFFICIENT_BUFFER
        },
        /* tests a buffer that runs out just after writing the + sign */
        {
            {0x4F60,0},
            -1,
            1,
            "+",
            1,
            0,
            ERROR_INSUFFICIENT_BUFFER
        },
        /* tests a buffer that runs out just before writing the - sign */
        /* the number of bits to encode here is not evenly divisible by 6 */
        {
            {0x4F60,0},
            -1,
            4,
            "+T2",
            3,
            0,
            ERROR_INSUFFICIENT_BUFFER
        },
        /* tests a buffer that runs out just before writing the - sign */
        /* the number of bits to encode here is evenly divisible by 6 */
        {
            {0x4F60,0x597D,0x5417,0},
            -1,
            9,
            "+T2BZfVQX",
            9,
            0,
            ERROR_INSUFFICIENT_BUFFER
        },
        /* tests a buffer that runs out in the middle of escaping a + sign */
        {
            {'+',0},
            -1,
            1,
            "+",
            1,
            0,
            ERROR_INSUFFICIENT_BUFFER
        }
    };

    static const BOOL directly_encodable_table[] = {
        /* \0     \x01   \x02   \x03   \x04   \x05   \x06   \a   */
           TRUE,  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
        /* \b     \t     \n     \v     \f     \r     \x0E   \x0F */
           FALSE, TRUE,  TRUE,  FALSE, FALSE, TRUE,  FALSE, FALSE,
        /* \x10   \x11   \x12   \x13   \x14   \x15   \x16   \x17 */
           FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
        /* \x18   \x19   \x1A   \e     \x1C   \x1D   \x1E   \x1F */
           FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
        /*        !      "      #      $      %      &      '    */
           TRUE,  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE,
        /* (      )      *      +      ,      -      .      /    */
           TRUE,  TRUE,  FALSE, TRUE,  TRUE,  TRUE,  TRUE,  TRUE,
        /* 0      1      2      3      4      5      6      7    */
           TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,
        /* 8      9      :      ;      <      =      >      ?    */
           TRUE,  TRUE,  TRUE,  FALSE, FALSE, FALSE, FALSE, TRUE,
        /* @      A      B      C      D      E      F      G    */
           FALSE, TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,
        /* H      I      J      K      L      M      N      O    */
           TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,
        /* P      Q      R      S      T      U      V      W    */
           TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,
        /* X      Y      Z      [      \      ]      ^      _    */
           TRUE,  TRUE,  TRUE,  FALSE, FALSE, FALSE, FALSE, FALSE,
        /* `      a      b      c      d      e      f      g    */
           FALSE, TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,
        /* h      i      j      k      l      m      n      o    */
           TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,
        /* p      q      r      s      t      u      v      w    */
           TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,
        /* x      y      z                                       */
           TRUE,  TRUE,  TRUE
    };

    static const char base64_encoding_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    int i;

    for (i = 0; i < sizeof(utf16_to_utf7_tests) / sizeof(struct utf16_to_utf7_test); i++)
    {
        char c_buffer[1024];
        WCHAR w_buffer[1024];
        int len;

        c_buffer[sizeof(c_buffer) - 1] = 0;
        w_buffer[sizeof(w_buffer) / sizeof(WCHAR) - 1] = 0;

        /* test string conversion with srclen=-1 */
        memset(c_buffer, '#', sizeof(c_buffer) - 1);
        SetLastError(0xdeadbeef);
        len = WideCharToMultiByte(CP_UTF7, 0, utf16_to_utf7_tests[i].utf16, -1, c_buffer, sizeof(c_buffer), NULL, NULL);
        ok(len == utf16_to_utf7_tests[i].utf7_len &&
           strcmp(c_buffer, utf16_to_utf7_tests[i].utf7) == 0 &&
           c_buffer[len] == '#',
           "utf16_to_utf7_test failure i=%i dst=\"%s\" len=%i\n", i, c_buffer, len);
        ok(GetLastError() == 0xdeadbeef, "error=%x\n", GetLastError());

        /* test string conversion with srclen=-2 */
        memset(c_buffer, '#', sizeof(c_buffer) - 1);
        SetLastError(0xdeadbeef);
        len = WideCharToMultiByte(CP_UTF7, 0, utf16_to_utf7_tests[i].utf16, -2, c_buffer, sizeof(c_buffer), NULL, NULL);
        ok(len == utf16_to_utf7_tests[i].utf7_len &&
           strcmp(c_buffer, utf16_to_utf7_tests[i].utf7) == 0 &&
           c_buffer[len] == '#',
           "utf16_to_utf7_test failure i=%i dst=\"%s\" len=%i\n", i, c_buffer, len);
        ok(GetLastError() == 0xdeadbeef, "error=%x\n", GetLastError());

        /* test string conversion with dstlen=len-1 */
        memset(c_buffer, '#', sizeof(c_buffer) - 1);
        SetLastError(0xdeadbeef);
        len = WideCharToMultiByte(CP_UTF7, 0, utf16_to_utf7_tests[i].utf16, -1, c_buffer, utf16_to_utf7_tests[i].utf7_len - 1, NULL, NULL);
        ok(len == 0 &&
           memcmp(c_buffer, utf16_to_utf7_tests[i].utf7, utf16_to_utf7_tests[i].utf7_len - 1) == 0 &&
           c_buffer[utf16_to_utf7_tests[i].utf7_len - 1] == '#',
           "utf16_to_utf7_test failure i=%i dst=\"%s\" len=%i\n", i, c_buffer, len);
        ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "error=%x\n", GetLastError());

        /* test string conversion with dstlen=len */
        memset(c_buffer, '#', sizeof(c_buffer) - 1);
        SetLastError(0xdeadbeef);
        len = WideCharToMultiByte(CP_UTF7, 0, utf16_to_utf7_tests[i].utf16, -1, c_buffer, utf16_to_utf7_tests[i].utf7_len, NULL, NULL);
        ok(len == utf16_to_utf7_tests[i].utf7_len &&
           strcmp(c_buffer, utf16_to_utf7_tests[i].utf7) == 0 &&
           c_buffer[len] == '#',
           "utf16_to_utf7_test failure i=%i dst=\"%s\" len=%i\n", i, c_buffer, len);
        ok(GetLastError() == 0xdeadbeef, "error=%x\n", GetLastError());

        /* test string conversion with dstlen=len+1 */
        memset(c_buffer, '#', sizeof(c_buffer) - 1);
        SetLastError(0xdeadbeef);
        len = WideCharToMultiByte(CP_UTF7, 0, utf16_to_utf7_tests[i].utf16, -1, c_buffer, utf16_to_utf7_tests[i].utf7_len + 1, NULL, NULL);
        ok(len == utf16_to_utf7_tests[i].utf7_len &&
           strcmp(c_buffer, utf16_to_utf7_tests[i].utf7) == 0 &&
           c_buffer[len] == '#',
           "utf16_to_utf7_test failure i=%i dst=\"%s\" len=%i\n", i, c_buffer, len);
        ok(GetLastError() == 0xdeadbeef, "error=%x\n", GetLastError());

        /* test dry run with dst=NULL and dstlen=0 */
        memset(c_buffer, '#', sizeof(c_buffer));
        SetLastError(0xdeadbeef);
        len = WideCharToMultiByte(CP_UTF7, 0, utf16_to_utf7_tests[i].utf16, -1, NULL, 0, NULL, NULL);
        ok(len == utf16_to_utf7_tests[i].utf7_len &&
           c_buffer[0] == '#',
           "utf16_to_utf7_test failure i=%i len=%i\n", i, len);
        ok(GetLastError() == 0xdeadbeef, "error=%x\n", GetLastError());

        /* test dry run with dst!=NULL and dstlen=0 */
        memset(c_buffer, '#', sizeof(c_buffer) - 1);
        SetLastError(0xdeadbeef);
        len = WideCharToMultiByte(CP_UTF7, 0, utf16_to_utf7_tests[i].utf16, -1, c_buffer, 0, NULL, NULL);
        ok(len == utf16_to_utf7_tests[i].utf7_len &&
           c_buffer[0] == '#',
           "utf16_to_utf7_test failure i=%i len=%i\n", i, len);
        ok(GetLastError() == 0xdeadbeef, "error=%x\n", GetLastError());

        /* all simple utf16-to-utf7 tests can be reversed to make utf7-to-utf16 tests */
        memset(w_buffer, '#', sizeof(w_buffer) - sizeof(WCHAR));
        SetLastError(0xdeadbeef);
        len = MultiByteToWideChar(CP_UTF7, 0, utf16_to_utf7_tests[i].utf7, -1, w_buffer, sizeof(w_buffer) / sizeof(WCHAR));
        ok(len == utf16_to_utf7_tests[i].utf16_len &&
           memcmp(w_buffer, utf16_to_utf7_tests[i].utf16, len * sizeof(WCHAR)) == 0 &&
           w_buffer[len] == 0x2323,
           "utf16_to_utf7_test failure i=%i dst=%s len=%i\n", i, wine_dbgstr_w(w_buffer), len);
        ok(GetLastError() == 0xdeadbeef, "error=%x\n", GetLastError());
    }

    for (i = 0; i < sizeof(wcstombs_tests) / sizeof(struct wcstombs_test); i++)
    {
        char c_buffer[1024];
        int len;

        c_buffer[sizeof(c_buffer) - 1] = 0;
        memset(c_buffer, '#', sizeof(c_buffer) - 1);
        SetLastError(0xdeadbeef);

        len = WideCharToMultiByte(CP_UTF7, 0, wcstombs_tests[i].src, wcstombs_tests[i].srclen, c_buffer, wcstombs_tests[i].dstlen, NULL, NULL);
        ok(len == wcstombs_tests[i].len &&
           memcmp(c_buffer, wcstombs_tests[i].dst, wcstombs_tests[i].chars_written) == 0 &&
           c_buffer[wcstombs_tests[i].chars_written] == '#',
           "wcstombs_test failure i=%i len=%i dst=\"%s\"\n", i, len, c_buffer);
        ok(GetLastError() == wcstombs_tests[i].error, "error=%x\n", GetLastError());
    }

    /* test which characters are encoded if surrounded by non-encoded characters */
    for (i = 0; i <= 0xFFFF; i++)
    {
        WCHAR w_buffer[] = {' ',i,' ',0};
        char c_buffer[1024];
        int len;

        memset(c_buffer, '#', sizeof(c_buffer) - 1);
        c_buffer[sizeof(c_buffer) - 1] = 0;
        SetLastError(0xdeadbeef);

        len = WideCharToMultiByte(CP_UTF7, 0, w_buffer, sizeof(w_buffer) / sizeof(WCHAR), c_buffer, 1023, NULL, NULL);

        if (i == '+')
        {
            /* escapes */
            ok(len == 5 &&
               memcmp(c_buffer, " +- \0#", 6) == 0,
               "non-encoded surrounding characters failure i='+' len=%i dst=\"%s\"\n", len, c_buffer);
        }
        else if (i <= 'z' && directly_encodable_table[i])
        {
            /* encodes directly */
            ok(len == 4 &&
               c_buffer[0] == ' ' && c_buffer[1] == i && memcmp(c_buffer + 2, " \0#", 3) == 0,
               "non-encoded surrounding characters failure i=0x%04x len=%i dst=\"%s\"\n", i, len, c_buffer);
        }
        else
        {
            /* base64-encodes */
            ok(len == 8 &&
               memcmp(c_buffer, " +", 2) == 0 &&
               c_buffer[2] == base64_encoding_table[(i & 0xFC00) >> 10] &&
               c_buffer[3] == base64_encoding_table[(i & 0x03F0) >> 4] &&
               c_buffer[4] == base64_encoding_table[(i & 0x000F) << 2] &&
               memcmp(c_buffer + 5, "- \0#", 4) == 0,
               "non-encoded surrounding characters failure i=0x%04x len=%i dst=\"%s\" %c\n", i, len, c_buffer, base64_encoding_table[i & 0xFC00 >> 10]);
        }
        ok(GetLastError() == 0xdeadbeef, "error=%x\n", GetLastError());
    }

    /* test which one-byte characters are absorbed into surrounding base64 blocks */
    /* (Windows always ends the base64 block when it encounters a directly encodable character) */
    for (i = 0; i <= 0xFFFF; i++)
    {
        WCHAR w_buffer[] = {0x2672,i,0x2672,0};
        char c_buffer[1024];
        int len;

        memset(c_buffer, '#', sizeof(c_buffer) - 1);
        c_buffer[sizeof(c_buffer) - 1] = 0;
        SetLastError(0xdeadbeef);

        len = WideCharToMultiByte(CP_UTF7, 0, w_buffer, sizeof(w_buffer) / sizeof(WCHAR), c_buffer, 1023, NULL, NULL);

        if (i == '+')
        {
            /* escapes */
            ok(len == 13 &&
               memcmp(c_buffer, "+JnI-+-+JnI-\0#", 14) == 0,
               "encoded surrounding characters failure i='+' len=%i dst=\"%s\"\n", len, c_buffer);
        }
        else if (i <= 'z' && directly_encodable_table[i])
        {
            /* encodes directly */
            ok(len == 12 &&
               memcmp(c_buffer, "+JnI-", 5) == 0 && c_buffer[5] == i && memcmp(c_buffer + 6, "+JnI-\0#", 7) == 0,
               "encoded surrounding characters failure i=0x%04x len=%i dst=\"%s\"\n", i, len, c_buffer);
        }
        else
        {
            /* base64-encodes */
            ok(len == 11 &&
               memcmp(c_buffer, "+Jn", 3) == 0 &&
               c_buffer[3] == base64_encoding_table[8 | ((i & 0xC000) >> 14)] &&
               c_buffer[4] == base64_encoding_table[(i & 0x3F00) >> 8] &&
               c_buffer[5] == base64_encoding_table[(i & 0x00FC) >> 2] &&
               c_buffer[6] == base64_encoding_table[((i & 0x0003) << 4) | 2] &&
               memcmp(c_buffer + 7, "Zy-\0#", 5) == 0,
               "encoded surrounding characters failure i=0x%04x len=%i dst=\"%s\" %c\n", i, len, c_buffer, base64_encoding_table[8 | ((i & 0xC000) >> 14)]);
        }
        ok(GetLastError() == 0xdeadbeef, "error=%x\n", GetLastError());
    }
}

static void test_utf7_decoding(void)
{
    struct utf7_to_utf16_test
    {
        char utf7[1024];
        WCHAR utf16[1024];
        int utf16_len;
    };

    static const struct utf7_to_utf16_test utf7_to_utf16_tests[] = {
        /* the first 4 tests test ill-formed UTF-7 */
        /* they also test whether the unfinished byte pair is discarded or not */

        /* 6 bits, not enough for a byte pair */
        {
            "+T-+T-+T-hello",
            {'h','e','l','l','o',0},
            6
        },
        /* 12 bits, not enough for a byte pair */
        {
            "+T2-+T2-+T2-hello",
            {'h','e','l','l','o',0},
            6
        },
        /* 18 bits, not a multiple of 16 and the last bit is a 1 */
        {
            "+T2B-+T2B-+T2B-hello",
            {0x4F60,0x4F60,0x4F60,'h','e','l','l','o',0},
            9
        },
        /* 24 bits, a multiple of 8 but not a multiple of 16 */
        {
            "+T2BZ-+T2BZ-+T2BZ-hello",
            {0x4F60,0x4F60,0x4F60,'h','e','l','l','o',0},
            9
        },
        /* tests UTF-7 followed by characters that should be encoded but aren't */
        {
            "+T2BZ-\x82\xFE",
            {0x4F60,0x0082,0x00FE,0},
            4
        }
    };

    struct mbstowcs_test
    {
        /* inputs */
        char src[1024];
        int srclen;
        int dstlen;
        /* expected outputs */
        WCHAR dst[1024];
        int chars_written;
        int len;
        DWORD error;
    };

    static const struct mbstowcs_test mbstowcs_tests[] = {
        /* tests srclen > strlen(src) */
        {
            "a\0b",
            4,
            1023,
            {'a',0,'b',0},
            4,
            4,
            0xdeadbeef
        },
        /* tests srclen < strlen(src) outside of a UTF-7 sequence */
        {
            "hello",
            2,
            1023,
            {'h','e'},
            2,
            2,
            0xdeadbeef
        },
        /* tests srclen < strlen(src) inside of a UTF-7 sequence */
        {
            "+T2BZfQ-",
            4,
            1023,
            {0x4F60},
            1,
            1,
            0xdeadbeef
        },
        /* tests srclen < strlen(src) right at the beginning of a UTF-7 sequence */
        {
            "hi+T2A-",
            3,
            1023,
            {'h','i'},
            2,
            2,
            0xdeadbeef
        },
        /* tests srclen < strlen(src) right at the end of a UTF-7 sequence */
        {
            "+T2A-hi",
            5,
            1023,
            {0x4F60},
            1,
            1,
            0xdeadbeef
        },
        /* tests srclen < strlen(src) at the beginning of an escaped + sign */
        {
            "hi+-",
            3,
            1023,
            {'h','i'},
            2,
            2,
            0xdeadbeef
        },
        /* tests srclen < strlen(src) at the end of an escaped + sign */
        {
            "+-hi",
            2,
            1023,
            {'+'},
            1,
            1,
            0xdeadbeef
        },
        /* tests len=0 but no error */
        {
            "+",
            1,
            1023,
            {},
            0,
            0,
            0xdeadbeef
        },
        /* tests a buffer that runs out while not decoding a UTF-7 sequence */
        {
            "hello",
            -1,
            2,
            {'h','e'},
            2,
            0,
            ERROR_INSUFFICIENT_BUFFER
        },
        /* tests a buffer that runs out in the middle of decoding a UTF-7 sequence */
        {
            "+T2BZfQ-",
            -1,
            1,
            {0x4F60},
            1,
            0,
            ERROR_INSUFFICIENT_BUFFER
        }
    };

    static const char base64_decoding_table[] = {
        /* \0     \x01   \x02   \x03   \x04   \x05   \x06   \a   */
           -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
        /* \b     \t     \n     \v     \f     \r     \x0E   \x0F */
           -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
        /* \x10   \x11   \x12   \x13   \x14   \x15   \x16   \x17 */
           -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
        /* \x18   \x19   \x1A   \e     \x1C   \x1D   \x1E   \x1F */
           -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
        /*        !      "      #      $      %      &      '    */
           -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
        /* (      )      *      +      ,      -      .      /    */
           -1,    -1,    -1,    62,    -1,    -1,    -1,    63,
        /* 0      1      2      3      4      5      6      7    */
           52,    53,    54,    55,    56,    57,    58,    59,
        /* 8      9      :      ;      <      =      >      ?    */
           60,    61,    -1,    -1,    -1,    -1,    -1,    -1,
        /* @      A      B      C      D      E      F      G    */
           -1,    0,     1,     2,     3,     4,     5,     6,
        /* H      I      J      K      L      M      N      O    */
           7,     8,     9,     10,    11,    12,    13,    14,
        /* P      Q      R      S      T      U      V      W    */
           15,    16,    17,    18,    19,    20,    21,    22,
        /* X      Y      Z      [      \      ]      ^      _    */
           23,    24,    25,    -1,    -1,    -1,    -1,    -1,
        /* `      a      b      c      d      e      f      g    */
           -1,    26,    27,    28,    29,    30,    31,    32,
        /* h      i      j      k      l      m      n      o    */
           33,    34,    35,    36,    37,    38,    39,    40,
        /* p      q      r      s      t      u      v      w    */
           41,    42,    43,    44,    45,    46,    47,    48,
        /* x      y      z      {      |      }      ~      \x7F */
           49,    50,    51,    -1,    -1,    -1,    -1,    -1
    };

    int i;

    for (i = 0; i < sizeof(utf7_to_utf16_tests) / sizeof(struct utf7_to_utf16_test); i++)
    {
        WCHAR w_buffer[1024];
        int len;

        w_buffer[sizeof(w_buffer) / sizeof(WCHAR) - 1] = 0;

        /* test string conversion with srclen=-1 */
        memset(w_buffer, '#', sizeof(w_buffer) - sizeof(WCHAR));
        SetLastError(0xdeadbeef);
        len = MultiByteToWideChar(CP_UTF7, 0, utf7_to_utf16_tests[i].utf7, -1, w_buffer, sizeof(w_buffer) / sizeof(WCHAR));
        ok(len == utf7_to_utf16_tests[i].utf16_len &&
           winetest_strcmpW(w_buffer, utf7_to_utf16_tests[i].utf16) == 0 &&
           w_buffer[len] == 0x2323,
           "utf7_to_utf16_test failure i=%i dst=%s len=%i\n", i, wine_dbgstr_w(w_buffer), len);
        ok(GetLastError() == 0xdeadbeef, "error=%x\n", GetLastError());

        /* test string conversion with srclen=-2 */
        memset(w_buffer, '#', sizeof(w_buffer) - sizeof(WCHAR));
        SetLastError(0xdeadbeef);
        len = MultiByteToWideChar(CP_UTF7, 0, utf7_to_utf16_tests[i].utf7, -2, w_buffer, sizeof(w_buffer) / sizeof(WCHAR));
        ok(len == utf7_to_utf16_tests[i].utf16_len &&
           winetest_strcmpW(w_buffer, utf7_to_utf16_tests[i].utf16) == 0 &&
           w_buffer[len] == 0x2323,
           "utf7_to_utf16_test failure i=%i dst=%s len=%i\n", i, wine_dbgstr_w(w_buffer), len);
        ok(GetLastError() == 0xdeadbeef, "error=%x\n", GetLastError());

        /* test string conversion with dstlen=len-1 */
        memset(w_buffer, '#', sizeof(w_buffer) - sizeof(WCHAR));
        SetLastError(0xdeadbeef);
        len = MultiByteToWideChar(CP_UTF7, 0, utf7_to_utf16_tests[i].utf7, -1, w_buffer, utf7_to_utf16_tests[i].utf16_len - 1);
        ok(len == 0 &&
           memcmp(w_buffer, utf7_to_utf16_tests[i].utf16, (utf7_to_utf16_tests[i].utf16_len - 1) * sizeof(WCHAR)) == 0 &&
           w_buffer[utf7_to_utf16_tests[i].utf16_len - 1] == 0x2323,
           "utf7_to_utf16_test failure i=%i dst=%s len=%i\n", i, wine_dbgstr_w(w_buffer), len);
        ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "error=%x\n", GetLastError());

        /* test string conversion with dstlen=len */
        memset(w_buffer, '#', sizeof(w_buffer) - sizeof(WCHAR));
        SetLastError(0xdeadbeef);
        len = MultiByteToWideChar(CP_UTF7, 0, utf7_to_utf16_tests[i].utf7, -1, w_buffer, utf7_to_utf16_tests[i].utf16_len);
        ok(len == utf7_to_utf16_tests[i].utf16_len &&
           winetest_strcmpW(w_buffer, utf7_to_utf16_tests[i].utf16) == 0 &&
           w_buffer[len] == 0x2323,
           "utf7_to_utf16_test failure i=%i dst=%s len=%i\n", i, wine_dbgstr_w(w_buffer), len);
        ok(GetLastError() == 0xdeadbeef, "error=%x\n", GetLastError());

        /* test string conversion with dstlen=len+1 */
        memset(w_buffer, '#', sizeof(w_buffer) - sizeof(WCHAR));
        SetLastError(0xdeadbeef);
        len = MultiByteToWideChar(CP_UTF7, 0, utf7_to_utf16_tests[i].utf7, -1, w_buffer, utf7_to_utf16_tests[i].utf16_len + 1);
        ok(len == utf7_to_utf16_tests[i].utf16_len &&
           winetest_strcmpW(w_buffer, utf7_to_utf16_tests[i].utf16) == 0 &&
           w_buffer[len] == 0x2323,
           "utf7_to_utf16_test failure i=%i dst=%s len=%i\n", i, wine_dbgstr_w(w_buffer), len);
        ok(GetLastError() == 0xdeadbeef, "error=%x\n", GetLastError());

        /* test dry run with dst=NULL and dstlen=0 */
        memset(w_buffer, '#', sizeof(w_buffer) - sizeof(WCHAR));
        SetLastError(0xdeadbeef);
        len = MultiByteToWideChar(CP_UTF7, 0, utf7_to_utf16_tests[i].utf7, -1, NULL, 0);
        ok(len == utf7_to_utf16_tests[i].utf16_len &&
           w_buffer[0] == 0x2323,
           "utf7_to_utf16_test failure i=%i len=%i\n", i, len);
        ok(GetLastError() == 0xdeadbeef, "error=%x\n", GetLastError());

        /* test dry run with dst!=NULL and dstlen=0 */
        memset(w_buffer, '#', sizeof(w_buffer) - sizeof(WCHAR));
        SetLastError(0xdeadbeef);
        len = MultiByteToWideChar(CP_UTF7, 0, utf7_to_utf16_tests[i].utf7, -1, w_buffer, 0);
        ok(len == utf7_to_utf16_tests[i].utf16_len &&
           w_buffer[0] == 0x2323,
           "utf7_to_utf16_test failure i=%i len=%i\n", i, len);
        ok(GetLastError() == 0xdeadbeef, "error=%x\n", GetLastError());
    }

    for (i = 0; i < sizeof(mbstowcs_tests) / sizeof(struct mbstowcs_test); i++)
    {
        WCHAR w_buffer[1024];
        int len;

        w_buffer[sizeof(w_buffer) / sizeof(WCHAR) - 1] = 0;
        memset(w_buffer, '#', sizeof(w_buffer) - sizeof(WCHAR));
        SetLastError(0xdeadbeef);

        len = MultiByteToWideChar(CP_UTF7, 0, mbstowcs_tests[i].src, mbstowcs_tests[i].srclen, w_buffer, mbstowcs_tests[i].dstlen);
        ok(len == mbstowcs_tests[i].len &&
           memcmp(w_buffer, mbstowcs_tests[i].dst, mbstowcs_tests[i].chars_written * sizeof(WCHAR)) == 0 &&
           w_buffer[mbstowcs_tests[i].chars_written] == 0x2323,
           "mbstowcs_test failure i=%i len=%i dst=%s\n", i, len, wine_dbgstr_w(w_buffer));
        ok(GetLastError() == mbstowcs_tests[i].error, "error=%x\n", GetLastError());
    }

    /* test which one-byte characters remove stray + signs */
    for (i = 0; i < 256; i++)
    {
        char c_buffer[] = {'+',i,'+','A','A','A',0};
        WCHAR w_buffer[1024];
        int len;

        memset(w_buffer, '#', sizeof(w_buffer) - sizeof(WCHAR));
        w_buffer[sizeof(w_buffer) / sizeof(WCHAR) - 1] = 0;
        SetLastError(0xdeadbeef);

        len = MultiByteToWideChar(CP_UTF7, 0, c_buffer, sizeof(c_buffer), w_buffer, 1023);

        if (i == '-')
        {
            /* removes the - sign */
            ok(len == 3 && w_buffer[0] == '+' && w_buffer[1] == 0 && w_buffer[2] == 0 && w_buffer[3] == 0x2323,
               "stray + removal failure i=%i len=%i dst=%s\n", i, len, wine_dbgstr_w(w_buffer));
        }
        else if (i < 128 && base64_decoding_table[i] != -1)
        {
            /* absorbs the character into the base64 sequence */
            ok(len == 2 &&
               w_buffer[0] == ((base64_decoding_table[i] << 10) | 0x03E0) &&
               w_buffer[1] == 0x0000 && w_buffer[2] == 0x2323,
               "stray + removal failure i=%i len=%i dst=%s\n", i, len, wine_dbgstr_w(w_buffer));
        }
        else
        {
            /* removes the + sign */
            ok(len == 3 && w_buffer[0] == i && w_buffer[1] == 0 && w_buffer[2] == 0 && w_buffer[3] == 0x2323,
               "stray + removal failure i=%i len=%i dst=%s\n", i, len, wine_dbgstr_w(w_buffer));
        }
        ok(GetLastError() == 0xdeadbeef, "error=%x\n", GetLastError());
    }

    /* test which one-byte characters terminate a sequence */
    /* also test whether the unfinished byte pair is discarded or not */
    for (i = 0; i < 256; i++)
    {
        char c_buffer[] = {'+','B',i,'+','A','A','A',0};
        WCHAR w_buffer[1024];
        int len;

        memset(w_buffer, '#', sizeof(w_buffer) - sizeof(WCHAR));
        w_buffer[sizeof(w_buffer) / sizeof(WCHAR) - 1] = 0;
        SetLastError(0xdeadbeef);

        len = MultiByteToWideChar(CP_UTF7, 0, c_buffer, sizeof(c_buffer), w_buffer, 1023);

        if (i == '-')
        {
            /* explicitly terminates */
            ok(len == 2 &&
               w_buffer[0] == 0 && w_buffer[1] == 0 && w_buffer[2] == 0x2323,
               "implicit termination failure i=%i len=%i dst=%s\n", i, len, wine_dbgstr_w(w_buffer));
        }
        else if (i < 128)
        {
            if (base64_decoding_table[i] != -1)
            {
                /* absorbs the character into the base64 sequence */
                ok(len == 3 &&
                   w_buffer[0] == (0x0400 | (base64_decoding_table[i] << 4) | 0x000F) &&
                   w_buffer[1] == 0x8000 && w_buffer[2] == 0 && w_buffer[3] == 0x2323,
                   "implicit termination failure i=%i len=%i dst=%s\n", i, len, wine_dbgstr_w(w_buffer));
            }
            else
            {
                /* implicitly terminates and discards the unfinished byte pair */
                ok(len == 3 &&
                   w_buffer[0] == i && w_buffer[1] == 0 && w_buffer[2] == 0 && w_buffer[3] == 0x2323,
                   "implicit termination failure i=%i len=%i dst=%s\n", i, len, wine_dbgstr_w(w_buffer));
            }
        }
        else
        {
            /* implicitly terminates but does not the discard unfinished byte pair */
            ok(len == 3 &&
               w_buffer[0] == i && w_buffer[1] == 0x0400 && w_buffer[2] == 0 && w_buffer[3] == 0x2323,
               "implicit termination failure i=%i len=%i dst=%s\n", i, len, wine_dbgstr_w(w_buffer));
        }
        ok(GetLastError() == 0xdeadbeef, "error=%x\n", GetLastError());
    }
}

static void test_undefined_byte_char(void)
{
    static const struct tag_testset {
        INT codepage;
        LPCSTR str;
        BOOL is_error;
    } testset[] = {
        {  874, "\xdd", TRUE },
        {  932, "\xfe", TRUE },
        {  932, "\x80", FALSE },
        {  936, "\xff", TRUE },
        {  949, "\xff", TRUE },
        {  950, "\xff", TRUE },
        { 1252, "\x90", FALSE },
        { 1253, "\xaa", TRUE },
        { 1255, "\xff", TRUE },
        { 1257, "\xa5", TRUE },
    };
    INT i, ret;

    for (i = 0; i < (sizeof(testset) / sizeof(testset[0])); i++) {
        if (! IsValidCodePage(testset[i].codepage))
        {
            skip("Codepage %d not available\n", testset[i].codepage);
            continue;
        }

        SetLastError(0xdeadbeef);
        ret = MultiByteToWideChar(testset[i].codepage, MB_ERR_INVALID_CHARS,
                                  testset[i].str, -1, NULL, 0);
        if (testset[i].is_error) {
            ok(ret == 0 && GetLastError() == ERROR_NO_UNICODE_TRANSLATION,
               "ret is %d, GetLastError is %u (cp %d)\n",
               ret, GetLastError(), testset[i].codepage);
        }
        else {
            ok(ret == strlen(testset[i].str)+1 && GetLastError() == 0xdeadbeef,
               "ret is %d, GetLastError is %u (cp %d)\n",
               ret, GetLastError(), testset[i].codepage);
        }

        SetLastError(0xdeadbeef);
        ret = MultiByteToWideChar(testset[i].codepage, 0,
                                  testset[i].str, -1, NULL, 0);
        ok(ret == strlen(testset[i].str)+1 && GetLastError() == 0xdeadbeef,
           "ret is %d, GetLastError is %u (cp %d)\n",
           ret, GetLastError(), testset[i].codepage);
    }
}

static void test_threadcp(void)
{
    static const LCID ENGLISH  = MAKELCID(MAKELANGID(LANG_ENGLISH,  SUBLANG_ENGLISH_US),         SORT_DEFAULT);
    static const LCID HINDI    = MAKELCID(MAKELANGID(LANG_HINDI,    SUBLANG_HINDI_INDIA),        SORT_DEFAULT);
    static const LCID GEORGIAN = MAKELCID(MAKELANGID(LANG_GEORGIAN, SUBLANG_GEORGIAN_GEORGIA),   SORT_DEFAULT);
    static const LCID RUSSIAN  = MAKELCID(MAKELANGID(LANG_RUSSIAN,  SUBLANG_RUSSIAN_RUSSIA),     SORT_DEFAULT);
    static const LCID JAPANESE = MAKELCID(MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN),     SORT_DEFAULT);
    static const LCID CHINESE  = MAKELCID(MAKELANGID(LANG_CHINESE,  SUBLANG_CHINESE_SIMPLIFIED), SORT_DEFAULT);

    BOOL islead, islead_acp;
    CPINFOEXA cpi;
    UINT cp, acp;
    int  i, num;
    LCID last;
    BOOL ret;

    struct test {
        LCID lcid;
        UINT threadcp;
    } lcids[] = {
        { HINDI,    0    },
        { GEORGIAN, 0    },
        { ENGLISH,  1252 },
        { RUSSIAN,  1251 },
        { JAPANESE, 932  },
        { CHINESE,  936  }
    };

    struct test_islead_nocp {
        LCID lcid;
        BYTE testchar;
    } isleads_nocp[] = {
        { HINDI,    0x00 },
        { HINDI,    0x81 },
        { HINDI,    0xa0 },
        { HINDI,    0xe0 },

        { GEORGIAN, 0x00 },
        { GEORGIAN, 0x81 },
        { GEORGIAN, 0xa0 },
        { GEORGIAN, 0xe0 },
    };

    struct test_islead {
        LCID lcid;
        BYTE testchar;
        BOOL islead;
    } isleads[] = {
        { ENGLISH,  0x00, FALSE },
        { ENGLISH,  0x81, FALSE },
        { ENGLISH,  0xa0, FALSE },
        { ENGLISH,  0xe0, FALSE },

        { RUSSIAN,  0x00, FALSE },
        { RUSSIAN,  0x81, FALSE },
        { RUSSIAN,  0xa0, FALSE },
        { RUSSIAN,  0xe0, FALSE },

        { JAPANESE, 0x00, FALSE },
        { JAPANESE, 0x81,  TRUE },
        { JAPANESE, 0xa0, FALSE },
        { JAPANESE, 0xe0,  TRUE },

        { CHINESE,  0x00, FALSE },
        { CHINESE,  0x81,  TRUE },
        { CHINESE,  0xa0,  TRUE },
        { CHINESE,  0xe0,  TRUE },
    };

    last = GetThreadLocale();
    acp  = GetACP();

    for (i = 0; i < sizeof(lcids)/sizeof(lcids[0]); i++)
    {
        SetThreadLocale(lcids[i].lcid);

        cp = 0xdeadbeef;
        GetLocaleInfoA(lcids[i].lcid, LOCALE_IDEFAULTANSICODEPAGE|LOCALE_RETURN_NUMBER, (LPSTR)&cp, sizeof(cp));
        ok(cp == lcids[i].threadcp, "wrong codepage %u for lcid %04x, should be %u\n", cp, lcids[i].threadcp, cp);

        /* GetCPInfoEx/GetCPInfo - CP_ACP */
        SetLastError(0xdeadbeef);
        memset(&cpi, 0, sizeof(cpi));
        ret = GetCPInfoExA(CP_ACP, 0, &cpi);
        ok(ret, "GetCPInfoExA failed for lcid %04x, error %d\n", lcids[i].lcid, GetLastError());
        ok(cpi.CodePage == acp, "wrong codepage %u for lcid %04x, should be %u\n", cpi.CodePage, lcids[i].lcid, acp);

        /* WideCharToMultiByte - CP_ACP */
        num = WideCharToMultiByte(CP_ACP, 0, foobarW, -1, NULL, 0, NULL, NULL);
        ok(num == 7, "ret is %d (%04x)\n", num, lcids[i].lcid);

        /* MultiByteToWideChar - CP_ACP */
        num = MultiByteToWideChar(CP_ACP, 0, "foobar", -1, NULL, 0);
        ok(num == 7, "ret is %d (%04x)\n", num, lcids[i].lcid);

        /* GetCPInfoEx/GetCPInfo - CP_THREAD_ACP */
        SetLastError(0xdeadbeef);
        memset(&cpi, 0, sizeof(cpi));
        ret = GetCPInfoExA(CP_THREAD_ACP, 0, &cpi);
        ok(ret, "GetCPInfoExA failed for lcid %04x, error %d\n", lcids[i].lcid, GetLastError());
        if (lcids[i].threadcp)
            ok(cpi.CodePage == lcids[i].threadcp, "wrong codepage %u for lcid %04x, should be %u\n",
               cpi.CodePage, lcids[i].lcid, lcids[i].threadcp);
        else
            ok(cpi.CodePage == acp, "wrong codepage %u for lcid %04x, should be %u\n",
               cpi.CodePage, lcids[i].lcid, acp);

        /* WideCharToMultiByte - CP_THREAD_ACP */
        num = WideCharToMultiByte(CP_THREAD_ACP, 0, foobarW, -1, NULL, 0, NULL, NULL);
        ok(num == 7, "ret is %d (%04x)\n", num, lcids[i].lcid);

        /* MultiByteToWideChar - CP_THREAD_ACP */
        num = MultiByteToWideChar(CP_THREAD_ACP, 0, "foobar", -1, NULL, 0);
        ok(num == 7, "ret is %d (%04x)\n", num, lcids[i].lcid);
    }

    /* IsDBCSLeadByteEx - locales without codepage */
    for (i = 0; i < sizeof(isleads_nocp)/sizeof(isleads_nocp[0]); i++)
    {
        SetThreadLocale(isleads_nocp[i].lcid);

        islead_acp = IsDBCSLeadByteEx(CP_ACP,        isleads_nocp[i].testchar);
        islead     = IsDBCSLeadByteEx(CP_THREAD_ACP, isleads_nocp[i].testchar);

        ok(islead == islead_acp, "wrong islead %i for test char %x in lcid %04x.  should be %i\n",
            islead, isleads_nocp[i].testchar, isleads_nocp[i].lcid, islead_acp);
    }

    /* IsDBCSLeadByteEx - locales with codepage */
    for (i = 0; i < sizeof(isleads)/sizeof(isleads[0]); i++)
    {
        SetThreadLocale(isleads[i].lcid);

        islead = IsDBCSLeadByteEx(CP_THREAD_ACP, isleads[i].testchar);
        ok(islead == isleads[i].islead, "wrong islead %i for test char %x in lcid %04x.  should be %i\n",
            islead, isleads[i].testchar, isleads[i].lcid, isleads[i].islead);
    }

    SetThreadLocale(last);
}

START_TEST(codepage)
{
    BOOL bUsedDefaultChar;

    test_destination_buffer();
    test_null_source();
    test_negative_source_length();
    test_negative_dest_length();
    test_other_invalid_parameters();
    test_overlapped_buffers();

    /* WideCharToMultiByte has two code paths, test both here */
    test_string_conversion(NULL);
    test_string_conversion(&bUsedDefaultChar);

    test_utf7_encoding();
    test_utf7_decoding();

    test_undefined_byte_char();
    test_threadcp();
}
