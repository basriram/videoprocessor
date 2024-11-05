/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#pragma warning(disable : 26812)  // class enum over class in BM API


#include <map>

#include "LibMWCapture/MWHDMIPackets.h"
#include <MWFOURCC.h>

#include "MagewellProCaptureTranslate.h"
#include <LibMWCapture/MWCaptureExtension.h>

//
// Functions
//


ColorFormat TranslateColorFormat(HDMI_PXIEL_ENCODING pixelEncoding)
{
	switch (pixelEncoding) {
	case HDMI_ENCODING_RGB_444:
		return ColorFormat::RGB444;
	case HDMI_ENCODING_YUV_422:
		return ColorFormat::YCbCr422;
	case HDMI_ENCODING_YUV_420:
		return ColorFormat::YCbCr420;
	}
	throw std::runtime_error("Failed to translate BMDDetectedVideoInputFormatFlags to encoding");
}


BitDepth TranslateBitDepth(BYTE bitDepth)
{
	
	
	if (bitDepth == 8)
		return BitDepth::BITDEPTH_8BIT;

	if (bitDepth == 10)
		return BitDepth::BITDEPTH_10BIT;

	if (bitDepth == 12)
		return BitDepth::BITDEPTH_12BIT;

	throw std::runtime_error("Failed to translate bit Depth");
}


VideoFrameEncoding TranslateFrameEncoding(DWORD dwFOURCC)
{
	switch (dwFOURCC)
	{
	case MWFOURCC_Y800:
	case MWFOURCC_Y8:
		// Note this can also be HDYC depending on colorspace (REC709), not implemented
		return VideoFrameEncoding::UYVY;
 	case MWFOURCC_YUY2:
		return VideoFrameEncoding::HDYC;

	case MWFOURCC_P010:
		return VideoFrameEncoding::P010;
	case MWFOURCC_P210:
		return VideoFrameEncoding::V210;
	case MWFOURCC_NV12:
		return VideoFrameEncoding::UNKNOWN;
	case MWFOURCC_ARGB:
		return VideoFrameEncoding::ARGB_8BIT;

	case MWFOURCC_BGRA:
		return VideoFrameEncoding::BGRA_8BIT;

	case MWFOURCC_RGB10:
		return VideoFrameEncoding::R210;

	}

	return VideoFrameEncoding::UNKNOWN;
}


EOTF TranslateEOTF(LONGLONG electroOpticalTransferFuncValue)
{
	// 3 bit value
	assert(electroOpticalTransferFuncValue >= 0);
	assert(electroOpticalTransferFuncValue <= 7);

	// Comments in the SDK 12 docs: "EOTF in range 0-7 as per CEA 861.3",
	// which is "A2016 HDR STATIC METADATA EXTENSIONS".
	switch (electroOpticalTransferFuncValue)
	{
	case 0:
		return EOTF::SDR;
	case 1:
		return EOTF::HDR;

	case 2:
		return EOTF::PQ;

	case 3:
		return EOTF::HLG;

	// 4-7 are reserved for future use
	}

	throw std::runtime_error("Failed to translate EOTF");
}


ColorSpace TranslateColorSpace(MWCAP_VIDEO_COLOR_FORMAT mwColorFormat)
{
	if (mwColorFormat & MWCAP_VIDEO_COLOR_FORMAT_YUV601)
		return ColorSpace::REC_601_625;

	if (mwColorFormat & MWCAP_VIDEO_COLOR_FORMAT_YUV709 || mwColorFormat & MWCAP_VIDEO_COLOR_FORMAT_RGB)
		return ColorSpace::REC_709;

	if  (mwColorFormat & MWCAP_VIDEO_COLOR_FORMAT_YUV2020 || mwColorFormat & MWCAP_VIDEO_COLOR_FORMAT_YUV2020C)
		return  ColorSpace::BT_2020;

	return ColorSpace::UNKNOWN;
}


DisplayModeSharedPtr TranslateDisplayMode(int cx, int cy, bool bInterlaced, DWORD dwFrameDuration)
{
	return std::make_shared<DisplayMode>(
		cx,
		cy,
		bInterlaced,  // Interlaced?
		(bInterlaced == TRUE) ? 20000000LL: 10000000LL,
		dwFrameDuration);
}


double FPS(DWORD dwFrameDuration, bool bInterlaced)
{
	return (bInterlaced == TRUE) ? (double)20000000LL / dwFrameDuration : (double)10000000LL / dwFrameDuration;
}


// https://discourse.coreelec.org/t/edid-override-injecting-a-dolby-vsvdb-block/51510/474?page=24
double TranslatePrimaries(BYTE lsb, BYTE msb)
{
	double color=0;
	color = lsb;
	color += msb << 8;
	return color/50000.0;
}


double TranslateLuminance(BYTE lsb, BYTE msb)
{
	double lumen = 0;
	lumen = lsb;
	lumen += msb << 8;
	return lumen/1.0;
}


