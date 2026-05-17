/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <ITimingClock.h>
#include <memory>
#include <d3d11.h>

// Forward declaration - HDRData is defined as struct in HDRData.h
class HDRData;

/**
 * Structure which represents a single video frame
 * 
 * Enhanced for 4K HDR60 with support for:
 * - D3D11 zero-copy texture sharing
 * - HDR metadata sidecar data
 * - Lock-free buffer management
 */
class VideoFrame
{
public:

	/**
	 * Buffer type enumeration
	 */
	enum class BufferType
	{
		CpuBuffer,      // Traditional CPU-backed buffer
		D3D11Texture    // D3D11 shared texture (zero-copy)
	};

	/**
	 * D3D11 texture metadata for zero-copy rendering
	 */
	struct D3D11TextureInfo
	{
		HANDLE sharedHandle;          // DXGI shared handle
		ID3D11Texture2D* texture;     // D3D11 texture pointer (optional, for direct access)
		UINT width;                   // Texture width
		UINT height;                  // Texture height
		DXGI_FORMAT format;           // Pixel format (NV12, P010, etc.)
		UINT stride;                  // Row stride in bytes
		UINT64 frameKey;              // Frame sequence key for synchronization
	};

	/**
	 * Constructor
	 *
	 * This is just a pointer to some data.
	 * If this data in any way, shape or form might be gone by the time it's used, you can use the
	 * sourceBuffer argument to have the VideoFrame constr/destr do ref management.
	 */
	VideoFrame() : m_data(nullptr), m_counter(0), m_timingTimestamp(0), 
	               m_sourceBuffer(nullptr), m_bufferType(BufferType::CpuBuffer) {}
	
	VideoFrame(
		const void* const data, uint64_t counter,
		timingclocktime_t timingTimestamp, IUnknown* sourceBuffer);
	
	/**
	 * Constructor for D3D11 zero-copy frames
	 */
	VideoFrame(
		const D3D11TextureInfo& textureInfo,
		timingclocktime_t timingTimestamp,
		std::shared_ptr<HDRData> hdrData = nullptr);

	/**
	 * Constructor with HDR metadata
	 */
	VideoFrame(
		const void* const data, uint64_t counter,
		timingclocktime_t timingTimestamp, IUnknown* sourceBuffer,
		std::shared_ptr<HDRData> hdrData);

	VideoFrame(const VideoFrame&);

	~VideoFrame();

	// Get frame data
	// If you're wondering where the size of GetData() is, it can be found by querying
	// VideoState::BytesPerFrame() which you should get before this gets delivered.
	const void* const GetData() const { return m_data; }

	// Get counter, this is monotoncally increasing from the capture source
	uint64_t GetCounter() const { return m_counter; }

	// Timestamp set by the timing clock.
	timingclocktime_t GetTimingTimestamp() const { return m_timingTimestamp; }

	// Get buffer type
	BufferType GetBufferType() const { return m_bufferType; }

	// Get D3D11 texture info (valid only if GetBufferType() == D3D11Texture)
	const D3D11TextureInfo* GetD3D11TextureInfo() const 
	{ 
		return m_bufferType == BufferType::D3D11Texture ? &m_d3d11Info : nullptr; 
	}

	// Get shared handle from D3D11 texture
	HANDLE GetD3D11SharedHandle() const 
	{
		return m_bufferType == BufferType::D3D11Texture ? m_d3d11Info.sharedHandle : INVALID_HANDLE_VALUE;
	}

	// Get HDR metadata (may be nullptr if not available)
	std::shared_ptr<HDRData> GetHDRData() const { return m_hdrData; }

	// Memory functions to hold onto the video buffer for longer
	void SourceBufferAddRef();
	void SourceBufferRelease();

	VideoFrame& operator= (const VideoFrame& videoFrame);

private:
	const void* m_data;
	uint64_t m_counter;
	timingclocktime_t m_timingTimestamp;
	IUnknown* m_sourceBuffer;
	
	// Buffer type indicator
	BufferType m_bufferType;
	
	// D3D11 texture information (used when m_bufferType == D3D11Texture)
	D3D11TextureInfo m_d3d11Info;
	
	// HDR metadata sidecar (optional)
	std::shared_ptr<HDRData> m_hdrData;
};
