/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <WinSDKVer.h>


// Phase 3: Target Windows 8+ for WaitOnAddress API (futex-style synchronization)
// https://docs.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitonaddress
#define NTDDI_VERSION NTDDI_WIN8
#define _WIN32_WINNT _WIN32_WINNT_WIN8


#include <SDKDDKVer.h>
