# VideoProcessor Codebase Knowledge Base

## Project Overview

**VideoProcessor** is a low-latency video capture and processing application built on Microsoft DirectShow, designed for professional HDMI video capture and rendering. It supports multiple capture devices (Magewell Pro Capture, Blackmagic DeckLink) and multiple renderers (MPC Video Renderer, madVR, Enhanced Video Renderer).

**Solution File:** `VideoProcessor.sln`
**Primary Library:** `src/VideoProcessor-Lib/VideoProcessor-Lib.vcxproj`
**Build Configuration:** Debug/Release, x64

---

## Architecture

### Core Pipeline Flow

```
HDMI Source → Magewell Capture Card → CPU Buffer (Ring Buffer) → D3D11 Texture (optional) → DirectShow Renderer → Display
```

### Key Components

#### 1. Capture Layer
- **`ACaptureDevice`** - Abstract base class for all capture devices
- **`ACaptureDeviceDiscoverer`** - Abstract base for device discovery
- **`MagewellProCaptureDevice`** - Magewell Pro Capture SDK implementation
- **`BlackMagicDeckLinkCaptureDevice`** - Blackmagic DeckLink SDK implementation
- **`MagewellVideoFrame`** - Magewell-specific video frame wrapper

#### 2. Ring Buffer Layer
- **`CRingBuffer`** - Legacy mutex-based ring buffer
- **`CRingBufferLockFree`** - Lock-free SPSC (Single-Producer Single-Consumer) ring buffer
  - Uses `std::atomic` for indices: `m_write_num`, `m_render_read_num`, `m_encode_read_num`
  - Cache-line padded to prevent false sharing (64-byte alignment)
  - Back-pressure drops oldest frame when queue is full

#### 3. D3D11 Layer
- **`D3D11TexturePool`** - Manages shared D3D11 textures for zero-copy rendering
  - Creates textures with `D3D11_RESOURCE_MISC_SHARED` flag
  - Optional `D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX` for synchronization
  - Supports opening shared resources from external handles

#### 4. Rendering Layer
- **`IRenderer`** - Abstract renderer interface
- **`DirectShowVideoRenderer`** - Base DirectShow renderer implementation
- **`DirectShowMPCVideoRenderer`** - MPC Video Renderer (preferred for HDR)
- **`DirectShowGenericVideoRenderer`** - Generic DirectShow renderer
- **`DirectShowEnhancedVideoRenderer`** - Enhanced Video Renderer
- **`DirectShowGenericHDRVideoRenderer`** - HDR-capable generic renderer

#### 5. Video Frame Layer
- **`VideoFrame`** - Single video frame with support for:
  - CPU-backed buffers (`BufferType::CpuBuffer`)
  - D3D11 shared textures (`BufferType::D3D11Texture`)
  - HDR metadata sidecar (`std::shared_ptr<HDRData>`)
- **`VideoState`** - Stream state (resolution, format, colorspace, HDR data)

#### 6. Color Space & Format
- **`ColorSpace`** - BT.601, BT.709, BT.2020, etc.
- **`EOTF`** - SMPTE ST 2084 (PQ), HLG, BT.1886, etc.
- **`VideoFrameEncoding`** - NV12, P010, V210, R210, etc.
- **`HDRData`** - HDR10 metadata (display primaries, white point, mastering display, CLL/FAIL)

---

## Magewell Pro Capture Pipeline (Primary Path)

### Device Initialization
```cpp
MagewellProCaptureDevice::MagewellProCaptureDevice() {
    MWCaptureInitInstance();
    m_width = 3840; m_height = 2160;  // Default 4K
    m_mw_fourcc = MWFOURCC_P010;     // Default 10-bit
    m_frame_duration = 0;             // Set by hardware
}
```

