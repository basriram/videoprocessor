/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include "CD3D11LiveSourceVideoOutputPin.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

CD3D11LiveSourceVideoOutputPin::CD3D11LiveSourceVideoOutputPin(
    CLiveSource* filter,
    CCritSec* pLock,
    HRESULT* phr) :
    CBufferedLiveSourceVideoOutputPin(filter, pLock, phr)
{
}

CD3D11LiveSourceVideoOutputPin::~CD3D11LiveSourceVideoOutputPin()
{
    ReleaseTexturePool();

    if (m_d3d11Context)
    {
        m_d3d11Context->Release();
        m_d3d11Context = nullptr;
    }

    if (m_d3d11Device && m_ownedDevice)
    {
        m_d3d11Device->Release();
        m_d3d11Device = nullptr;
    }
}

HRESULT CD3D11LiveSourceVideoOutputPin::InitializeD3D11(
    UINT width,
    UINT height,
    DXGI_FORMAT format,
    UINT poolSize)
{
    if (width == 0 || height == 0)
    {
        return E_INVALIDARG;
    }

    m_width = width;
    m_height = height;
    m_dxgiFormat = format;

    // Create D3D11 device if not already created
    HRESULT hr = CreateD3D11Device();
    if (FAILED(hr))
    {
        return hr;
    }

    // Create texture pool
    return CreateTexturePool();
}

HRESULT CD3D11LiveSourceVideoOutputPin::CreateD3D11Device()
{
    if (m_d3d11Device != nullptr)
    {
        return S_OK;  // Already created
    }

    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        nullptr,                          // Use default adapter
        D3D_DRIVER_TYPE_HARDWARE,         // Hardware device
        nullptr,                          // Software device
        createFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &m_d3d11Device,
        nullptr,                          // Feature level
        &m_d3d11Context                   // Device context
    );

    if (FAILED(hr))
    {
        return hr;
    }

    m_ownedDevice = true;
    return S_OK;
}

HRESULT CD3D11LiveSourceVideoOutputPin::CreateTexturePool()
{
    std::lock_guard<std::mutex> lock(m_textureMutex);

    ReleaseTexturePool();

    // Determine DXGI format
    DXGI_FORMAT format = m_dxgiFormat;
    if (format == DXGI_FORMAT_UNKNOWN)
    {
        format = DXGI_FORMAT_NV12;  // Default fallback
    }

    // Texture description for shared texture
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = m_width;
    desc.Height = m_height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    // Create texture pool
    for (UINT i = 0; i < 3; i++)  // Triple buffering
    {
        ID3D11Texture2D* texture = nullptr;
        HRESULT hr = m_d3d11Device->CreateTexture2D(&desc, nullptr, &texture);
        if (FAILED(hr))
        {
            ReleaseTexturePool();
            return hr;
        }

        // Get DXGI resource interface for shared handle
        IDXGIResource* dxgiResource = nullptr;
        hr = texture->QueryInterface(__uuidof(IDXGIResource),
            reinterpret_cast<void**>(&dxgiResource));
        if (FAILED(hr))
        {
            texture->Release();
            ReleaseTexturePool();
            return hr;
        }

        // Get shared handle
        HANDLE sharedHandle = INVALID_HANDLE_VALUE;
        hr = dxgiResource->GetSharedHandle(&sharedHandle);
        if (FAILED(hr))
        {
            dxgiResource->Release();
            texture->Release();
            ReleaseTexturePool();
            return hr;
        }

        m_textures.push_back(texture);
        m_sharedHandles.push_back(sharedHandle);

        dxgiResource->Release();
    }

    return S_OK;
}

void CD3D11LiveSourceVideoOutputPin::ReleaseTexturePool()
{
    std::lock_guard<std::mutex> lock(m_textureMutex);

    for (auto& texture : m_textures)
    {
        if (texture)
        {
            texture->Release();
        }
    }
    m_textures.clear();
    m_sharedHandles.clear();
}

HRESULT CD3D11LiveSourceVideoOutputPin::DecideAllocator(
    IMemInputPin* pPin,
    IMemAllocator** ppAlloc)
{
    // For D3D11 zero-copy, we don't use the standard allocator
    // The renderer will create its own D3D11 device and open shared handles
    return CBufferedLiveSourceVideoOutputPin::DecideAllocator(pPin, ppAlloc);
}

HRESULT CD3D11LiveSourceVideoOutputPin::DecideBufferSize(
    IMemAllocator* pAlloc,
    ALLOCATOR_PROPERTIES* ppropInputRequest)
{
    // For D3D11 zero-copy, buffer size is determined by texture dimensions
    // Return S_OK to indicate we don't need standard buffer allocation
    return S_OK;
}

HANDLE CD3D11LiveSourceVideoOutputPin::GetSharedHandleFromFrame(const VideoFrame& videoFrame)
{
    // Check if this frame has a D3D11 texture
    if (videoFrame.GetBufferType() == VideoFrame::BufferType::D3D11Texture)
    {
        // Return the shared handle from the D3D11 texture
        return videoFrame.GetD3D11SharedHandle();
    }
    
    return INVALID_HANDLE_VALUE;
}

HRESULT CD3D11LiveSourceVideoOutputPin::RenderD3D11FrameIntoSample(
    VideoFrame& videoFrame,
    IMediaSample* pSample)
{
    // Check if this frame has a D3D11 texture for zero-copy delivery
    if (videoFrame.GetBufferType() == VideoFrame::BufferType::D3D11Texture)
    {
        // Zero-copy path: Store the shared handle in a member variable
        // The renderer can access this via a custom interface or by checking the sample's context
        HANDLE sharedHandle = videoFrame.GetD3D11SharedHandle();
        
        if (sharedHandle != INVALID_HANDLE_VALUE)
        {
            // Store the shared handle for the next Deliver() call
            // This will be retrieved by the renderer through a custom mechanism
            m_currentSharedHandle = sharedHandle;
            
            // Also set the sample size to indicate valid frame data
            // The actual size depends on the format
            const VideoFrame::D3D11TextureInfo* textureInfo = videoFrame.GetD3D11TextureInfo();
            if (textureInfo)
            {
                // Calculate buffer size based on format
                UINT bufferSize = 0;
                switch (textureInfo->format)
                {
                    case DXGI_FORMAT_NV12:
                        bufferSize = textureInfo->width * textureInfo->height * 3 / 2;
                        break;
                    case DXGI_FORMAT_P010:
                    case DXGI_FORMAT_P016:
                        bufferSize = textureInfo->width * textureInfo->height * 4;
                        break;
                    case DXGI_FORMAT_YUY2:
                        bufferSize = textureInfo->width * textureInfo->height * 2;
                        break;
                    default:
                        bufferSize = textureInfo->width * textureInfo->height * 4;
                        break;
                }
                pSample->SetActualDataLength(bufferSize);
            }
            
            return S_OK;
        }
    }
    
    // Fallback to CPU copy for non-D3D11 frames
    // Get buffer pointer from sample
    BYTE* pBuffer = nullptr;
    HRESULT hr = pSample->GetPointer(&pBuffer);
    if (FAILED(hr))
    {
        return hr;
    }

    // Calculate buffer size based on frame dimensions
    UINT bufferSize = m_width * m_height * 2;  // Approximate for NV12/P010

    // Copy frame data to CPU buffer
    // This is a fallback path when D3D11 texture is not available
    const void* frameData = videoFrame.GetData();
    if (frameData && pBuffer)
    {
        memcpy(pBuffer, frameData, bufferSize);
    }

    // Set sample size
    pSample->SetActualDataLength(bufferSize);

    return S_OK;
}