/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>
#include <vector>
#include <mutex>
#include <atomic>

#include "CBufferedLiveSourceVideoOutputPin.h"

/**
 * CD3D11LiveSourceVideoOutputPin - DirectShow output pin with D3D11 zero-copy support.
 * 
 * This class extends CBufferedLiveSourceVideoOutputPin to support D3D11 shared textures
 * for zero-copy delivery to madVR renderer. When a VideoFrame contains a D3D11 texture,
 * this pin delivers the shared handle directly instead of copying to CPU memory.
 * 
 * Key features:
 * - Zero-copy D3D11 texture delivery to madVR
 * - DXGI shared handle support for cross-process access
 * - Keyed mutex synchronization for frame timing
 * - Fallback to CPU buffer for non-D3D11 frames
 */
class CD3D11LiveSourceVideoOutputPin :
    public CBufferedLiveSourceVideoOutputPin
{
public:

    CD3D11LiveSourceVideoOutputPin(
        CLiveSource* filter,
        CCritSec* pLock,
        HRESULT* phr);
    virtual ~CD3D11LiveSourceVideoOutputPin() override;

    // Initialize D3D11 support
    HRESULT InitializeD3D11(
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        UINT poolSize = 3);

    // Get D3D11 device
    ID3D11Device* GetD3D11Device() const { return m_d3d11Device; }

    // Check if D3D11 is initialized
    bool IsD3D11Initialized() const { return m_d3d11Device != nullptr; }

    // Get DXGI format
    DXGI_FORMAT GetDXGIFormat() const { return m_dxgiFormat; }

protected:

    // CBaseOutputPin overrides
    HRESULT DecideAllocator(IMemInputPin* pPin, IMemAllocator** ppAlloc);
    HRESULT DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* ppropInputRequest);

    // Render D3D11 frame into sample
    HRESULT RenderD3D11FrameIntoSample(VideoFrame& videoFrame, IMediaSample* pSample);

    // Get shared handle from VideoFrame
    HANDLE GetSharedHandleFromFrame(const VideoFrame& videoFrame);

private:

    // D3D11 device and context
    ID3D11Device* m_d3d11Device = nullptr;
    ID3D11DeviceContext* m_d3d11Context = nullptr;
    bool m_ownedDevice = false;

    // DXGI format
    DXGI_FORMAT m_dxgiFormat = DXGI_FORMAT_UNKNOWN;

    // Frame dimensions
    UINT m_width = 0;
    UINT m_height = 0;

    // Texture pool for shared handles
    std::vector<HANDLE> m_sharedHandles;
    std::vector<ID3D11Texture2D*> m_textures;
    std::mutex m_textureMutex;

    // Current texture index for round-robin
    std::atomic<UINT> m_currentTextureIndex{ 0 };

    // Create D3D11 device if needed
    HRESULT CreateD3D11Device();

    // Create texture pool
    HRESULT CreateTexturePool();

    // Release texture pool
    void ReleaseTexturePool();

    // Current shared handle for zero-copy delivery
    HANDLE m_currentSharedHandle = INVALID_HANDLE_VALUE;
};
