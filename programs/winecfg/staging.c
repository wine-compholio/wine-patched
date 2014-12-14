/*
 * WineCfg Staging panel
 *
 * Copyright 2014 Michael Müller
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
 *
 */

#define COBJMACROS

#include "config.h"

#include <windows.h>
#include <wine/unicode.h>
#include <wine/debug.h>

#include "resource.h"
#include "winecfg.h"

WINE_DEFAULT_DEBUG_CHANNEL(winecfg);

static WCHAR redirects_keyW[] = {'S','o','f','t','w','a','r','e','\\','W','i','n','e','\\',
                                 'D','l','l','R','e','d','i','r','e','c','t','s',0};
static WCHAR wined3dW[] = {'w','i','n','e','d','3','d',0};
static WCHAR wined3d_csmtW[] = {'w','i','n','e','d','3','d','-','c','s','m','t','.','d','l','l',0};

static BOOL csmt_get(void)
{
    BOOL ret = FALSE;
    WCHAR *redirect = get_reg_keyW(HKEY_CURRENT_USER, redirects_keyW, wined3dW, NULL);
    if (!redirect) return FALSE;

    if (strcmpW(redirect, wined3d_csmtW) == 0)
        ret = TRUE;

    HeapFree(GetProcessHeap(), 0, redirect);
    return ret;
}

static void csmt_set(BOOL status)
{
    if (csmt_get() == status)
        return;

    set_reg_keyW(HKEY_CURRENT_USER, redirects_keyW, wined3dW, status ? wined3d_csmtW : NULL);
}

static void csmt_clicked(HWND dialog)
{
    csmt_set(IsDlgButtonChecked(dialog, IDC_ENABLE_CSMT) == BST_CHECKED);
}

static void initStagingDlg(HWND dialog)
{
    CheckDlgButton(dialog, IDC_ENABLE_CSMT, csmt_get() ? BST_CHECKED : BST_UNCHECKED);
}

INT_PTR CALLBACK StagingDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_NOTIFY:
        break;

    case WM_INITDIALOG:
        initStagingDlg(hDlg);
        return TRUE;

  case WM_SHOWWINDOW:
        set_window_title(hDlg);
        break;

    case WM_DESTROY:
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_ENABLE_CSMT && HIWORD(wParam) == BN_CLICKED)
        {
            csmt_clicked(hDlg);
            SendMessageW(GetParent(hDlg), PSM_CHANGED, 0, 0);
        }
        break;
    }
    return FALSE;
}
