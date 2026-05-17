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
#include <memory>
#include <mutex>

/**
 * D3D11TexturePool - Manages a pool of shared D3D11 textures for zero-copy video frame handling.
 * 
 * This class creates D3D11 textures with shared handles that can be accessed by:
 * - The Magewell SDK for direct hardware capture
 * - madVR renderer via DirectShow for zero-copy rendering
 * 
 * Key features:
 * - D3D11_RESOURCE_MISC_SHARED flag for cross-process sharing
 * - Optional D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX for synchronization
 * - Automatic texture pool management with configurable depth
 */
class D3D11TexturePool
{
public:
    /**
     * Texture configuration structure
     */
    struct TextureConfig
    {
        UINT width = 0;
        UINT height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
        UINT poolSize = 3;  // Number of textures in the pool
        bool useKeyedMutex = true;  // Use keyed mutex for synchronization
    };

    /**
     * Shared texture handle for external access
     */
    struct SharedTexture
    {
        ID3D11Texture2D* texture = nullptr;
        IDXGIResource* dxgiResource = nullptr;
        HANDLE sharedHandle = INVALID_HANDLE_VALUE;
        void* keyedMutex = nullptr;  // For frame synchronization (ID3D11KeyedMutex*, stored as void*)
        UINT index = 0;
    };

    D3D11TexturePool();
    ~D3D11TexturePool();

    /**
     * Initialize the D3D11 device and texture pool
     * @param config - Texture configuration parameters
     * @param pDevice - Optional existing D3D11 device (if nullptr, will create one)
     * @return S_OK on success
     */
    HRESULT Initialize(const TextureConfig& config, ID3D11Device* pDevice = nullptr);

    /**
     * Get texture at specified index
     * @param index - Texture index (0 to pool size - 1)
     * @return SharedTexture with valid texture and shared handle
     */
    SharedTexture GetTexture(UINT index) const;

    /**
     * Get the D3D11 device
     */
    ID3D11Device* GetDevice() const { return m_pDevice; }

    /**
     * Get the D3D11 device context
     */
    ID3D11DeviceContext* GetContext() const { return m_pContext; }

    /**
     * Get texture count in pool
     */
    UINT GetTextureCount() const { return static_cast<UINT>(m_textures.size()); }

    /**
     * Check if initialized
     */
    bool IsInitialized() const { return m_pDevice != nullptr; }

    /**
     * Open a shared texture from external handle
     * @param sharedHandle - The shared handle from another process/device
     * @param ppTexture - Receives the opened ID3D11Texture2D
     * @return S_OK on success
     */
    HRESULT OpenSharedTexture(HANDLE sharedHandle, ID3D11Texture2D** ppTexture);

    /**
     * Get keyed mutex for texture at specified index
     * @param index - Texture index
     * @return void* (cast to ID3D11KeyedMutex* when needed) or nullptr if not available
     */
    void* GetKeyedMutex(UINT index) const;

    /**
     * Acquire keyed mutex for texture at specified index
     * @param index - Texture index
     * @param msec - Timeout in milliseconds
     * @return S_OK if acquired, E_TIMEOUT if timeout
     */
    HRESULT AcquireKeyedMutex(UINT index, DWORD msec = INFINITE);

    /**
     * Release keyed mutex for texture at specified index
     * @param index - Texture index
     */
    void ReleaseKeyedMutex(UINT index);

    /**
     * Get DXGI format for common pixel formats
     */
    static DXGI_FORMAT GetDXGIFormat(DWORD magewellFourCC);

private:
    HRESULT CreateTextures();
    void ReleaseTextures();

    TextureConfig m_config;
    std::vector<SharedTexture> m_textures;
    ID3D11Device* m_pDevice = nullptr;
    ID3D11DeviceContext* m_pContext = nullptr;
    bool m_ownedDevice = false;
    mutable std::mutex m_mutex;
};