### Capture Start Sequence
1. `SetCallbackHandler(callback)` - Sets up callback, creates signal detection thread
2. `StartCapture()` - Creates capture thread and render thread
3. `video_signal_pro()` - Monitors HDMI signal status, registers HDR/notification callbacks
4. `capture_by_input()` - Main capture loop:
   - Calls `MWStartVideoCapture()`
   - Registers for `MWCAP_NOTIFY_VIDEO_FRAME_BUFFERED`
   - Gets buffer from ring buffer via `get_buffer_to_fill()`
   - Fills buffer via `MWCaptureVideoFrameToVirtualAddressEx()`
   - Posts frame to ring buffer via `buffer_filled()`
   - Signals render thread via `SetEvent(frameAvailableEvent)`
5. `render_by_input()` - Render loop:
   - Waits for `frameAvailableEvent` or timeout (33ms)
   - Gets frame from ring buffer via `get_frame_to_render()`
   - Calls `m_callback->OnCaptureDeviceVideoFrame(vpVideoFrame)`

### Signal Detection
```cpp
DWORD MagewellProCaptureDevice::check_input_signal() {
    HNOTIFY notify = MWRegisterNotify(m_channel_handle, notify_event,
        MWCAP_NOTIFY_HDMI_INFOFRAME_HDR |
        MWCAP_NOTIFY_VIDEO_SIGNAL_CHANGE |
        MWCAP_NOTIFY_CONNECTION_FORMAT_CHANGED |
        MWCAP_NOTIFY_INPUT_SPECIFIC_CHANGE |
        MWCAP_NOTIFY_VIDEO_INPUT_SOURCE_CHANGE);
    // WaitForMultipleObjects(interruptEvent, notify_event, ...)
    // Checks MWGetVideoSignalStatus() for state changes
}
```

### HDR Detection
- Uses `MWCAP_NOTIFY_HDMI_INFOFRAME_HDR` notification
- Extracts HDR10 metadata from HDMI infoframe:
  - Display primaries (red, green, blue)
  - White point
  - Mastering display min/max luminance
  - Content max/min light levels (CLL/FAIL)

### FourCC Auto-Selection
```cpp
void MagewellProCaptureDevice::UpdateFourCCFromBitDepth() {
    if (bit_depth > 8) {
        m_mw_fourcc = MWFOURCC_P010;  // HDR (10-bit+)
    } else {
        m_mw_fourcc = MWFOURCC_NV12;  // SDR (8-bit)
    }
}
```

### Timestamp Handling
- Hardware clock via `MWGetDeviceTime()` - 10MHz resolution (100ns ticks)
- Frame timestamp from `video_frame_info.allFieldStartTimes[0]`
- Timing clock alignment for monotonic counter
- Current issue: Hardware timestamp is overwritten by `TimingClockNow()`

---

## Ring Buffer Implementation Details

### CRingBufferLockFree (Lock-Free SPSC)

**Memory Layout:**
```
st_frame_t m_p_frame[buffer_num]
  ├─ p_buffer (unsigned char*)
  ├─ buffer_len
  ├─ frame_len
  ├─ ts (timestamp)
  └─ user_point
```

**Atomic Indices (cache-line padded):**
```cpp
std::atomic<long long> m_write_num;        // Capture thread (producer)
std::atomic<long long> m_render_read_num;  // Render thread (consumer)
std::atomic<long long> m_encode_read_num;  // Encode thread (consumer)
```

**Back-Pressure Strategy:**
- When queue is full, advance read index to drop oldest frame
- Drop count tracked in `m_dropped_frames`
- Skip-ahead optimization: if consumer falls behind by > buffer_num/2, jump to newest

**Current Configuration:**
- Buffer count: 16 (line 300 in MagewellProCaptureDevice.cpp)
- Buffer size: `stride * height * 3/2` (P010 = 2 bytes/component, 1.5 bytes/pixel)
- Stride alignment: 256-byte aligned (line 294)

---

## D3D11 Texture Pool

