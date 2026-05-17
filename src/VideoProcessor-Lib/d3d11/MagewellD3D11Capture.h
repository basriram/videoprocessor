/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <d3d11.h>
#include <functional>
#include "D3D11TexturePool.h"

// Forward declarations for Magewell SDK types
// These are defined in magewell_pro_capture.h which should be included by the caller
typedef void* MWChannelHandle;
// HNOTIFY is defined in the Magewell SDK header (MWCaptureDef.h)
// Do not redefine it here - it will be included by the implementation file

/**
 * MagewellD3D11Capture - Zero-copy capture bridge for Magewell SDK and D3D11 shared surfaces.
 * 
 * This class enables direct capture from Magewell hardware into D3D11 textures
 * that can be shared with madVR renderer without any CPU memory copies.
 * 
 * Key features:
 * - Direct Magewell capture into D3D11 textures via MWPinVideoBuffer
 * - Shared DXGI handles for cross-process access by madVR
 * - Automatic format detection (NV12/P010) based on input signal
 * - Keyed mutex synchronization for frame delivery
 */
class MagewellD3D11Capture
{
public:
    /**
     * Frame callback signature for delivering captured frames
     */
    typedef std::function<void(UINT textureIndex, ULONGLONG timestamp, DWORD width, DWORD height)> FrameCallback;

    MagewellD3D11Capture();
    ~MagewellD3D11Capture();

    /**
     * Initialize D3D11 capture pipeline
     * @param width - Frame width
     * @param height - Frame height
     * @param fourcc - Pixel format (P010, NV12, etc.)
     * @param callback - Frame delivery callback
     * @return S_OK on success
     */
    HRESULT Initialize(UINT width, UINT height, DWORD fourcc, FrameCallback callback);

    /**
     * Start capture
     * @return S_OK on success
     */
    HRESULT StartCapture();

    /**
     * Stop capture
     */
    void StopCapture();

    /**
     * Check if capture is active
     */
    bool IsCapturing() const { return m_capturing; }

    /**
     * Get D3D11 device
     */
    ID3D11Device* GetDevice() const { return m_texturePool.GetDevice(); }

    /**
     * Get texture shared handle for external access
     * @param index - Texture index
     * @return Shared handle
     */
    HANDLE GetTextureSharedHandle(UINT index) const;

    /**
     * Get texture at index (for internal use)
     */
    D3D11TexturePool::SharedTexture GetTexture(UINT index);

    /**
     * Get DXGI format
     */
    DXGI_FORMAT GetDXGIFormat() const { return m_dxgiFormat; }

    /**
     * Get frame width
     */
    UINT GetWidth() const { return m_width; }

    /**
     * Get frame height
     */
    UINT GetHeight() const { return m_height; }

    /**
     * Get pixel format
     */
    DWORD GetFourCC() const { return m_fourcc; }

    /**
     * Acquire keyed mutex for texture (if enabled)
     * @param index - Texture index
     * @param timeoutMs - Timeout in milliseconds
     * @return S_OK if mutex acquired
     */
    HRESULT AcquireMutex(UINT index, DWORD timeoutMs = 1000);

    /**
     * Release keyed mutex for texture
     * @param index - Texture index
     * @return S_OK on success
     */
    HRESULT ReleaseMutex(UINT index);

    /**
     * Set the Magewell channel handle for capture
     * @param channelHandle - Magewell channel handle
     */
    void SetChannelHandle(MWChannelHandle channelHandle) { m_channelHandle = channelHandle; }

    /**
     * Get the Magewell channel handle
     */
    MWChannelHandle GetChannelHandle() const { return m_channelHandle; }

private:
    HRESULT CreateTexturePool();
    HRESULT PinTextures();
    void UnpinTextures();
    void ProcessCapturedFrame();

    D3D11TexturePool m_texturePool;
    FrameCallback m_frameCallback;
    
    MWChannelHandle m_channelHandle = nullptr;
    HANDLE m_captureEvent = nullptr;
    HNOTIFY m_notifyHandle = nullptr;
    
    UINT m_width = 0;
    UINT m_height = 0;
    DWORD m_fourcc = 0;
    DXGI_FORMAT m_dxgiFormat = DXGI_FORMAT_UNKNOWN;
    
    bool m_capturing = false;
    bool m_initialized = false;
    
    // Current frame index for callback
    UINT m_currentFrameIndex = 0;
};