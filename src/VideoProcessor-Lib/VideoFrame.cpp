/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include <assert.h>

#include "VideoFrame.h"
#include "HDRData.h"


VideoFrame::VideoFrame(
	const void* data, uint64_t counter,
	timingclocktime_t timingTimestamp, IUnknown* sourceBuffer):
	m_data(data),
	m_counter(counter),
	m_timingTimestamp(timingTimestamp),
	m_sourceBuffer(sourceBuffer),
	m_bufferType(BufferType::CpuBuffer)
{
	assert(data);
}

/**
 * Constructor for D3D11 zero-copy frames
 */
VideoFrame::VideoFrame(
	const D3D11TextureInfo& textureInfo,
	timingclocktime_t timingTimestamp,
	std::shared_ptr<HDRData> hdrData) :
	m_data(nullptr),
	m_counter(0),
	m_timingTimestamp(timingTimestamp),
	m_sourceBuffer(nullptr),
	m_bufferType(BufferType::D3D11Texture),
	m_d3d11Info(textureInfo),
	m_hdrData(hdrData)
{
	assert(textureInfo.sharedHandle != INVALID_HANDLE_VALUE);
}

/**
 * Constructor with HDR metadata
 */
VideoFrame::VideoFrame(
	const void* const data, uint64_t counter,
	timingclocktime_t timingTimestamp, IUnknown* sourceBuffer,
	std::shared_ptr<HDRData> hdrData) :
	m_data(data),
	m_counter(counter),
	m_timingTimestamp(timingTimestamp),
	m_sourceBuffer(sourceBuffer),
	m_bufferType(BufferType::CpuBuffer),
	m_hdrData(hdrData)
{
	assert(data);
}

VideoFrame::VideoFrame(const VideoFrame& videoFrame) :
	m_data(videoFrame.m_data),
	m_counter(videoFrame.m_counter),
	m_timingTimestamp(videoFrame.m_timingTimestamp),
	m_sourceBuffer(videoFrame.m_sourceBuffer),
	m_bufferType(videoFrame.m_bufferType),
	m_d3d11Info(videoFrame.m_d3d11Info),
	m_hdrData(videoFrame.m_hdrData)
{
	// If this is a D3D11 texture, addRef the texture
	if (m_bufferType == BufferType::D3D11Texture && m_d3d11Info.texture)
	{
		m_d3d11Info.texture->AddRef();
	}
}


VideoFrame::~VideoFrame()
{
	// Release D3D11 texture if we own it
	if (m_bufferType == BufferType::D3D11Texture && m_d3d11Info.texture)
	{
		m_d3d11Info.texture->Release();
		m_d3d11Info.texture = nullptr;
	}
}


void VideoFrame::SourceBufferAddRef()
{
	if (m_sourceBuffer!=NULL)
		m_sourceBuffer->AddRef();
}


void VideoFrame::SourceBufferRelease()
{
	if (m_sourceBuffer != NULL) {
		const ULONG refCount = m_sourceBuffer->Release();
		//assert(refCount == 0);
	}
}


VideoFrame& VideoFrame::operator= (const VideoFrame& videoFrame)
{
	assert(this != &videoFrame);

	m_data = videoFrame.m_data;
	m_counter = videoFrame.m_counter;
	m_timingTimestamp = videoFrame.m_timingTimestamp;
	m_sourceBuffer = videoFrame.m_sourceBuffer;

	return *this;
}
