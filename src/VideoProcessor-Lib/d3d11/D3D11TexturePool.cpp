/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include "D3D11TexturePool.h"

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

// IID for ID3D11KeyedMutex (defined in d3d11.h)
// {d8049659-3972-4608-a698-cf1690510a18}
static const GUID IID_ID3D11KeyedMutex = { 0xd8049659, 0x3972, 0x4608, { 0xa6, 0x98, 0xcf, 0x16, 0x90, 0x51, 0x0a, 0x18 } };

// Helper macro for FourCC encoding (little-endian)
#define MAKE_FOURCC(a, b, c, d) \
    ((DWORD)(BYTE)(a) | ((DWORD)(BYTE)(b) << 8) | ((DWORD)(BYTE)(c) << 16) | ((DWORD)(BYTE)(d) << 24))

D3D11TexturePool::D3D11TexturePool()
{
}

D3D11TexturePool::~D3D11TexturePool()
{
    ReleaseTextures();
    
    if (m_pContext)
    {
        m_pContext->Release();
        m_pContext = nullptr;
    }
    
    if (m_pDevice && m_ownedDevice)
    {
        m_pDevice->Release();
        m_pDevice = nullptr;
    }
}

DXGI_FORMAT D3D11TexturePool::GetDXGIFormat(DWORD magewellFourCC)
{
    // Map Magewell FourCC to DXGI_FORMAT
    switch (magewellFourCC)
    {
        case MAKE_FOURCC('N', 'V', '1', '2'):
            return DXGI_FORMAT_NV12;
        case MAKE_FOURCC('P', '0', '1', '0'):
            return DXGI_FORMAT_P010;
        case MAKE_FOURCC('Y', 'U', 'Y', '2'):
            return DXGI_FORMAT_YUY2;
        case MAKE_FOURCC('R', 'G', 'B', '3'):
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case MAKE_FOURCC('R', 'G', 'B', '4'):
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        default:
            return DXGI_FORMAT_NV12;
    }
}

HRESULT D3D11TexturePool::Initialize(const TextureConfig& config, ID3D11Device* pDevice)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_pDevice)
    {
        return S_OK;
    }
    
    m_config = config;
    
    if (pDevice == nullptr)
    {
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
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            createFlags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &m_pDevice,
            nullptr,
            &m_pContext
        );
        
        if (FAILED(hr))
        {
            return hr;
        }
        
        m_ownedDevice = true;
    }
    else
    {
        m_pDevice = pDevice;
        m_pDevice->AddRef();
        m_pDevice->GetImmediateContext(&m_pContext);
        m_ownedDevice = false;
    }
    
    return CreateTextures();
}

HRESULT D3D11TexturePool::CreateTextures()
{
    ReleaseTextures();
    
    DXGI_FORMAT format = m_config.format;
    if (format == DXGI_FORMAT_UNKNOWN)
    {
        format = DXGI_FORMAT_NV12;
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
            return hr;
        }
        
        hr = sharedTexture.texture->QueryInterface(__uuidof(IDXGIResource), 
            reinterpret_cast<void**>(&sharedTexture.dxgiResource));
        if (FAILED(hr))
        {
            sharedTexture.texture->Release();
            return hr;
        }
        
        hr = sharedTexture.dxgiResource->GetSharedHandle(&sharedTexture.sharedHandle);
        if (FAILED(hr))
        {
            sharedTexture.dxgiResource->Release();
            sharedTexture.texture->Release();
            return hr;
        }
        
        // Query the keyed mutex interface if enabled
        if (m_config.useKeyedMutex)
        {
            hr = sharedTexture.texture->QueryInterface(IID_ID3D11KeyedMutex,
                reinterpret_cast<void**>(&sharedTexture.keyedMutex));
            if (FAILED(hr))
            {
                // Keyed mutex not available, but continue without it
                sharedTexture.keyedMutex = nullptr;
            }
        }
        
        m_textures.push_back(sharedTexture);
    }
    
    return S_OK;
}

D3D11TexturePool::SharedTexture D3D11TexturePool::GetTexture(UINT index) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (index >= m_textures.size())
    {
        SharedTexture invalid;
        invalid.texture = nullptr;
        invalid.dxgiResource = nullptr;
        invalid.sharedHandle = INVALID_HANDLE_VALUE;
        invalid.index = index;
        return invalid;
    }
    
    m_textures[index].texture->AddRef();
    m_textures[index].dxgiResource->AddRef();
    
    return m_textures[index];
}

HRESULT D3D11TexturePool::OpenSharedTexture(HANDLE sharedHandle, ID3D11Texture2D** ppTexture)
{
    if (ppTexture == nullptr || sharedHandle == INVALID_HANDLE_VALUE)
    {
        return E_INVALIDARG;
    }
    
    return m_pDevice->OpenSharedResource(
        sharedHandle,
        __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(ppTexture)
    );
}

