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
#include "../HDRData.h"
#include "../HDR10Metadata.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// Forward declare ID3D11KeyedMutex for keyed mutex operations
extern "C" {
    struct ID3D11KeyedMutex : public IUnknown
    {
        STDMETHOD(AcquireSync)(UINT64 key, DWORD msec) = 0;
        STDMETHOD(ReleaseSync)(UINT64 key) = 0;
    };
}

// IID for ID3D11KeyedMutex
static const GUID IID_ID3D11KeyedMutex = { 0xd8049659, 0x3972, 0x4608, { 0xa6, 0x98, 0xcf, 0x16, 0x90, 0x51, 0x0a, 0x18 } };

// Helper macro for FourCC encoding (little-endian)
#define MAKE_FOURCC(a, b, c, d) \
    ((DWORD)(BYTE)(a) | ((DWORD)(BYTE)(b) << 8) | ((DWORD)(BYTE)(c) << 16) | ((DWORD)(BYTE)(d) << 24))

MagewellD3D11Capture::MagewellD3D11Capture()
{
}

MagewellD3D11Capture::~MagewellD3D11Capture()
{
    StopCapture();
    
    if (m_pContext)
    {
        m_pContext->Release();
        m_pContext = nullptr;
    }
    
    if (m_pDevice)
    {
        m_pDevice->Release();
        m_pDevice = nullptr;
    }
}

DXGI_FORMAT MagewellD3D11Capture::GetDXGIFormatFromFourCC(DWORD fourcc)
{
    switch (fourcc)
    {
        case MAKE_FOURCC('N', 'V', '1', '2'):
            return DXGI_FORMAT_NV12;
        case MAKE_FOURCC('P', '0', '1', '0'):
            return DXGI_FORMAT_P010;
        case MAKE_FOURCC('P', '2', '1', '0'):
            return DXGI_FORMAT_P210;
        case MAKE_FOURCC('Y', 'U', 'Y', '2'):
            return DXGI_FORMAT_YUY2;
        case MAKE_FOURCC('R', 'G', 'B', '3'):
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case MAKE_FOURCC('R', 'G', 'B', '4'):
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        default:
            return DXGI_FORMAT_P010;  // Default to 10-bit for HDR
    }
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
    m_dxgiFormat = GetDXGIFormatFromFourCC(fourcc);
    if (m_dxgiFormat == DXGI_FORMAT_UNKNOWN)
    {
        m_dxgiFormat = DXGI_FORMAT_P010;  // Default to 10-bit for HDR
    }

    // Configure texture pool for zero-copy capture
    m_config.width = width;
    m_config.height = height;
    m_config.format = m_dxgiFormat;
    m_config.poolSize = 3;  // Triple buffering for smooth 4K60
    m_config.useKeyedMutex = true;  // Enable keyed mutex for synchronization

    // Create D3D11 device
    HRESULT hr = CreateD3D11Device();
    if (FAILED(hr))
    {
        return hr;
    }

    // Create texture pool
    hr = CreateTexturePool();
    if (FAILED(hr))
    {
        return hr;
    }

    m_capturing = false;
    return S_OK;
}

HRESULT MagewellD3D11Capture::CreateD3D11Device()
{
    if (m_pDevice != nullptr)
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
        &m_pDevice,
        nullptr,                          // Feature level
        &m_pContext                       // Device context
    );

    if (FAILED(hr))
    {
        return hr;
    }

    return S_OK;
}

