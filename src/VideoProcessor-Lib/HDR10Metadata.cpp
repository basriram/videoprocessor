/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 * Additional HDR10 metadata support by VideoProcessor Team
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include <guiddef.h>
#include "HDR10Metadata.h"

// Define the HDR10 metadata GUID (must be in exactly one .cpp file)
// Note: IID_MediaSideDataHDR10Plus is already defined in LAVFilters, so we only define HDR10 static metadata
EXTERN_C const GUID IID_MediaSideDataHDR10 = 
    { 0xa5b3c7d9, 0x1234, 0x5678, { 0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78 } };
