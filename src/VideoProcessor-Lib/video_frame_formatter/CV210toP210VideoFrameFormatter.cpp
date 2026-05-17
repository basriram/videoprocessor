/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include <span>
#include <stdexcept>
#include "CV210toP210VideoFrameFormatter.h"

//
// Parts of this are copied from ffmpeg v210dec.c, see /3rdparty/ffmpeg/README.txt for license and attribution
//

#define PIXELS_PER_PACK 6
#define BYTES_PER_PACK (4 * sizeof(uint32_t))


void CV210toP210VideoFrameFormatter::OnVideoState(VideoStateComPtr& videoState)
{
	if (!videoState)
		throw std::runtime_error("Null video state is not allowed");

    if (videoState->videoFrameEncoding != VideoFrameEncoding::V210)
        throw std::runtime_error("Can only handle V210 input");

    m_height = videoState->displayMode->FrameHeight();
    if (m_height % 2 != 0)
        throw std::runtime_error("P010 output needs an even amount of input lines");

    m_width = videoState->displayMode->FrameWidth();
	if (m_width % 6 != 0)
		throw std::runtime_error("Can only handle conversions which align with V210 boundry (6 pixels)");

    const uint32_t bytes = videoState->BytesPerFrame();
    const uint32_t expectedBytes =
        videoState->displayMode->FrameHeight() *
        (videoState->displayMode->FrameWidth() / PIXELS_PER_PACK * BYTES_PER_PACK);

    if(bytes != expectedBytes)
        throw std::runtime_error("Unexpected amount of bytes for frame");
}


bool CV210toP210VideoFrameFormatter::FormatVideoFrame(
	const VideoFrame& inFrame,
	BYTE* outBuffer)
{
	// Read V210
	// https://wiki.multimedia.cx/index.php/V210

	// Write P210
    // 10bpp per component, data in the high bits, zeros in the low bits (we assume little-endian native)
	// https://docs.microsoft.com/en-us/windows/win32/medfound/10-bit-and-16-bit-yuv-video-formats

    const uint32_t pixels = m_height * m_width;
    const uint32_t aligned_width = ((m_width + 47) / 48) * 48;
    const uint32_t stride = aligned_width * 8 / 3;

    // Use std::span for safer array access instead of raw pointer arithmetic
    const std::span<const uint32_t> srcSpan(
        static_cast<const uint32_t*>(inFrame.GetData()),
        (pixels / PIXELS_PER_PACK * BYTES_PER_PACK * m_height) / sizeof(uint32_t));

    // Create output spans for Y and UV planes
    std::span<uint16_t> dstYSpan(reinterpret_cast<uint16_t*>(outBuffer), pixels);
    std::span<uint16_t> dstUVSpan(
        reinterpret_cast<uint16_t*>(outBuffer + static_cast<std::ptrdiff_t>(pixels * sizeof(uint16_t))),
        pixels);

    const uint32_t packsPerLine = m_width / PIXELS_PER_PACK;
    const uint32_t srcStride = stride / sizeof(uint32_t);  // Convert byte stride to uint32_t stride
    const uint32_t dstYStride = m_width;
    const uint32_t dstUVStride = m_width;

    for (uint32_t line = 0; line < m_height; line++)
    {
        const uint32_t srcLineOffset = line * srcStride;

        // Output indices for Y and UV planes
        uint32_t dstYIdx = line * dstYStride;
        uint32_t dstUVIdx = (line / 2) * dstUVStride;

        for (uint32_t pack = 0; pack < packsPerLine; pack++)
        {
            uint32_t val;
            uint16_t u, y1, y2, v;

            // Read 6 pixels per pack using std::span indexing
            val = srcSpan[srcLineOffset + pack * 4 + 0];
            u = static_cast<uint16_t>(val & 0x3FF);
            y1 = static_cast<uint16_t>((val >> 10) & 0x3FF);
            v = static_cast<uint16_t>((val >> 20) & 0x3FF);

            val = srcSpan[srcLineOffset + pack * 4 + 1];
            y1 = static_cast<uint16_t>(val & 0x3FF);
            u = static_cast<uint16_t>((val >> 10) & 0x3FF);
            y2 = static_cast<uint16_t>((val >> 20) & 0x3FF);

            val = srcSpan[srcLineOffset + pack * 4 + 2];
            v = static_cast<uint16_t>(val & 0x3FF);
            y1 = static_cast<uint16_t>((val >> 10) & 0x3FF);
            u = static_cast<uint16_t>((val >> 20) & 0x3FF);

            val = srcSpan[srcLineOffset + pack * 4 + 3];
            y1 = static_cast<uint16_t>(val & 0x3FF);
            v = static_cast<uint16_t>((val >> 10) & 0x3FF);
            y2 = static_cast<uint16_t>((val >> 20) & 0x3FF);

            // P210: interleaved Y and UV for all lines (unlike P010 which has UV only on even lines)
            dstUVSpan[dstUVIdx++] = static_cast<uint16_t>(u << 6);
            dstYSpan[dstYIdx++] = static_cast<uint16_t>(y1 << 6);
            dstUVSpan[dstUVIdx++] = static_cast<uint16_t>(v << 6);

            dstYSpan[dstYIdx++] = static_cast<uint16_t>(y1 << 6);
            dstUVSpan[dstUVIdx++] = static_cast<uint16_t>(u << 6);
            dstYSpan[dstYIdx++] = static_cast<uint16_t>(y2 << 6);

            dstUVSpan[dstUVIdx++] = static_cast<uint16_t>(v << 6);
            dstYSpan[dstYIdx++] = static_cast<uint16_t>(y1 << 6);
            dstUVSpan[dstUVIdx++] = static_cast<uint16_t>(u << 6);

            dstYSpan[dstYIdx++] = static_cast<uint16_t>(y1 << 6);
            dstUVSpan[dstUVIdx++] = static_cast<uint16_t>(v << 6);
            dstYSpan[dstYIdx++] = static_cast<uint16_t>(y2 << 6);
        }
    }

	return true;
}


LONG CV210toP210VideoFrameFormatter::GetOutFrameSize() const
{
    const LONG pixels = m_height * m_width;

    return
        (pixels * sizeof(uint16_t)) +  // Every pixel 1 y
        (pixels / 2 * (2 * sizeof(uint16_t)));  // Every 2 pixels 2 16-bit numbers
}