void D3D11TexturePool::ReleaseTextures()
{
    for (auto& texture : m_textures)
    {
        // FIX #8: Explicitly release keyed mutex before texture destruction
        // Keyed mutex has its own reference count and must be released explicitly
        if (texture.keyedMutex)
        {
            // Release the keyed mutex interface (ID3D11KeyedMutex derives from IUnknown)
            IUnknown* pKeyedMutex = static_cast<IUnknown*>(texture.keyedMutex);
            pKeyedMutex->Release();
            texture.keyedMutex = nullptr;
        }
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

void* D3D11TexturePool::GetKeyedMutex(UINT index) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (index >= m_textures.size())
    {
        return nullptr;
    }
    
    return m_textures[index].keyedMutex;
}

HRESULT D3D11TexturePool::AcquireKeyedMutex(UINT index, DWORD msec)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (index >= m_textures.size() || m_textures[index].keyedMutex == nullptr)
    {
        return E_FAIL;
    }
    
    ID3D11KeyedMutex* pKeyedMutex = reinterpret_cast<ID3D11KeyedMutex*>(m_textures[index].keyedMutex);
    HRESULT hr = pKeyedMutex->AcquireSync(0, msec);
    
    return hr;
}

void D3D11TexturePool::ReleaseKeyedMutex(UINT index)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (index >= m_textures.size() || m_textures[index].keyedMutex == nullptr)
    {
        return;
    }
    
    ID3D11KeyedMutex* pKeyedMutex = reinterpret_cast<ID3D11KeyedMutex*>(m_textures[index].keyedMutex);
    pKeyedMutex->ReleaseSync(0);
}

HRESULT D3D11TexturePool::UploadCpuBuffer(UINT index, const void* pSrcData, UINT srcRowPitch, UINT srcSlicePitch)
{
    if (!pSrcData || !m_pDevice || !m_pContext)
    {
        return E_INVALIDARG;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (index >= m_textures.size() || m_textures[index].texture == nullptr)
    {
        return E_INVALIDARG;
    }
    
    ID3D11Texture2D* pTexture = m_textures[index].texture;
    
    // Acquire keyed mutex before writing to the texture
    // This ensures the render thread is not reading this texture simultaneously
    if (m_textures[index].keyedMutex)
    {
        ID3D11KeyedMutex* pKeyedMutex = reinterpret_cast<ID3D11KeyedMutex*>(m_textures[index].keyedMutex);
        HRESULT hr = pKeyedMutex->AcquireSync(0, 1000);  // 1 second timeout
        if (FAILED(hr))
        {
            // Keyed mutex acquisition failed - texture is still being read by renderer
            return hr;
        }
    }
    
    HRESULT hr = S_OK;
    
    // The texture was created with D3D11_USAGE_DEFAULT and no CPUAccessFlags,
    // so we cannot Map/Unmap it directly. Instead, create a staging texture
    // for the upload, or use UpdateSubresource.
    //
    // UpdateSubresource is simpler for D3D11_USAGE_DEFAULT textures:
    // The source row pitch must match the expected format stride.
    // For NV12/P010 planar formats, we pass the subresource data in the Y plane
    // followed by the UV plane.
    
    D3D11_TEXTURE2D_DESC desc;
    pTexture->GetDesc(&desc);
    
    // For planar YUV formats (NV12, P010), the subresource layout is:
    // Subresource 0: Y plane (width * height pixels)
    // Subresource 1: UV plane (width/2 * height/2 * 2 components)
    //
    // For packed formats (YUY2, BGRA), it's a single subresource.
    UINT subresourceCount = desc.ArraySize * desc.MipLevels;
    
    if (desc.Format == DXGI_FORMAT_NV12 || desc.Format == DXGI_FORMAT_P010)
    {
        // Planar format: 2 subresources (Y and UV)
        // Calculate pitches for each plane
        D3D11_BOX srcBoxY = { 0, 0, 0, desc.Width, desc.Height, 1 };
        D3D11_BOX srcBoxUV = { 0, 0, 0, desc.Width, desc.Height / 2, 1 };
        
        // The source data layout from MWCaptureVideoFrameToVirtualAddressEx:
        // Y plane: srcRowPitch * desc.Height bytes
        // UV plane: srcRowPitch * (desc.Height / 2) bytes (interleaved U and V)
        UINT uvPlaneOffset = srcRowPitch * desc.Height;
        
        // Upload Y plane (subresource 0)
        m_pContext->UpdateSubresource(pTexture, 0, &srcBoxY, pSrcData, srcRowPitch, 0);
        
        // Upload UV plane (subresource 1)
        const void* pUVData = static_cast<const BYTE*>(pSrcData) + uvPlaneOffset;
        m_pContext->UpdateSubresource(pTexture, 1, &srcBoxUV, pUVData, srcRowPitch, 0);
    }
    else
    {
        // Packed format: single subresource
        D3D11_BOX srcBox = { 0, 0, 0, desc.Width, desc.Height, 1 };
        m_pContext->UpdateSubresource(pTexture, 0, &srcBox, pSrcData, srcRowPitch, 0);
    }
    
    // Release keyed mutex to signal render thread that texture is ready
    if (m_textures[index].keyedMutex)
    {
        ID3D11KeyedMutex* pKeyedMutex = reinterpret_cast<ID3D11KeyedMutex*>(m_textures[index].keyedMutex);
        pKeyedMutex->ReleaseSync(0);
    }
    
    return hr;
}