### Texture Creation
```cpp
D3D11_TEXTURE2D_DESC desc = {};
desc.Usage = D3D11_USAGE_DEFAULT;
desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
desc.CPUAccessFlags = 0;
desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
```

### Supported Formats
```cpp
case MAKE_FOURCC('N', 'V', '1', '2'): return DXGI_FORMAT_NV12;
case MAKE_FOURCC('P', '0', '1', '0'): return DXGI_FORMAT_P010;
case MAKE_FOURCC('Y', 'U', 'Y', '2'): return DXGI_FORMAT_YUY2;
case MAKE_FOURCC('R', 'G', 'B', '3'): return DXGI_FORMAT_R8G8B8A8_UNORM;
```

### Keyed Mutex
- `AcquireSync(0, msec)` - Acquire for current owner
- `ReleaseSync(0)` - Release to next owner
- Currently **NOT USED** in capture/render path

---

## DirectShow Renderer Pipeline

### Graph Construction
1. Create filter graph (`CoCreateInstance(CLSID_FilterGraph)`)
2. Create live source filter (`CLSID_LiveSource`)
3. Create renderer filter (MPC Video Renderer, madVR, etc.)
4. Build media type (`AM_MEDIA_TYPE`) with:
   - `MEDIATYPE_Video`
   - Subtype (NV12, P010, etc.)
   - `VIDEOINFOHEADER2` with bitmap info
   - `DXVA_ExtendedFormat` for colorimetry
5. Connect pins (`m_pGraph->ConnectDirect()`)
6. Start graph

### Media Type Colorimetry
```cpp
DXVA_ExtendedFormat* colorimetry = (DXVA_ExtendedFormat*)&(pvi2->dwControlFlags);
colorimetry->VideoPrimaries = TranslateVideoPrimaries(colorspace);
colorimetry->VideoTransferMatrix = TranslateVideoTransferMatrix(colorspace);
colorimetry->VideoTransferFunction = TranslateVideoTranferFunction(eotf, colorspace);
colorimetry->NominalRange = DXVA_NominalRange_Unknown;  // BUG: should be explicit
```

### HDR Metadata Forwarding
```cpp
if (videoState->hdrData) {
    m_liveSource->OnHDRData(videoState->hdrData);
}
```

---

## Timing Clock

### Magewell Hardware Clock
- Resolution: 10,000,000 ticks/second (100ns per tick)
- Source: `MWGetDeviceTime()` - hardware timestamp from capture card
- Used for precise frame timing and synchronization

### Default/Fallback Clock
- `WallClock` - Windows system time

---

## Key File Locations

| File | Purpose |
|------|---------|
| `src/VideoProcessor-Lib/magewell_procapture/MagewellProCaptureDevice.cpp` | Main capture implementation |
| `src/VideoProcessor-Lib/magewell_procapture/ring_buffer/ring_buffer_lockfree.cpp` | Lock-free ring buffer |
| `src/VideoProcessor-Lib/d3d11/D3D11TexturePool.cpp` | D3D11 shared texture management |
| `src/VideoProcessor-Lib/microsoft_directshow/video_renderers/DirectShowMPCVideoRenderer.cpp` | MPC/madVR renderer |
| `src/VideoProcessor-Lib/VideoFrame.h` | Video frame structure |
| `src/VideoProcessor-Lib/HDRData.h` | HDR10 metadata structure |
| `src/VideoProcessor-Lib/VideoState.h` | Stream state structure |
| `src/VideoProcessor-Lib/video_frame_formatter/CV210toP010VideoFrameFormatter.cpp` | V210 to P010 converter |

---

## Build Configuration

### Compiler Settings
- Platform: x64
- Configuration: Debug / Release
- C++ Standard: C++20
- Precompiled Header: `pch.h` / `pch.cpp`

