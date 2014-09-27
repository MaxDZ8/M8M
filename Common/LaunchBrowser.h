/*
 * This code is released under the MIT license.
 * For conditions of distribution and use, see the LICENSE or hit the web.
 */
#pragma once
#include <exception>

#if defined(_WIN32)
#include <Windows.h>
#include <shellapi.h>
#define DIR_SEPARATOR L"\\"
#endif


void LaunchBrowser(const wchar_t *what);

void OpenFileExplorer(const wchar_t *folder);