HRESULT MagewellD3D11Capture::CreateTexturePool()
{
    std::lock_guard<std::mutex> lock(m_textureMutex);

    ReleaseTexturePool();

    DXGI_FORMAT format = m_config.format;
    if (format == DXGI_FORMAT_UNKNOWN)
    {
        format = DXGI_FORMAT_P010;  // Default to 10-bit
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = m_config.width;
    desc.Height = m_config.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    if (m_config.useKeyedMutex)
    {
        desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    }

    for (UINT i = 0; i < m_config.poolSize; i++)
    {
        SharedTexture sharedTexture;
        sharedTexture.index = i;

        HRESULT hr = m_pDevice->CreateTexture2D(&desc, nullptr, &sharedTexture.texture);
        if (FAILED(hr))
        {
            ReleaseTexturePool();
            return hr;
        }

        hr = sharedTexture.texture->QueryInterface(__uuidof(IDXGIResource), 
            reinterpret_cast<void**>(&sharedTexture.dxgiResource));
        if (FAILED(hr))
        {
            sharedTexture.texture->Release();
            ReleaseTexturePool();
            return hr;
        }

        hr = sharedTexture.dxgiResource->GetSharedHandle(&sharedTexture.sharedHandle);
        if (FAILED(hr))
        {
            sharedTexture.dxgiResource->Release();
            sharedTexture.texture->Release();
            ReleaseTexturePool();
            return hr;
        }

        // Query the keyed mutex interface if enabled
        if (m_config.useKeyedMutex)
        {
            hr = sharedTexture.texture->QueryInterface(IID_ID3D11KeyedMutex,
                reinterpret_cast<void**>(&sharedTexture.keyedMutex));
            if (FAILED(hr))
            {
                sharedTexture.keyedMutex = nullptr;
            }
        }

        m_textures.push_back(sharedTexture);
    }

    return S_OK;
}

void MagewellD3D11Capture::ReleaseTexturePool()
{
    for (auto& texture : m_textures)
    {
        if (texture.dxgiResource)
        {
            texture.dxgiResource->Release();
            texture.dxgiResource = nullptr;
        }
        if (texture.texture)
        {
            texture.texture->Release();
            texture.texture = nullptr;
        }
        texture.sharedHandle = INVALID_HANDLE_VALUE;
    }
    m_textures.clear();
}

HRESULT MagewellD3D11Capture::AcquireKeyedMutex(UINT index, DWORD timeoutMs)
{
    std::lock_guard<std::mutex> lock(m_textureMutex);

    if (index >= m_textures.size() || m_textures[index].keyedMutex == nullptr)
    {
        return S_OK;  // No mutex to acquire
    }

    ID3D11KeyedMutex* pKeyedMutex = reinterpret_cast<ID3D11KeyedMutex*>(m_textures[index].keyedMutex);
    HRESULT hr = pKeyedMutex->AcquireSync(0, timeoutMs);

    return hr;
}

void MagewellD3D11Capture::ReleaseKeyedMutex(UINT index)
{
    std::lock_guard<std::mutex> lock(m_textureMutex);

    if (index >= m_textures.size() || m_textures[index].keyedMutex == nullptr)
    {
        return;
    }

    ID3D11KeyedMutex* pKeyedMutex = reinterpret_cast<ID3D11KeyedMutex*>(m_textures[index].keyedMutex);
    pKeyedMutex->ReleaseSync(0);
}

HRESULT MagewellD3D11Capture::StartCapture()
{
    if (m_capturing.load())
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

std::shared_ptr<HDRData> MagewellD3D11Capture::ParseHDRMetadata()
{
    // This method should be called when HDR infoframe is detected
    // It parses the Magewell HDMI HDR infoframe and returns HDRData
    // Implementation depends on integration with MagewellProCaptureDevice
    
    std::shared_ptr<HDRData> hdrData = std::make_shared<HDRData>();
    
    // Placeholder - actual implementation would call MWGetHDMIInfoFramePacket
    // and parse the HDR infoframe to populate hdrData
    
    return hdrData;
}

void MagewellD3D11Capture::ProcessCapturedFrame(UINT textureIndex, LONGLONG timestamp)
{
    if (!m_frameCallback || !m_capturing.load())
    {
        return;
    }

    // Acquire mutex before accessing texture
    AcquireKeyedMutex(textureIndex, 10);

    // Get shared handle for the texture
    HANDLE sharedHandle = INVALID_HANDLE_VALUE;
    if (textureIndex < m_textures.size())
    {
        sharedHandle = m_textures[textureIndex].sharedHandle;
        
        // Update frame key for synchronization
        m_textures[textureIndex].frameKey = m_frameKey.fetch_add(1);
    }

    // Parse HDR metadata if available
    std::shared_ptr<HDRData> hdrData = nullptr;
    if (m_isHDR && m_hdrCallback)
    {
        hdrData = ParseHDRMetadata();
    }

    // Deliver frame via callback
    m_frameCallback(
        textureIndex,
        sharedHandle,
        m_width,
        m_height,
        m_dxgiFormat,
        timestamp,
        hdrData
    );

    // Release mutex after processing
    ReleaseKeyedMutex(textureIndex);
}