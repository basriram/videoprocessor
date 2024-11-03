/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <ColorFormat.h>
#include <BitDepth.h>
#include <VideoFrameEncoding.h>
#include <EOTF.h>
#include <ColorSpace.h>
#include <DisplayMode.h>
#include <LibMWCapture/MWCaptureDef.h>
#include <LibMWCapture/MWCaptureExtension.h>
#include <LibMWCapture/MWHDMIPackets.h>

//ColorFormat TranslateColorFormat(BMDDetectedVideoInputFormatFlags detectedVideoInputFormatFlagsValue);

BitDepth TranslateBithDepth(DWORD dwFOURCC);

VideoFrameEncoding TranslateFrameEncoding(DWORD dwFOURCC);

EOTF TranslateEOTF(LONGLONG electroOpticalTransferFuncValue);
ColorSpace TranslateColorSpace(MWCAP_VIDEO_SIGNAL_STATUS video_signal_status);
ColorSpace TranslateColorSpace(MWCAP_VIDEO_COLOR_FORMAT mwColorFormat);

DisplayModeSharedPtr TranslateDisplayMode(MWCAP_VIDEO_SIGNAL_STATUS video_signal_status);

double FPS(MWCAP_VIDEO_SIGNAL_STATUS video_signal_status);
double TranslatePrimaries(BYTE lsb, BYTE msb);
double TranslateLuminance(BYTE lsb, BYTE msb);