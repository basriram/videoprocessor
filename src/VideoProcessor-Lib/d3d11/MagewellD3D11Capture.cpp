/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

// Must include Magewell SDK before the header that uses HNOTIFY
#include <LibMWCapture/MWCapture.h>

#include "MagewellD3D11Capture.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// Forward declare ID3D11KeyedMutex for keyed mutex operations
// This interface is defined in d3d11.h but we use it via void* to avoid header conflicts
extern "C" {
    struct ID3D11KeyedMutex : public IUnknown
    {
        STDMETHOD(AcquireSync)(UINT64 key, DWORD msec) = 0;
        STDMETHOD(ReleaseSync)(UINT64 key) = 0;
    };
}

MagewellD3D11Capture::MagewellD3D11Capture()
{
}

MagewellD3D11Capture::~MagewellD3D11Capture()
{
    StopCapture();
}

HRESULT MagewellD3D11Capture::Initialize(UINT width, UINT height, DWORD fourcc, FrameCallback callback)
{
    if (width == 0 || height == 0)
    {
        return E_INVALIDARG;
    }

    m_width = width;
    m_height = height;
    m_fourcc = fourcc;
    m_frameCallback = callback;

    // Determine DXGI format based on FourCC
    m_dxgiFormat = D3D11TexturePool::GetDXGIFormat(fourcc);
    if (m_dxgiFormat == DXGI_FORMAT_UNKNOWN)
    {
        m_dxgiFormat = DXGI_FORMAT_NV12;  // Default fallback
    }

    // Configure texture pool for zero-copy capture
    D3D11TexturePool::TextureConfig config;
    config.width = width;
    config.height = height;
    config.format = m_dxgiFormat;
    config.poolSize = 3;  // Triple buffering for smooth 4K60
    config.useKeyedMutex = true;  // Enable keyed mutex for synchronization

    HRESULT hr = m_texturePool.Initialize(config);
    if (FAILED(hr))
    {
        return hr;
    }

    m_initialized = true;
    return S_OK;
}

HRESULT MagewellD3D11Capture::StartCapture()
{
    if (!m_initialized)
    {
        return E_FAIL;
    }

    if (m_capturing)
    {
        return S_OK;  // Already capturing
    }

    m_capturing = true;
    return S_OK;
}

void MagewellD3D11Capture::StopCapture()
{
    m_capturing = false;
}

HANDLE MagewellD3D11Capture::GetTextureSharedHandle(UINT index) const
{
    if (!m_initialized || index >= m_texturePool.GetTextureCount())
    {
        return INVALID_HANDLE_VALUE;
    }

    D3D11TexturePool::SharedTexture texture = m_texturePool.GetTexture(index);
    if (texture.dxgiResource == nullptr)
    {
        return INVALID_HANDLE_VALUE;
    }

    HANDLE sharedHandle = texture.sharedHandle;
    
    // Release the references we just got
    if (texture.texture) texture.texture->Release();
    if (texture.dxgiResource) texture.dxgiResource->Release();

    return sharedHandle;
}

D3D11TexturePool::SharedTexture MagewellD3D11Capture::GetTexture(UINT index)
{
    return m_texturePool.GetTexture(index);
}

HRESULT MagewellD3D11Capture::AcquireMutex(UINT index, DWORD timeoutMs)
{
    if (!m_initialized || index >= m_texturePool.GetTextureCount())
    {
        return E_INVALIDARG;
    }

    // Use the D3D11TexturePool's keyed mutex interface
    void* pMutex = m_texturePool.GetKeyedMutex(index);
    if (pMutex == nullptr)
    {
        // No keyed mutex available, return success
        return S_OK;
    }

    // Cast to ID3D11KeyedMutex and acquire
    ID3D11KeyedMutex* pKeyedMutex = reinterpret_cast<ID3D11KeyedMutex*>(pMutex);
    return pKeyedMutex->AcquireSync(0, timeoutMs);
}

HRESULT MagewellD3D11Capture::ReleaseMutex(UINT index)
{
    if (!m_initialized || index >= m_texturePool.GetTextureCount())
    {
        return E_INVALIDARG;
    }

    // Use the D3D11TexturePool's keyed mutex interface
    void* pMutex = m_texturePool.GetKeyedMutex(index);
    if (pMutex == nullptr)
    {
        // No keyed mutex available, return success
        return S_OK;
    }

    // Cast to ID3D11KeyedMutex and release
    ID3D11KeyedMutex* pKeyedMutex = reinterpret_cast<ID3D11KeyedMutex*>(pMutex);
    return pKeyedMutex->ReleaseSync(0);
}

void MagewellD3D11Capture::ProcessCapturedFrame()
{
    if (m_frameCallback && m_capturing)
    {
        // Get the current frame index (round-robin through pool)
        UINT frameIndex = m_currentFrameIndex % m_texturePool.GetTextureCount();
        
        // Acquire mutex before processing
        AcquireMutex(frameIndex);
        
        // Deliver frame with texture index for zero-copy rendering
        m_frameCallback(frameIndex, 0, m_width, m_height);
        
        // Release mutex after processing
        ReleaseMutex(frameIndex);
        
        m_currentFrameIndex++;
    }
}