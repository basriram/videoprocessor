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

/*
ColorFormat TranslateColorFormat(MWCAP_VIDEO_SIGNAL_STATUS video_signal_status)
{
	if (detectedVideoInputFormatFlagsValue & bmdDetectedVideoInputYCbCr422)
		return ColorFormat::YCbCr422;

	if (detectedVideoInputFormatFlagsValue & bmdDetectedVideoInputRGB444)
		return ColorFormat::RGB444;

	throw std::runtime_error("Failed to translate BMDDetectedVideoInputFormatFlags to encoding");
}
*/


BitDepth TranslateBithDepth(DWORD dwFOURCC)
{
	int bitDepth = FOURCC_GetBpp(dwFOURCC);
	if (bitDepth == 8)
		return BitDepth::BITDEPTH_8BIT;

	if (bitDepth == 10)
		return BitDepth::BITDEPTH_10BIT;

	if (bitDepth == 12)
		return BitDepth::BITDEPTH_12BIT;

	throw std::runtime_error("Failed to translate BMDDetectedVideoInputFormatFlags to bit depth");
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
	case MWFOURCC_P210:
		return VideoFrameEncoding::P010;
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
		//return EOTF::SDR;
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


ColorSpace TranslateColorSpace(MWCAP_VIDEO_SIGNAL_STATUS video_signal_status)
{
	MWCAP_VIDEO_COLOR_FORMAT mwColorFormat = video_signal_status.colorFormat;
	if (mwColorFormat & MWCAP_VIDEO_COLOR_FORMAT_YUV601)
		return ColorSpace::REC_601_625;

	if (mwColorFormat & MWCAP_VIDEO_COLOR_FORMAT_YUV709 || mwColorFormat & MWCAP_VIDEO_COLOR_FORMAT_RGB)
		return ColorSpace::REC_709;

	if (mwColorFormat & MWCAP_VIDEO_COLOR_FORMAT_YUV2020 || mwColorFormat & MWCAP_VIDEO_COLOR_FORMAT_YUV2020C)
		return  ColorSpace::BT_2020;

	return ColorSpace::UNKNOWN;

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


DisplayModeSharedPtr TranslateDisplayMode(MWCAP_VIDEO_SIGNAL_STATUS video_signal_status)
{
	printf("Input signal resolution: %d x %d\n", video_signal_status.cx, video_signal_status.cy);
	double fps = (video_signal_status.bInterlaced == TRUE) ? (double)20000000LL / video_signal_status.dwFrameDuration : (double)10000000LL / video_signal_status.dwFrameDuration;
	printf("Input signal fps: %.2f\n", fps);
	printf("Input signal interlaced: %d\n", video_signal_status.bInterlaced);
	printf("Input signal frame segmented: %d\n", video_signal_status.bSegmentedFrame);
	return std::make_shared<DisplayMode>(
		video_signal_status.cx,
		video_signal_status.cy,
		video_signal_status.bInterlaced,  // Interlaced?
		(video_signal_status.bInterlaced == TRUE) ? 20000000LL: 10000000LL,
		video_signal_status.dwFrameDuration);
}


double FPS(MWCAP_VIDEO_SIGNAL_STATUS video_signal_status)
{
	return (video_signal_status.bInterlaced == TRUE) ? (double)20000000LL / video_signal_status.dwFrameDuration : (double)10000000LL / video_signal_status.dwFrameDuration;
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