### Key Dependencies
- Magewell MWCapture SDK (`#include <Magewell/MWCapture.h>`)
- DirectShow Base Classes
- DirectX 11 (`d3d11.lib`, `dxgi.lib`)
- FFmpeg (for video frame formatting)

### MSBuild Commands
```bash
# Debug build
"MSBuild.exe" VideoProcessor.sln /p:Configuration=Debug /p:Platform=x64

# Release build
"MSBuild.exe" VideoProcessor.sln /p:Configuration=Release /p:Platform=x64
```

---

## Known Issues & Limitations

### Capture Pipeline
1. **Hardware timestamp overwritten** - `p_frame->ts` set from hardware but then overwritten by `TimingClockNow()`
2. **frame_len semantic error** - Uses cumulative counter instead of frame delta
3. **INFINITE wait in capture loop** - Thread can sleep indefinitely between frames
4. **Buffer reallocation on HDR/SDR switch** - Stops entire pipeline when FourCC changes
5. **16 buffers too large** - 4K P010 x 16 = ~414MB, adds ~26ms latency at 60fps

### Ring Buffer
1. **Event wakeup latency** - 33ms timeout means frames wait up to 33ms (mitigated by WaitOnAddress implementation)
2. **No WaitOnAddress** - Uses event-based wakeup instead of futex-style atomic wait (MITIGATED: WaitOnAddress + InterlockedExchange64 implemented in Phase 1)
3. **No keyed mutex usage** - Synchronization relies on events, not D3D11 keyed mutex (TODO for future phase)

### Renderer
1. **Nominal range not set** - P010 should use full range, NV12 should use video range
2. **lSampleSize uses DIBSIZE** - Should use actual formatter output size
3. **madVR not configured for low latency** - Default settings may include heavy post-processing

### HDR Handling
1. **Full pipeline update on any HDR change** - Could be field-level incremental
2. **No HDR infoframe debounce** - Rapid changes may cause unnecessary rebuilds

---

## Color Space & Format Reference

### YUV Formats
| Format | Bits/Pixel | Chroma Subsampling | Use Case |
|--------|-----------|-------------------|----------|
| NV12 | 12 | 4:2:0 | SDR 8-bit |
| P010 | 16 | 4:2:0 | HDR 10-bit |
| V210 | 30 | 4:2:2 | Uncompressed 10-bit 4:2:2 |
| R210 | 30 | 4:4:4 | Uncompressed 10-bit RGB |

### Color Spaces
| Space | ID | Use Case |
|-------|----|----------|
| BT.601 | 1 | SD/SDR legacy |
| BT.709 | 2 | HD/SDR |
| BT.2020 | 3 | UHD/HDR |

### EOTF (Electro-Optical Transfer Function)
| EOTF | ID | Use Case |
|------|----|----------|
| BT.1886 | 1 | SDR gamma |
| SMPTE ST 2084 (PQ) | 3 | HDR10 |
| HLG | 4 | Hybrid Log-Gamma |

### HDR10 Metadata Fields
- `displayPrimaryRedX/Y`, `displayPrimaryGreenX/Y`, `displayPrimaryBlueX/Y`
- `whitePointX/Y`
- `masteringDisplayMinLuminance`
- `masteringDisplayMaxLuminance`
- `maxCll` (Content Maximum Light Level)
- `maxFall` (Content Maximum Frame-Average Light Level)

---

## Threading Model

### Threads per Magewell Device
1. **Signal Detection Thread** (`video_signal_pro`) - Monitors HDMI signal, HDR infoframes
2. **Capture Thread** (`capture_by_input`) - Fills ring buffer from hardware
3. **Render Thread** (`render_by_input`) - Consumes from ring buffer, calls callback

### Thread Safety Notes
- `m_state`, `m_videoFrameSeen`, `m_ticksPerFrame` are R/W from capture thread only
- `m_callback` is R/W from main thread only (via `SetCallbackHandler`)
- Ring buffer is lock-free SPSC (single producer = capture, single consumer = render)
- `D3D11TexturePool` uses `std::mutex` for texture pool management

