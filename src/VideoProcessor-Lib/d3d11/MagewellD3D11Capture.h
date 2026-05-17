/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>

// Forward declaration
struct _HDMI_HDR_INFOFRAME_PAYLOAD;

/**
 * Magewell D3D11 Capture - Zero-Copy HDR Pipeline
 * 
 * This class provides direct D3D11 texture capture from Magewell hardware,
 * enabling zero-copy frame delivery to madVR renderer for 4K HDR60 streams.
 * 
 * Key features:
 * - D3D11 shared textures for zero-copy rendering
 * - P010 format support for 10-bit HDR
 * - HDR metadata extraction and attachment
 * - Lock-free frame queue for low-latency delivery
 */
class MagewellD3D11Capture
{
public:
    /**
     * Frame callback signature
     * @param textureIndex Index of the captured texture in the pool
     * @param sharedHandle DXGI shared handle for the frame
     * @param width Frame width
     * @param height Frame height
     * @param format Pixel format
     * @param timestamp Frame timestamp
     * @param hdrData HDR metadata (may be nullptr)
     */
    using FrameCallback = std::function<void(
        UINT textureIndex,
        HANDLE sharedHandle,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        LONGLONG timestamp,
        std::shared_ptr<struct HDRData> hdrData
    )>;

    /**
     * Texture configuration
     */
    struct TextureConfig
    {
        UINT width = 3840;
        UINT height = 2160;
        DXGI_FORMAT format = DXGI_FORMAT_P010;  // Default to 10-bit for HDR
        UINT poolSize = 3;                       // Triple buffering
        bool useKeyedMutex = true;              // Enable synchronization
    };

    MagewellD3D11Capture();
    ~MagewellD3D11Capture();

    /**
     * Initialize the capture pipeline
     * @param width Frame width
     * @param height Frame height
     * @param fourcc Magewell FourCC (e.g., 'P010' for 10-bit HDR)
     * @param callback Frame delivery callback
     * @return S_OK on success
     */
    HRESULT Initialize(UINT width, UINT height, DWORD fourcc, FrameCallback callback);

    /**
     * Start capturing frames
     * @return S_OK on success
     */
    HRESULT StartCapture();

    /**
     * Stop capturing frames
     */
    void StopCapture();

    /**
     * Get the D3D11 device (for renderer integration)
     */
    ID3D11Device* GetDevice() const { return m_pDevice; }

    /**
     * Get the DXGI format being used
     */
    DXGI_FORMAT GetDXGIFormat() const { return m_dxgiFormat; }

    /**
     * Check if capture is active
     */
    bool IsCapturing() const { return m_capturing.load(); }

    /**
     * Set HDR metadata callback (called when HDR mode changes)
     */
    using HDRCallback = std::function<void(std::shared_ptr<struct HDRData>)>;
    void SetHDRCallback(HDRCallback callback) { m_hdrCallback = callback; }

private:
    /**
     * Shared texture wrapper
     */
    struct SharedTexture
    {
        ID3D11Texture2D* texture = nullptr;
        IDXGIResource* dxgiResource = nullptr;
        HANDLE sharedHandle = INVALID_HANDLE_VALUE;
        void* keyedMutex = nullptr;
        std::atomic<UINT64> frameKey{0};
    };

    /**
     * Create the D3D11 device
     */
    HRESULT CreateD3D11Device();

    /**
     * Create the texture pool
     */
    HRESULT CreateTexturePool();

    /**
     * Release all textures
     */
    void ReleaseTexturePool();

    /**
     * Acquire keyed mutex for texture access
     */
    HRESULT AcquireKeyedMutex(UINT index, DWORD timeoutMs);

    /**
     * Release keyed mutex
     */
    void ReleaseKeyedMutex(UINT index);

    /**
     * Get DXGI format from Magewell FourCC
     */
    DXGI_FORMAT GetDXGIFormatFromFourCC(DWORD fourcc);

    /**
     * Process captured frame - called from Magewell callback
     */
    void ProcessCapturedFrame(UINT textureIndex, LONGLONG timestamp);

    /**
     * Parse HDR metadata from Magewell HDMI infoframe
     */
    std::shared_ptr<struct HDRData> ParseHDRMetadata();

    // Configuration
    TextureConfig m_config;
    DXGI_FORMAT m_dxgiFormat;
    DWORD m_fourcc;
    UINT m_width;
    UINT m_height;

    // D3D11 resources
    ID3D11Device* m_pDevice = nullptr;
    ID3D11DeviceContext* m_pContext = nullptr;
    std::vector<SharedTexture> m_textures;

    // Capture state
    std::atomic<bool> m_capturing{false};
    std::atomic<UINT> m_currentFrameIndex{0};
    std::atomic<UINT64> m_frameKey{0};

    // Callbacks
    FrameCallback m_frameCallback;
    HDRCallback m_hdrCallback;

    // Synchronization
    mutable std::mutex m_textureMutex;

    // HDR state
    bool m_isHDR = false;
};