---

## API Reference

### Magewell SDK Key Functions
| Function | Purpose |
|----------|---------|
| `MWRefreshDevice()` | Scan for devices |
| `MWGetChannelCount()` | Get channel count |
| `MWOpenChannelByPath()` | Open channel by device path |
| `MWGetChannelInfo()` | Get channel info |
| `MWStartVideoCapture()` | Start video capture |
| `MWStopVideoCapture()` | Stop video capture |
| `MWCaptureVideoFrameToVirtualAddressEx()` | Capture frame to CPU buffer |
| `MWGetVideoSignalStatus()` | Get HDMI signal status |
| `MWGetDeviceTime()` | Get hardware clock time |
| `MWGetHDMIInfoFramePacket()` | Get HDMI infoframe data |
| `MWRegisterNotify()` | Register for event notifications |

### DirectShow Key Interfaces
| Interface | Purpose |
|-----------|---------|
| `IFilterGraph` | Build and control filter graph |
| `IBaseFilter` | Add filters to graph |
| `IPin` | Connect filters |
| `IBasicVideo` | Set video size/position |
| `IVideoWindow` | Set video window |

---

## Performance Characteristics

### Memory Usage at 4K60 P010
- Single frame: `3840 * 256 (stride) * 2160 * 1.5 / 2` = ~25.9 MB
- 16 buffers: ~414 MB
- 4 buffers: ~103 MB
- 6 buffers: ~155 MB

### Latency Budget (Current)
| Stage | Latency |
|-------|---------|
| Magewell hardware capture | ~1-2ms |
| Ring buffer (16 frames @ 60fps) | ~26ms |
| Event wakeup (33ms timeout) | ~0-33ms |
| Render thread processing | ~1-3ms |
| D3D11 presentation | ~1-2ms |
| **Total (worst case)** | **~63-66ms** |
| **Total (best case)** | **~29-32ms** |

### Latency Budget (Optimized Target)
| Stage | Target Latency |
|-------|---------------|
| Magewell hardware capture | ~1-2ms |
| Ring buffer (4 frames @ 60fps) | ~6.7ms |
| WaitOnAddress wakeup | ~0-1ms |
| Render thread processing | ~1-3ms |
| D3D11 keyed mutex sync | ~1-2ms |
| **Total (target)** | **~10-15ms** |

---

## Common HRESULT Error Codes

| Code | Meaning |
|------|---------|
| `0x887A0005` | DXGI_ERROR_DEVICE_REMOVED - GPU removed/reset |
| `0x887A0006` | DXGI_ERROR_DEVICE_REMOVED - Device removed |
| `0x887A0021` | DXGI_ERROR_INVALID_CALL - Invalid API call |
| `0x88990007` | D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS |
| `S_OK` (0) | Success |
| `E_FAIL` (0x80004005) | Generic failure |

---

## Debugging Tips

### Frame Drop Analysis
- Monitor `m_capturedVideoFrameCount` and `m_missedVideoFrameCount`
- Check ring buffer queue depth via `GetQueueDepth()`
- Check dropped frames via `GetDroppedFrameCount()`

### Latency Measurement
- Compare `p_frame->ts` (hardware) vs `TimingClockNow()` (CPU)
- Log timestamps at each pipeline stage
- Use Windows Performance Recorder (WPR) for thread scheduling analysis

### DirectX Debugging
- Enable `D3D11_CREATE_DEVICE_DEBUG` in debug builds
- Use PIX for Windows (`pixwin.exe`) for GPU frame capture
- Check for resource leaks via `IDXGIDevice::GetDebugInfo()`

### Crash Dump Analysis
- Map call stack to source via Visual Studio debugger
- Check for null COM pointers (especially `m_channel_handle`)
- Verify D3D11 texture descriptions match actual capture format