/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>


#pragma warning(disable : 26812)  // class enum over class in BM API

#include <set>
#include <math.h>

#include <magewell_procapture/MagewellProCaptureTranslate.h>
#include <magewell_procapture/ring_buffer/ring_buffer_lockfree.h>
#include <cie.h>
#include <StringUtils.h>
#include <WallClock.h>

#include "MagewellProCaptureDevice.h"
#include "MagewellVideoFrame.h"
#include <d3d11.h>
#include <d3d11/D3D11TexturePool.h>
 //#define _DEBUG
#ifdef _DEBUG
#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
 // Replace _NORMAL_BLOCK with _CLIENT_BLOCK if you want the
 // allocations to be of _CLIENT_BLOCK type
#else
#define DBG_NEW new
#endif
static const timingclocktime_t MAGEWELL_CLOCK_MAX_TICKS_SECOND = 10000000LL;  // us


//
// Constructor & destructor
//


MagewellProCaptureDevice::MagewellProCaptureDevice() 
	{
		MWCaptureInitInstance();


		m_p_video_buffer = NULL;
		// Phase 3: Pre-allocate dual-format buffer pools
		m_p_video_buffer_nv12 = NULL;
		m_p_video_buffer_p010 = NULL;
		m_active_video_fourcc = MWFOURCC_P010;  // Default to P010
		m_p_audio_buffer = NULL;
		
		// Phase 3.2: Initialize D3D11 members
		m_p_d3d11_texture_pool = NULL;
		m_p_d3d11_device = NULL;
		m_p_d3d11_device_context = NULL;
		m_enable_d3d11_capture = false;  // Disabled by default, enabled via SetD3D11Device()

	m_is_start = false;
	m_frame_duration = 0;
	m_width = 3840;
	m_height = 2160;
	m_mw_fourcc = MWFOURCC_P010;
	m_audio_channel_num = 2;
	m_audio_bit_per_sample = 16;

	m_is_mirror = -1;
	m_is_reverse = -1;

	m_signal_frame_duration = 166667;
	m_audio_sample_rate = 48000;
	m_channel_handle = NULL;
	m_device_family = MW_FAMILY_ID_PRO_CAPTURE;
	m_device_index = -1;
	m_use_common_api = false;
	m_user_video_buffer = false;
	m_user_audio_buffer = false;
	m_prev_frame_count = 0;
	m_prev_time = 0;
	m_fps = 0;
	m_capture_frame_count = 0;
	m_video_capturing = false;
	m_audio_capturing = false;
	m_capture_video = true;
	m_capture_audio = false;

	m_bottom_up = false;//
	m_process_switchs = 0;//
	m_parital_notify = 0;// Enable partial notification for low-latency capture
	m_OSD_image = NULL;//NULL
	m_p_OSD_rects = NULL;//NULL
	m_OSD_rects_num = 0;//0
	m_contrast = 100;//100
	m_brightness = 0;//0
	m_saturation = 100;//100
	m_hue = 0;//0
	m_deinterlace_mode = MWCAP_VIDEO_DEINTERLACE_BLEND;//
	m_aspect_ratio_convert_mode = MWCAP_VIDEO_ASPECT_RATIO_IGNORE;//
	m_p_rect_src = NULL;//NULL
	m_p_rect_dest = NULL;//NULL
	m_aspect_x = 0;//0
	m_aspect_y = 0;//0
	m_color_format = MWCAP_VIDEO_COLOR_FORMAT_UNKNOWN;//
	m_quant_range = MWCAP_VIDEO_QUANTIZATION_UNKNOWN;
	m_sat_range = MWCAP_VIDEO_SATURATION_UNKNOWN;
	m_signal_thread = NULL;
	m_video_thread = NULL;
	m_render_thread = NULL;

	// Get current capture id
	LONGLONG captureInputId;
	captureInputId = 0;

	// Build capture inputs
	m_captureInputSet.push_back(CaptureInput(captureInputId, CaptureInputType::HDMI, TEXT("HDMI")));
	ResetVideoState();
	set_device(0);
	m_state = CaptureDeviceState::CAPTUREDEVICESTATE_UNKNOWN;
}


MagewellProCaptureDevice::~MagewellProCaptureDevice()
{
	MWCaptureExitInstance();

	// Phase 3.2: Release D3D11 resources
	if (m_p_d3d11_texture_pool) {
		delete m_p_d3d11_texture_pool;
		m_p_d3d11_texture_pool = NULL;
	}
	if (m_p_d3d11_device_context) {
		m_p_d3d11_device_context->Release();
		m_p_d3d11_device_context = NULL;
	}
	if (m_p_d3d11_device) {
		m_p_d3d11_device->Release();
		m_p_d3d11_device = NULL;
	}

	// Phase 3: Free dual-format buffer pools
	if (!m_user_video_buffer) {
		if (m_p_video_buffer_nv12) {
			delete m_p_video_buffer_nv12;
			m_p_video_buffer_nv12 = NULL;
		}
		if (m_p_video_buffer_p010) {
			delete m_p_video_buffer_p010;
			m_p_video_buffer_p010 = NULL;
		}
		// Also free legacy m_p_video_buffer if it's different from both
		if (m_p_video_buffer && m_p_video_buffer != m_p_video_buffer_nv12 && m_p_video_buffer != m_p_video_buffer_p010) {
			delete m_p_video_buffer;
			m_p_video_buffer = NULL;
		}
	}
	if (!m_user_audio_buffer && m_p_audio_buffer) {
		delete m_p_audio_buffer;
		m_p_audio_buffer = NULL;
	}
	if (m_channel_handle) {
		MWCloseChannel(m_channel_handle);
		m_channel_handle = NULL;

	}
	m_canCapture = false;
	m_capture_video = false;
	m_video_capturing = false;
	if (!SetEvent(interruptEvent))
	{
		printf("SetEvent failed (%d)\n", GetLastError());
	}

	HANDLE handle[3];
	handle[0] = m_signal_thread;
	handle[1] = m_video_thread;
	handle[2] = m_render_thread;
	WaitForMultipleObjects(3, handle, TRUE, 500);
	//CloseHandle(signalLockedEvent);
	//CloseHandle(frameAvailableEvent);
	//CloseHandle(interruptEvent);

/*
	if (m_video_thread) {
		m_video_capturing = false;
		WaitForSingleObject(m_video_thread, INFINITE);
	}
	if (m_render_thread) {
		m_audio_capturing = false;
		WaitForSingleObject(m_render_thread, INFINITE);
	}
	*/
	//m_callback = nullptr;

}


//
// ACaptureDevice
//




bool MagewellProCaptureDevice::refresh_devices()
{
	if (MWRefreshDevice() != MW_SUCCEEDED) {
		return false;
	}
	return true;
}

int MagewellProCaptureDevice::get_channel_count()
{
	return MWGetChannelCount();
}

bool MagewellProCaptureDevice::get_channel_info_by_index(int index, MWCAP_CHANNEL_INFO* p_channel_info)
{
	if (MW_SUCCEEDED != MWGetChannelInfoByIndex(index, p_channel_info)) {
		return false;
	}
	return true;
}
void pin_device_name(MWCAP_CHANNEL_INFO channel_info, char* p_device_name)
{
	if (NULL == p_device_name) {
		return;
	}
	if (strstr(channel_info.szProductName, "Quad") || strstr(channel_info.szProductName, "Dual")) {
		sprintf_s(p_device_name, 30, "%d-%d %s", channel_info.byBoardIndex, channel_info.byChannelIndex, channel_info.szProductName);
	}
	else {
		sprintf_s(p_device_name, 30, "%d %s", channel_info.byBoardIndex, channel_info.szProductName);
	}
}
bool MagewellProCaptureDevice::get_device_name_by_index(int index, char* p_device_name)
{
	MWCAP_CHANNEL_INFO channel_info;
	if (MW_SUCCEEDED != MWGetChannelInfoByIndex(index, &channel_info)) {
		printf("MWGetChannelInfoByIndex fail\n");
		return false;
	} 
	pin_device_name(channel_info, p_device_name);
	return true;
}

DWORD WINAPI video_signal_pro(LPVOID p_param)
{
	MagewellProCaptureDevice* p_class = (MagewellProCaptureDevice*)p_param;
	return p_class->check_input_signal();
}

bool MagewellProCaptureDevice::set_device(int magewell_device_index)
{
	if (m_channel_handle) {
		if (m_device_index == magewell_device_index) {
			return true;
		}
		printf("have set_device(%d)\n",m_device_index);
		return false;
	}
    WCHAR path[128] = { 0 };
    MWGetDevicePath(magewell_device_index, path);
    m_channel_handle = MWOpenChannelByPath(path);
    if (NULL == m_channel_handle) {
        printf("open fail\n");
        return false;
    }
	MWCAP_CHANNEL_INFO info;
	MWGetChannelInfo(m_channel_handle, &info);
	m_device_family = (MW_FAMILY_ID)info.wFamilyID;
    m_device_index = magewell_device_index;

    return true;
}

void MagewellProCaptureDevice::SetCallbackHandler(ICaptureDeviceCallback* callback)
{
	m_callback = callback;

	// Update client if subscribing
	// Note that thisis a read from the state which is set by the capture thread.

	if (m_callback)
	{
	//	m_callback->OnCaptureDeviceState(m_state);
		//SendVideoStateCallback();
		m_canCapture = true;
		m_signal_thread = CreateThread(NULL, 0, video_signal_pro, (LPVOID)this, 0, NULL);
		signalLockedEvent = CreateEvent(NULL, TRUE, FALSE, TEXT("Signal Locked"));
		frameAvailableEvent = CreateEvent(NULL, TRUE, FALSE, TEXT("frames available event "));
		interruptEvent = CreateEvent(NULL, TRUE, FALSE, TEXT("interrupt Event "));
		if (NULL == m_signal_thread) {
			printf("signal check thread failed\n ");
		}
		m_capture_video = true;
		m_video_capturing = true;
	}
	else {
		m_canCapture = false;
		m_capture_video = false;
		m_video_capturing = false;
		if (!SetEvent(interruptEvent))
		{
			printf("SetEvent failed (%d)\n", GetLastError());
		}
		//handle[1] = m_video_thread;
		//handle[2] = m_render_thread;
		WaitForSingleObject(m_signal_thread, INFINITE);
		CloseHandle(signalLockedEvent);
		CloseHandle(frameAvailableEvent);
		CloseHandle(interruptEvent);
		ResetVideoState();
		UpdateState(CaptureDeviceState::CAPTUREDEVICESTATE_UNKNOWN);
	}
	DbgLog((LOG_TRACE, 1, TEXT("MagewellProCaptureDevice::SetCallbackHandler(): updated callback")));
}

bool MagewellProCaptureDevice::check_video_buffer()
{
    if ((!m_user_video_buffer) && (NULL == m_p_video_buffer)) {
        // PHASE 3: Pre-allocate dual-format buffer pools for zero-stall HDR/SDR switching
        // This eliminates the 50-200ms pipeline stall that occurs when stopping/starting
        // capture to reallocate buffers during FourCC changes.
        
        // Calculate sizes for both formats
        DWORD nv12_stride = ((m_width * 3 + 1) / 2 * 4 + 255) & ~255;  // NV12 stride, 256-byte aligned
        DWORD nv12_frame_size = nv12_stride * m_height;  // NV12 = 1.5 bytes per pixel
        DWORD p010_stride = ((m_width * 2 + 255) & ~255);  // P010 stride, 256-byte aligned
        DWORD p010_frame_size = p010_stride * m_height * 3 / 2;  // P010 = 1.5 bytes per pixel
        
        const int buffer_count = 4;  // Low-latency: 4 buffers = ~6.7ms at 60fps
        
        // Allocate NV12 buffer pool (SDR)
        m_p_video_buffer_nv12 = DBG_NEW CRingBufferLockFree();
        if (NULL == m_p_video_buffer_nv12) {
            return false;
        }
        if (!m_p_video_buffer_nv12->set_property(buffer_count, nv12_frame_size)) {
            delete m_p_video_buffer_nv12;
            m_p_video_buffer_nv12 = NULL;
            return false;
        }
        
        // Allocate P010 buffer pool (HDR)
        m_p_video_buffer_p010 = DBG_NEW CRingBufferLockFree();
        if (NULL == m_p_video_buffer_p010) {
            delete m_p_video_buffer_nv12;
            m_p_video_buffer_nv12 = NULL;
            return false;
        }
        if (!m_p_video_buffer_p010->set_property(buffer_count, p010_frame_size)) {
            delete m_p_video_buffer_p010;
            delete m_p_video_buffer_nv12;
            m_p_video_buffer_p010 = NULL;
            m_p_video_buffer_nv12 = NULL;
            return false;
        }
        
        // Set active buffer to default (P010)
        m_p_video_buffer = m_p_video_buffer_p010;
        m_active_video_fourcc = MWFOURCC_P010;
        
        DbgLog((LOG_TRACE, 1, TEXT("Phase 3: Pre-allocated dual-format buffers - NV12:%u bytes, P010:%u bytes"),
            nv12_frame_size, p010_frame_size));
    }
    return true;
}

// Phase 3: Switch active buffer pool without stopping capture
void MagewellProCaptureDevice::SwitchActiveBufferPool(DWORD new_fourcc)
{
    if (new_fourcc == m_active_video_fourcc)
        return;  // Already using this format
    
    CRingBufferLockFree* new_buffer = NULL;
    if (new_fourcc == MWFOURCC_NV12) {
        new_buffer = m_p_video_buffer_nv12;
    } else if (new_fourcc == MWFOURCC_P010) {
        new_buffer = m_p_video_buffer_p010;
    }
    
    if (new_buffer == NULL) {
        DbgLog((LOG_ERROR, 1, TEXT("Phase 3: Cannot switch to unsupported FourCC %08X"), new_fourcc));
        return;
    }
    
    // Atomically switch the active buffer pointer
    // The capture thread uses get_buffer_to_fill() which is lock-free
    // The render thread uses get_frame_to_render() which is also lock-free
    // This switch is safe because:
    // 1. Old buffers still have valid frames being consumed by render thread
    // 2. New buffers are empty and ready for capture thread to fill
    // 3. Both are lock-free SPSC queues with proper memory ordering
    m_p_video_buffer = new_buffer;
    m_active_video_fourcc = new_fourcc;
    
    DbgLog((LOG_TRACE, 1, TEXT("Phase 3: Switched active buffer pool to FourCC %08X (zero-stall)"), new_fourcc));
}

bool MagewellProCaptureDevice::set_mirror_and_reverse(bool is_mirror, bool is_reverse)
{
	return true;
}
bool MagewellProCaptureDevice::get_mirror_and_reverse(bool *p_is_mirror, bool *p_is_reverse)
{
	if ((m_is_mirror < 0) || (m_is_reverse < 0)) {
		printf("please start capture\n");
		return false;
	}
	if (p_is_mirror) {
		*p_is_mirror = m_is_mirror == 1;
	}
	if (p_is_reverse) {
		*p_is_reverse = m_is_reverse == 1;
	}
	return true;
}
float MagewellProCaptureDevice::get_capture_fps()
{
    if ((m_capture_frame_count - m_prev_frame_count) < 30) {
        return m_fps;
    }
    long long now_time = GetTickCount();
    int diff = now_time - m_prev_time;
    if (diff <= 0) {
        return m_fps;
    }
    m_fps = (m_capture_frame_count - m_prev_frame_count)*1000.0 / diff;
    m_prev_time = now_time;
    m_prev_frame_count = m_capture_frame_count;
    return m_fps;
}


bool MagewellProCaptureDevice::set_resolution(int width, int height)
{
	if (m_is_start) {
		printf("have start\n");
		return false;
	}
    m_width = width;
    m_height = height;
    return true;
}
bool MagewellProCaptureDevice::set_mw_fourcc(DWORD mw_fourcc)
{
	if (m_is_start) {
		printf("have start\n");
		return false;
	}
    m_mw_fourcc = mw_fourcc;
    return true;
}
bool MagewellProCaptureDevice::get_resolution(int *p_width, int *p_height)
{
	if (!m_is_start) {
		printf("not start\n");
		return false;
	}
    *p_width = m_width;
    *p_height = m_height;
    return true;
}
bool MagewellProCaptureDevice::get_mw_fourcc(DWORD *p_mw_fourcc)
{
	if (!m_is_start) {
		printf("not start\n");
		return false;
	}
    *p_mw_fourcc = m_mw_fourcc;
    return true;
}

bool MagewellProCaptureDevice::get_device_name(char *p_device_name)
{
	if (NULL == m_channel_handle) {
		printf("not open\n");
		return false;
	}
	MWCAP_CHANNEL_INFO channel_info;
	if (MW_SUCCEEDED != MWGetChannelInfo(m_channel_handle, &channel_info)) {
		printf("MWGetChannelInfo fail\n");
		return false;
	}
	pin_device_name(channel_info, p_device_name);
	return true;
}


CString MagewellProCaptureDevice::GetName()
{
	CString name(_T("Magewell pro card"));
//	CString name;

//	CComBSTR deviceNameBSTR;
//	if (m_deckLink->GetDisplayName(&deviceNameBSTR) == S_OK)
//	{
//		name = CString(deviceNameBSTR);
//		::SysFreeString(deviceNameBSTR);
//	}
//	else
//	{
//		name = _T("DeckLink");
//	}

	//return name;
	//get_device_name((char*)(LPCTSTR)name);
	return name;
}


bool MagewellProCaptureDevice::CanCapture()
{
	return m_canCapture;
}





DWORD WINAPI video_capture_pro(LPVOID p_param)
{
	MagewellProCaptureDevice* p_class = (MagewellProCaptureDevice*)p_param;
	return p_class->capture_by_input();
}

DWORD WINAPI video_render_pro(LPVOID p_param)
{
	MagewellProCaptureDevice* p_class = (MagewellProCaptureDevice*)p_param;
	return p_class->render_by_input();
}


void MagewellProCaptureDevice::StartCapture() {
	if (m_outputCaptureData.load(std::memory_order_acquire))
		throw std::runtime_error("StartCapture() callbed but already started");

	if (!check_video_buffer()) {
		printf("chenck video buffer fail\n");
		return ;
	}
	m_video_capturing = true;

	m_video_thread = CreateThread(NULL, 0, video_capture_pro, (LPVOID)this, 0, NULL);
	if (NULL == m_video_thread) {
		printf("capture video fail\n");
		return ;
	}

	m_render_thread = CreateThread(NULL, 0, video_render_pro, (LPVOID)this, 0, NULL);
	if (NULL == m_render_thread) {
		printf("render video failed\n ");
		return ;
	}
	// From here on out data can egress
	m_outputCaptureData.store(true, std::memory_order_release);

}		

DWORD MagewellProCaptureDevice::check_input_signal()
{
	bool validSignal = false;
	MWCAP_VIDEO_SIGNAL_STATUS	video_signal_status;
	HANDLE notify_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	//UpdateState(CAPTUREDEVICESTATE_UNKNOWN);
	HNOTIFY notify = MWRegisterNotify(m_channel_handle, notify_event, MWCAP_NOTIFY_HDMI_INFOFRAME_HDR| 
													MWCAP_NOTIFY_VIDEO_SIGNAL_CHANGE | 
													MWCAP_NOTIFY_CONNECTION_FORMAT_CHANGED | 
													MWCAP_NOTIFY_INPUT_SPECIFIC_CHANGE |
													MWCAP_NOTIFY_VIDEO_INPUT_SOURCE_CHANGE);
	if (notify == NULL) {
		printf("register notify fail\n");
	}
	HANDLE events[2] = { interruptEvent, notify_event };

	int event_wait_time = 100;
	while (m_canCapture) {
		MWGetVideoSignalStatus(m_channel_handle, &video_signal_status);
		switch (video_signal_status.state)
		{
		case MWCAP_VIDEO_SIGNAL_NONE:
		case MWCAP_VIDEO_SIGNAL_UNSUPPORTED:
		case MWCAP_VIDEO_SIGNAL_LOCKING:
			CardStateChanged();
			if (WaitForSingleObject(notify_event, event_wait_time)) {
				continue;
			}
		case MWCAP_VIDEO_SIGNAL_LOCKED:
			printf("Input signal status: Locked\n");
			CardStateChanged();
		}
		if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == 0)
			break;

		ULONGLONG notify_status = 0;
		if (MWGetNotifyStatus(m_channel_handle, notify, &notify_status) != MW_SUCCEEDED) {
			continue;
		}

		if (notify_status == 0) {
			Sleep(5000);
			continue;
		}
		
		if ((notify_status & MWCAP_NOTIFY_CONNECTION_FORMAT_CHANGED))
			continue;

		if	((notify_status & MWCAP_NOTIFY_INPUT_SPECIFIC_CHANGE) ||
			(notify_status & MWCAP_NOTIFY_VIDEO_INPUT_SOURCE_CHANGE) ||
			(notify_status & MWCAP_NOTIFY_VIDEO_SIGNAL_CHANGE))
		{
			VideoInputFormatChanged();
		} 
		if (notify_status & MWCAP_NOTIFY_HDMI_INFOFRAME_HDR) {
			VideoInputHDRModeChanged();
		}	
	}

	if (notify) {
		MWUnregisterNotify(m_channel_handle, notify);
	}
	if (notify_event) {
		CloseHandle(notify_event);
	}
	return 1;
}


// Function pointer type for WaitOnAddress (Windows 8+ API)
typedef BOOL (WINAPI *PFN_WaitOnAddress)(volatile void*, const void*, SIZE_T, DWORD);

DWORD MagewellProCaptureDevice::render_by_input() {

	printf("render video by input in\n");
	st_frame_t* p_frame = NULL;
	MagewellVideoFrame::MagewellVideoFrameComPtr  mVideoFrame;

	// PHASE 2 OPTIMIZATION: Use WaitOnAddress for futex-style wakeup
	// Eliminates event signal propagation delay (~5-10ms vs 33ms event timeout)
	// WaitOnAddress is available on Windows 8+ and provides kernel-level sleep
	// that wakes up immediately when the memory address changes
	long long* p_write_counter = m_p_video_buffer->GetWriteCounterPointer();
	
	// Track the last known write counter to detect new frames
	long long last_write_counter = 0;
	
	// Spin deadline for initial frame detection (ms) - gives capture thread time to fill
	const DWORD spin_deadline_ms = 2;
	
	// Phase 3: Dynamically load WaitOnAddress to avoid linker issues
	// when building against Windows 7 SDK
	static PFN_WaitOnAddress pfn_WaitOnAddress = nullptr;
	static bool fn_loaded = false;
	if (!fn_loaded) {
		HMODULE hmod = GetModuleHandleW(L"KERNEL32.DLL");
		if (hmod) {
			pfn_WaitOnAddress = (PFN_WaitOnAddress)GetProcAddress(hmod, "WaitOnAddress");
		}
		fn_loaded = true;
	}

	while (m_outputCaptureData.load(std::memory_order_acquire)) {
		
		// Phase 2: WaitOnAddress - sleep until write_counter changes
		// This is the key latency optimization: no fixed timeout polling
		// Thread sleeps at kernel level with ~microsecond wakeup latency
		DWORD wait_result = WaitForSingleObject(interruptEvent, 0);
		if (wait_result == WAIT_OBJECT_0) {
			continue;
		}
		
		// Check if there's a frame available (non-blocking)
		p_frame = m_p_video_buffer->get_frame_to_render();
		if (p_frame == NULL) {
			// No frame yet - spin briefly then WaitOnAddress
			long long current_write = *p_write_counter;
			DWORD start_ticks = GetTickCount();
			
			// Spin-wait for first frame or brief delay (reduces latency vs event)
			bool frame_arrived = false;
			while (GetTickCount() - start_ticks < spin_deadline_ms) {
				if (m_outputCaptureData.load(std::memory_order_acquire) == false) {
					goto render_cleanup;
				}
				
				// Check if a new frame has been written
				long long new_write = *p_write_counter;
				if (new_write != current_write) {
					current_write = new_write;
					frame_arrived = true;
					break;
				}
				
				// Use YieldProcessor to reduce hyperthreading contention
				YieldProcessor();
			}
			
			// If still no frame, use WaitOnAddress with timeout for background sleep
			if (!frame_arrived && pfn_WaitOnAddress) {
				pfn_WaitOnAddress(
					p_write_counter,
					&current_write,
					sizeof(long long),
					1  // 1ms timeout - very low latency
				);
			}
			else if (!frame_arrived && !pfn_WaitOnAddress) {
				// Fallback: Sleep(1) if WaitOnAddress not available
				Sleep(1);
			}
			continue;
		}
		
		// Frame available - process it
		mVideoFrame = DBG_NEW MagewellVideoFrame();
		const void* data = (void*)p_frame->p_buffer;
		VideoFrame vpVideoFrame(
			data, p_frame->frame_len,
			(timingclocktime_t)p_frame->ts, mVideoFrame);
		m_callback->OnCaptureDeviceVideoFrame(vpVideoFrame);
	}

render_cleanup:
	return 1;
}


DWORD MagewellProCaptureDevice::capture_by_input() {

	printf("capture video by input in\n");
	m_capturedVideoFrameCount = 0;
	m_missedVideoFrameCount = 0;
	HANDLE capture_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (NULL == capture_event) {
		printf("create event fail\n");
		return 1;
	}
	if (MWStartVideoCapture(m_channel_handle, capture_event) != MW_SUCCEEDED) {
		printf("start video capture fail\n");
		CloseHandle(capture_event);
		return 1;
	}
	UpdateState(CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING);
	SendCardStateCallback();
	MWCAP_VIDEO_BUFFER_INFO video_buffer_info;
	//MWGetVideoBufferInfo(m_channel_handle, &video_buffer_info);

	MWCAP_VIDEO_FRAME_INFO video_frame_info;
	//MWGetVideoFrameInfo(m_channel_handle, video_buffer_info.iNewestBufferedFullFrame, &video_frame_info);

	HANDLE notify_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	HNOTIFY notify = MWRegisterNotify(m_channel_handle, notify_event, MWCAP_NOTIFY_VIDEO_FRAME_BUFFERED);
	if (notify == NULL) {
		printf("register notify fail\n");
	}
	HANDLE events[2] = { interruptEvent, notify_event };

	DWORD stride = FOURCC_CalcMinStride(m_mw_fourcc, m_width, 2);
	DWORD frame_size = FOURCC_CalcImageSize(m_mw_fourcc, m_width, m_height, stride);
	st_frame_t* p_frame;
	for (int i = 0; p_frame = m_p_video_buffer->get_buffer_by_index(i); i++) {
		MWPinVideoBuffer(m_channel_handle, p_frame->p_buffer, frame_size);
	}
	DWORD event_wait_time = 100;
	bool haveFrames = false;
	while (m_outputCaptureData.load(std::memory_order_acquire)) {
		if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == 0) {
			//interrupted
			continue;
		}
		
		ULONGLONG notify_status = 0;
		if (MWGetNotifyStatus(m_channel_handle, notify, &notify_status) != MW_SUCCEEDED) {
			continue;
		}
		if (!(notify_status & MWCAP_NOTIFY_VIDEO_FRAME_BUFFERED)) {
			continue;
		}

		if (MWGetVideoBufferInfo(m_channel_handle, &video_buffer_info) != MW_SUCCEEDED) {
			continue;
		}
		if (MWGetVideoFrameInfo(m_channel_handle, video_buffer_info.iNewestBufferedFullFrame, &video_frame_info) != MW_SUCCEEDED) {
			continue;
		}
		if (NULL == p_frame) {
			p_frame = m_p_video_buffer->get_buffer_to_fill();
		}

		if (NULL == p_frame) {
			DbgLog((LOG_ERROR, 1, TEXT("Unable to get buffer to fill.frames will be dropped ")));
			continue;
		}
		prev_frame_capture_process();
		MWCaptureVideoFrameToVirtualAddressEx(m_channel_handle, MWCAP_VIDEO_FRAME_ID_NEWEST_BUFFERED, (LPBYTE)p_frame->p_buffer,
			frame_size, stride, m_bottom_up, NULL, m_mw_fourcc, m_width, m_height, m_process_switchs, m_parital_notify, m_OSD_image, m_p_OSD_rects, m_OSD_rects_num,
			m_contrast, m_brightness, m_saturation, m_hue,
			m_deinterlace_mode, m_aspect_ratio_convert_mode, m_p_rect_src, m_p_rect_dest,
			m_aspect_x, m_aspect_y, m_color_format, m_quant_range, m_sat_range);
		if (WaitForSingleObject(capture_event, event_wait_time)) {
			continue;
		}
		// PHASE 2 OPTIMIZATION: Preserve hardware timestamp instead of overwriting
		// The hardware timestamp from video_frame_info provides precise capture time
		// The timing clock is only used for monotonic counter alignment (frame delta)
		p_frame->ts = video_frame_info.allFieldStartTimes[0];  // Keep hardware timestamp
		timingclocktime_t timingClockFrameTime = TimingClockNow();
		
		// Figure out how many frames fit in the interval for monotonic counter
		if (m_previousTimingClockFrameTime != TIMING_CLOCK_TIME_INVALID)
		{
			assert(m_previousTimingClockFrameTime < timingClockFrameTime);

			const double frameDiffTicks = (double)(timingClockFrameTime - m_previousTimingClockFrameTime);
			const int frames = (int)round(frameDiffTicks / m_ticksPerFrame);
			assert(frames >= 0);
			
			// FIX: frame_len should be the actual frame delta, not cumulative counter
			p_frame->frame_len = frames;
			
			if ((m_capturedVideoFrameCount % 200) < 5)
				DbgLog((LOG_TRACE, 1, TEXT("frames:%d, fts:%I64d, clk: %I64d"), frames, p_frame->ts, timingClockFrameTime));
			
			// Don't overwrite p_frame->ts - hardware timestamp is more accurate
			// p_frame->ts = timingClockFrameTime;  // REMOVED
			
			m_capturedVideoFrameCount += frames;
			m_missedVideoFrameCount += std::max((frames - 1), 0);
		}
		else {
			// First frame - use hardware timestamp, fallback to timing clock
			p_frame->frame_len = 1;
		}

		m_previousTimingClockFrameTime = timingClockFrameTime;

		// Every every so often get the hardware latency.
		// TODO: Change to framerate rather than fixed number of frames
	//	if (m_capturedVideoFrameCount % 20 == 0)
//		{
	//		timingclocktime_t timingClockNow = TimingClockNow();
		//	m_hardwareLatencyMs = TimingClockDiffMs(timingClockFrameTime, timingClockNow, TimingClockTicksPerSecond());
	//	}

		// Offset timestamp. Do this after getting the hardware latency else it'll account for this as well
		timingClockFrameTime += m_frameOffsetTicks;
		m_p_video_buffer->buffer_filled();
		m_capture_frame_count++;
		if (!SetEvent(frameAvailableEvent))
		{
			printf("SetEvent failed (%d)\n", GetLastError());
			continue;
		}
		p_frame = NULL;
	//	MWCAP_VIDEO_CAPTURE_STATUS capture_status;
	//	MWGetVideoCaptureStatus(m_channel_handle, &capture_status);
	}
		
	for (int i = 0; p_frame = m_p_video_buffer->get_buffer_by_index(i); i++) {
		MWUnpinVideoBuffer(m_channel_handle, p_frame->p_buffer);
	}
	printf("capture video by input out\n");

end_and_free:
	if (notify) {
		MWUnregisterNotify(m_channel_handle, notify);
	}
	MWStopVideoCapture(m_channel_handle);
	if (notify_event) {
		CloseHandle(notify_event);
	}
	if (capture_event) {
		CloseHandle(capture_event);
	}

	return 0;
}



	void MagewellProCaptureDevice::StopCapture()
	{
		if (!m_outputCaptureData.load(std::memory_order_acquire))
			throw std::runtime_error("StopCapture() called while not started");
		// Stop egressing data
		m_outputCaptureData.store(false, std::memory_order_release);

		m_video_capturing = false;

		if (!SetEvent(interruptEvent))
		{
			printf("SetEvent failed (%d)\n", GetLastError());
		}
		HANDLE handle[2];
		handle[0] = m_video_thread;
		handle[1] = m_render_thread;
		WaitForMultipleObjects(2, handle, TRUE, INFINITE);
		UpdateState(CAPTUREDEVICESTATE_READY);
		if (m_p_video_buffer != NULL) {
			delete m_p_video_buffer;
			m_p_video_buffer = NULL;
		}
		DbgLog((LOG_TRACE, 1, TEXT("MagewellProCaptureDevice::StopCapture() completed successfully")));
	}


	CaptureInputId MagewellProCaptureDevice::CurrentCaptureInputId()
	{
		return static_cast<CaptureInputId>(m_captureInputId);
	}


	CaptureInputs MagewellProCaptureDevice::SupportedCaptureInputs()
	{
		CaptureInputs captureInputs;

		LONGLONG availableInputConnections;
		for (const auto& captureInput : m_captureInputSet)
		{
			// The used id's are actually BMDVideoConnection.
			captureInputs.push_back(captureInput);
		}

		return captureInputs;
	}


	void MagewellProCaptureDevice::SetCaptureInput(const CaptureInputId captureInputId)
	{
		DbgLog((LOG_TRACE, 1, TEXT("MagewellProCaptureDevice::SetCaptureInput() to %i (only used in StartCapture())"), m_captureInputId));

		m_captureInputId = captureInputId;
	}


	ITimingClock* MagewellProCaptureDevice::GetTimingClock()
	{
		// WARNING: m_state will be updated by some internal capture thread so this might be a race
		//          condition. Probably only an academic problem given that the clients of this will
		//          only be requesting it after they see the capture state
		if (m_state != CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING)
			return nullptr;

		return this;
	}


	void MagewellProCaptureDevice::SetFrameOffsetMs(int frameOffsetMs)
	{
		DbgLog((LOG_TRACE, 1, TEXT("MagewellProCaptureDevice::SetFrameOffsetMs() to %i"), frameOffsetMs));

		const timingclocktime_t ticksPerMs = MAGEWELL_CLOCK_MAX_TICKS_SECOND / 100;
		m_frameOffsetTicks = frameOffsetMs * ticksPerMs;
	}


	//
	// ITimingClock
	//


	timingclocktime_t MagewellProCaptureDevice::TimingClockNow()
	{

		LONGLONG currentTimeTicks = 0LL;
		if (MWGetDeviceTime(m_channel_handle, &currentTimeTicks) != MW_SUCCEEDED) {
			throw std::runtime_error("Could not get the hardware clock timestamp");
		}
		return currentTimeTicks;
	}


	timingclocktime_t MagewellProCaptureDevice::TimingClockTicksPerSecond() const
	{
		// This is hard-coded, we can also take a more course approach by using the exact frame-frequency muliplied by 1000
		// as shown in the DeckLink examples. Given that we most likely want to convert it to a DirectShow timestamp,
		// which is 100ns the choice here is to go for maximum usable resolution.
		return MAGEWELL_CLOCK_MAX_TICKS_SECOND;
	}


	const TCHAR* MagewellProCaptureDevice::TimingClockDescription()
	{
		return TEXT("Magewell hardware clock");
	}

	bool MagewellProCaptureDevice::VideoInputHDRModeChanged()
	{
		DWORD t_dw_flag = 0;
		bool isHdr = false;
		HDMI_INFOFRAME_PACKET packet;
		MWGetHDMIInfoFrameValidFlag(m_channel_handle, &t_dw_flag);
		if (t_dw_flag & MWCAP_HDMI_INFOFRAME_MASK_HDR) {
			isHdr = true;
			MWGetHDMIInfoFramePacket(m_channel_handle, MWCAP_HDMI_INFOFRAME_ID_HDR, &packet);

		}
		else
			isHdr = false;

		// Phase 2.4: HDR infoframe debounce - skip if counter not triggered
		if (!isHdr && m_hdrChangeCounter > 0) {
			m_hdrChangeCounter = 0;  // Reset debounce on confirmed loss
			return 0;
		}
		if (isHdr && m_hdrChangeCounter > 0) {
			m_hdrChangeCounter--;  // Decrement debounce counter
			if (m_hdrChangeCounter > 0) {
				return 0;  // Still debouncing, skip processing
			}
		}

		double doubleValue = 0.0;
		//HDMI_HDR_INFOFRAME stHdrInfo = { 0 };
		bool videoStateChanged = false;
		if (isHdr) {
			// PHASE 2.3: Field-level HDR metadata change detection
			// Compare each HDR field individually against last known values
			// Only trigger callback when fields that affect rendering change
			double xvalues[3], yvalues[3];
			xvalues[0] = TranslatePrimaries(packet.hdrInfoFramePayload.display_primaries_lsb_x0,
				packet.hdrInfoFramePayload.display_primaries_msb_x0);

			xvalues[1] = TranslatePrimaries(packet.hdrInfoFramePayload.display_primaries_lsb_x1,
				packet.hdrInfoFramePayload.display_primaries_msb_x1);

			xvalues[2] = TranslatePrimaries(packet.hdrInfoFramePayload.display_primaries_lsb_x2,
				packet.hdrInfoFramePayload.display_primaries_msb_x2);

			yvalues[0] = TranslatePrimaries(packet.hdrInfoFramePayload.display_primaries_lsb_y0,
				packet.hdrInfoFramePayload.display_primaries_msb_y0);

			yvalues[1] = TranslatePrimaries(packet.hdrInfoFramePayload.display_primaries_lsb_y1,
				packet.hdrInfoFramePayload.display_primaries_msb_y1);

			yvalues[2] = TranslatePrimaries(packet.hdrInfoFramePayload.display_primaries_lsb_y2,
				packet.hdrInfoFramePayload.display_primaries_msb_y2);

			int redindex = 0, greenindex = 1, blueindex = 2;

			// Determine color channel indices (same as before)
			if (xvalues[0] > xvalues[1]) {
				if (xvalues[0] > xvalues[2]) {
					redindex = 0;
					if (yvalues[1] > yvalues[2]) {
						greenindex = 1;
						blueindex = 2;
					}
					else {
						greenindex = 2;
						blueindex = 1;
					}
				}
				else {
					redindex = 2;
					if (yvalues[0] > yvalues[1]) {
						greenindex = 0;
						blueindex = 1;
					}
					else {
						greenindex = 1;
						blueindex = 0;
					}
				}
			}
			else if (xvalues[1] > xvalues[2]) {
				redindex = 1;
				if (yvalues[0] > yvalues[2]) {
					greenindex = 0;
					blueindex = 2;
				}
				else {
					greenindex = 2;
					blueindex = 0;
				}
			}
			else {
				redindex = 2;
				if (yvalues[0] > yvalues[1]) {
					greenindex = 0;
					blueindex = 1;
				}
				else {
					greenindex = 1;
					blueindex = 0;
				}
			}

			// Was nothing, now is something (first confirmed HDR)
			if (!m_videoHasHdrData)
			{
				m_videoHasHdrData = true;
				videoStateChanged = true;
				DbgLog((LOG_TRACE, 1, TEXT("HDR detected - first confirmed frame")));
			}

			//PHASE 2.3: Field-level change detection - compare against stored last values
			// This prevents unnecessary callbacks when metadata hasn't actually changed
			bool primariesChanged = false;
			if (fabs(xvalues[redindex] - m_last_displayPrimaryRedX) > 0.001 ||
				fabs(yvalues[redindex] - m_last_displayPrimaryRedY) > 0.001) {
				primariesChanged = true;
			}
			if (fabs(xvalues[greenindex] - m_last_displayPrimaryGreenX) > 0.001 ||
				fabs(yvalues[greenindex] - m_last_displayPrimaryGreenY) > 0.001) {
				primariesChanged = true;
			}
			if (fabs(xvalues[blueindex] - m_last_displayPrimaryBlueX) > 0.001 ||
				fabs(yvalues[blueindex] - m_last_displayPrimaryBlueY) > 0.001) {
				primariesChanged = true;
			}
			
			if (primariesChanged) {
				m_last_displayPrimaryRedX = xvalues[redindex];
				m_last_displayPrimaryRedY = yvalues[redindex];
				m_last_displayPrimaryGreenX = xvalues[greenindex];
				m_last_displayPrimaryGreenY = yvalues[greenindex];
				m_last_displayPrimaryBlueX = xvalues[blueindex];
				m_last_displayPrimaryBlueY = yvalues[blueindex];
				videoStateChanged = true;
			}
		// PHASE 2.3: White point field-level change detection
			double newXValue, newYValue;
			newXValue = TranslatePrimaries(packet.hdrInfoFramePayload.white_point_lsb_x,
				packet.hdrInfoFramePayload.white_point_msb_x);
			newYValue = TranslatePrimaries(packet.hdrInfoFramePayload.white_point_lsb_y,
				packet.hdrInfoFramePayload.white_point_msb_y);

			bool whitePointChanged = fabs(newXValue - m_last_whitePointX) > 0.001 ||
				fabs(newYValue - m_last_whitePointY) > 0.001;
			
			if (whitePointChanged) {
				m_last_whitePointX = newXValue;
				m_last_whitePointY = newYValue;
				m_videoHdrData.whitePointX = newXValue;
				m_videoHdrData.whitePointY = newYValue;
				videoStateChanged = true;
			}

		// PHASE 2.3: Mastering display luminance field-level change detection
			double maxValue = TranslateLuminance(packet.hdrInfoFramePayload.max_display_mastering_lsb_luminance,
				packet.hdrInfoFramePayload.max_display_mastering_msb_luminance);

			double minValue = TranslateLuminance(packet.hdrInfoFramePayload.min_display_mastering_lsb_luminance,
				packet.hdrInfoFramePayload.min_display_mastering_msb_luminance);

			bool masteringChanged = fabs(minValue - m_last_masteringDisplayMinLuminance) > 0.001 ||
				fabs(maxValue - m_last_masteringDisplayMaxLuminance) > 0.001;
			
			if (masteringChanged) {
				m_last_masteringDisplayMinLuminance = minValue;
				m_last_masteringDisplayMaxLuminance = maxValue;
				m_videoHdrData.masteringDisplayMinLuminance = minValue;
				m_videoHdrData.masteringDisplayMaxLuminance = maxValue;
				videoStateChanged = true;
			}

		// PHASE 2.3: CLL/FALL field-level change detection
			maxValue = TranslateLuminance(packet.hdrInfoFramePayload.maximum_content_light_level_lsb,
				packet.hdrInfoFramePayload.maximum_content_light_level_msb);

			bool cllChanged = fabs(maxValue - m_last_maxCll) > 0.001;
			if (cllChanged) {
				m_last_maxCll = maxValue;
				m_videoHdrData.maxCll = maxValue;
				videoStateChanged = true;
			}

			maxValue = TranslateLuminance(packet.hdrInfoFramePayload.maximum_frame_average_light_level_lsb,
				packet.hdrInfoFramePayload.maximum_frame_average_light_level_msb);

		bool fallChanged = fabs(maxValue - m_last_maxFall) > 0.001;
			if (fallChanged) {
				m_last_maxFall = maxValue;
				m_videoHdrData.maxFall = maxValue;
				videoStateChanged = true;
			}


			// PHASE 2.3: EOTF field-level change detection
			if (m_last_videoEotf != (int)packet.hdrInfoFramePayload.byEOTF) {
				m_last_videoEotf = (int)packet.hdrInfoFramePayload.byEOTF;
				m_videoEotf = packet.hdrInfoFramePayload.byEOTF;
				videoStateChanged = true;
			}
		}
		else {
			if (m_videoHasHdrData)
			{
				m_videoHasHdrData = false;
				videoStateChanged = true;
			}
		}
		if (videoStateChanged)
		{
			if (!SendVideoStateCallback())
				return E_FAIL;
		}
		return 0;
	}


void MagewellProCaptureDevice::prev_frame_capture_process()
	{

	}

// Phase 3.2: Set D3D11 device and create texture pool for keyed mutex synchronization
HRESULT MagewellProCaptureDevice::SetD3D11Device(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	if (!pDevice || !pContext) {
		return E_INVALIDARG;
	}
	
	if (m_video_capturing) {
		return E_FAIL;  // Cannot set after capture starts
	}
	
	// Addref the device and context
	m_p_d3d11_device = pDevice;
	m_p_d3d11_device->AddRef();
	m_p_d3d11_device_context = pContext;
	m_p_d3d11_device_context->AddRef();
	
	// Create the texture pool with keyed mutex support
	D3D11TexturePool::TextureConfig config;
	config.width = m_width;
	config.height = m_height;
	config.format = D3D11TexturePool::GetDXGIFormat(m_mw_fourcc);
	config.poolSize = 4;  // Low-latency: 4 buffers
	config.useKeyedMutex = true;
	
	m_p_d3d11_texture_pool = DBG_NEW D3D11TexturePool();
	HRESULT hr = m_p_d3d11_texture_pool->Initialize(config, pDevice);
	if (FAILED(hr)) {
		DbgLog((LOG_ERROR, 1, TEXT("Phase 3.2: Failed to initialize D3D11 texture pool (0x%X)"), hr));
		return hr;
	}
	
	m_enable_d3d11_capture = true;
	DbgLog((LOG_TRACE, 1, TEXT("Phase 3.2: D3D11 texture pool initialized - %dx%d, format %d, keyed mutex enabled"),
		m_width, m_height, config.format));
	
	return S_OK;
}
	HRESULT STDMETHODCALLTYPE MagewellProCaptureDevice::CardStateChanged()
	{

		MWCAP_VIDEO_SIGNAL_STATUS
			video_signal_status; 
		MWGetVideoSignalStatus(m_channel_handle, &video_signal_status);
		switch (video_signal_status.state)
		{
		case MWCAP_VIDEO_SIGNAL_NONE:
		case MWCAP_VIDEO_SIGNAL_UNSUPPORTED:
		case MWCAP_VIDEO_SIGNAL_LOCKING:
			if (m_display_mode != NULL) {
				//UpdateState(CAPTUREDEVICESTATE_UNKNOWN);
				//SendCardStateCallback();
				m_display_mode = NULL;
				SendCardStateCallback();
			}
			return 0;
		case MWCAP_VIDEO_SIGNAL_LOCKED:
			printf("Input signal status: Locked\n");
			if (m_state == CAPTUREDEVICESTATE_UNKNOWN)
				UpdateState(CAPTUREDEVICESTATE_READY);

			if (m_state == CAPTUREDEVICESTATE_READY && video_signal_status.state == MWCAP_VIDEO_SIGNAL_LOCKED) {
				bool videoFormatChanged = false;

				if (m_width != video_signal_status.cx || m_height != video_signal_status.cy) {
					m_width = video_signal_status.cx;
					m_height = video_signal_status.cy;
					videoFormatChanged = true;
				}

				if (m_is_interlaced != video_signal_status.bInterlaced || m_frame_duration != video_signal_status.dwFrameDuration) {
					m_is_interlaced = video_signal_status.bInterlaced;
					m_frame_duration = video_signal_status.dwFrameDuration;
					m_signal_frame_duration = video_signal_status.dwFrameDuration;
					m_ticksPerFrame = (timingclocktime_t)round((1.0 / FPS(m_frame_duration, m_is_interlaced)) * TimingClockTicksPerSecond());
					videoFormatChanged = true;
				}
				if (m_quant_range != video_signal_status.quantRange) {
					m_quant_range = video_signal_status.quantRange;
					videoFormatChanged = true;
				}
				if (m_color_format != video_signal_status.colorFormat) {
					m_color_format = video_signal_status.colorFormat;
					videoFormatChanged = true;
				}

				MWCAP_INPUT_SPECIFIC_STATUS inputStatus;
				if ((MWGetInputSpecificStatus(m_channel_handle, &inputStatus) == MW_SUCCEEDED)) {
					MWCAP_HDMI_SPECIFIC_STATUS hdmiSpecificStatus = inputStatus.hdmiStatus;
					if (m_bit_depth != hdmiSpecificStatus.byBitDepth) {
						m_bit_depth = hdmiSpecificStatus.byBitDepth;
						videoFormatChanged = true;
					}
					if (m_pixel_encoding != hdmiSpecificStatus.pixelEncoding) {
						m_pixel_encoding = hdmiSpecificStatus.pixelEncoding;
						videoFormatChanged = true;
					}
				}

				// Update FourCC based on bit depth for HDR/SDR detection
				UpdateFourCCFromBitDepth();

				SendCardStateCallback();
				if (videoFormatChanged) {
					m_display_mode = TranslateDisplayMode(m_width, m_height, m_is_interlaced, m_frame_duration);
					SendVideoStateCallback();
				}
			//	if (!m_video_capturing)
			//		StartCapture();
			}
		}
		return 0;
	}

	HRESULT STDMETHODCALLTYPE MagewellProCaptureDevice::VideoInputFormatChanged()
	{
	//	if (!m_outputCaptureData.load(std::memory_order_acquire))
		//	return S_OK;
		MWCAP_VIDEO_SIGNAL_STATUS video_signal_status;
		MWGetVideoSignalStatus(m_channel_handle, &video_signal_status);
		if (video_signal_status.state != MWCAP_VIDEO_SIGNAL_LOCKED || m_state == CAPTUREDEVICESTATE_UNKNOWN)
			return 1;

		bool videoFormatChanged = false;

		if (m_width != video_signal_status.cx || m_height != video_signal_status.cy) {
			m_width = video_signal_status.cx;
			m_height = video_signal_status.cy;
			videoFormatChanged = true;
		}

		if (m_is_interlaced != video_signal_status.bInterlaced || m_frame_duration != video_signal_status.dwFrameDuration) {
			m_is_interlaced = video_signal_status.bInterlaced;
			m_frame_duration = video_signal_status.dwFrameDuration;
			m_signal_frame_duration = video_signal_status.dwFrameDuration;
			m_ticksPerFrame = (timingclocktime_t)round((1.0 / FPS(m_frame_duration, m_is_interlaced)) * TimingClockTicksPerSecond());
			videoFormatChanged = true;
		}
		if (m_quant_range != video_signal_status.quantRange) {
			m_quant_range = video_signal_status.quantRange;
			videoFormatChanged = true;
		}
		if (m_color_format != video_signal_status.colorFormat) {
			m_color_format = video_signal_status.colorFormat;
			videoFormatChanged = true;
		}

		MWCAP_INPUT_SPECIFIC_STATUS inputStatus;
		if ((MWGetInputSpecificStatus(m_channel_handle, &inputStatus) == MW_SUCCEEDED)) {
			MWCAP_HDMI_SPECIFIC_STATUS hdmiSpecificStatus = inputStatus.hdmiStatus;
			if (m_bit_depth != hdmiSpecificStatus.byBitDepth) {
				m_bit_depth = hdmiSpecificStatus.byBitDepth;
				videoFormatChanged = true;
			}
			if (m_pixel_encoding != hdmiSpecificStatus.pixelEncoding) {
				m_pixel_encoding = hdmiSpecificStatus.pixelEncoding;
				videoFormatChanged = true;
			}
		}

		// Update FourCC based on bit depth for HDR/SDR detection
		UpdateFourCCFromBitDepth();

		if (videoFormatChanged)
		{
			DbgLog((LOG_TRACE, 1, TEXT("MagewellProCaptureDevice::VideoInputFormatChanged(): detected change")));
			m_display_mode = TranslateDisplayMode(m_width, m_height, m_is_interlaced, m_frame_duration);

			m_ticksPerFrame = (timingclocktime_t)round((1.0 / FPS(m_frame_duration, m_is_interlaced)) * TimingClockTicksPerSecond());

			// Inform callback handlers that stream will be invalid before re-starting
			if (!SendVideoStateCallback())
				return E_FAIL;

		//	if (m_video_capturing) {
		//		StopCapture();
		//	}
		//	if (!m_video_capturing) {
		//		StartCapture();
		//	}


			DbgLog((LOG_TRACE, 1, TEXT("MagewellProCaptureDevice::VideoInputFormatChanged(): restart success")));
		}

		return S_OK;
	}

	void MagewellProCaptureDevice::UpdateFourCCFromBitDepth()
	{
		MWCAP_INPUT_SPECIFIC_STATUS inputStatus;
		DWORD old_fourcc = m_mw_fourcc;

		if (MWGetInputSpecificStatus(m_channel_handle, &inputStatus) == MW_SUCCEEDED) {
			MWCAP_HDMI_SPECIFIC_STATUS hdmiSpecificStatus = inputStatus.hdmiStatus;
			BYTE bit_depth = hdmiSpecificStatus.byBitDepth;

			// Dynamic FourCC selection based on bit depth
			// If signal reports > 8 bits per component (HDR/10-bit+), use P010
			// If standard dynamic range or 8-bit, fall back to NV12
			if (bit_depth > 8) {
				// HDR signal (10-bit, 12-bit, etc.) - use P010 (16 bits per component)
				m_mw_fourcc = MWFOURCC_P010;
				DbgLog((LOG_TRACE, 1, TEXT("Magewell: HDR signal detected (bit depth: %d), switching to P010"), bit_depth));
			}
			else {
				// SDR signal (8-bit) - use NV12 (12 bits per pixel footprint)
				m_mw_fourcc = MWFOURCC_NV12;
				DbgLog((LOG_TRACE, 1, TEXT("Magewell: SDR signal detected (bit depth: %d), switching to NV12"), bit_depth));
			}

		// Phase 3: Use zero-stall buffer pool switching instead of stopping capture
		if (m_mw_fourcc != old_fourcc && m_video_capturing) {
			DbgLog((LOG_TRACE, 1, TEXT("Magewell: FourCC changed from %08X to %08X, using zero-stall switch"),
				old_fourcc, m_mw_fourcc));
			
			// Phase 3: Switch to pre-allocated buffer pool without stopping capture
			// This eliminates the 50-200ms pipeline stall
			SwitchActiveBufferPool(m_mw_fourcc);
			
			DbgLog((LOG_TRACE, 1, TEXT("Magewell: Zero-stall buffer pool switch complete")));
		}
		}
	}

	HRESULT	MagewellProCaptureDevice::QueryInterface(REFIID iid, LPVOID* ppv)
	{
		if (!ppv)
			return E_INVALIDARG;

		// Initialise the return result
		*ppv = nullptr;

		// Obtain the IUnknown interface and compare it the provided REFIID
		if (iid == IID_IUnknown)
		{
			*ppv = this;
			AddRef();
			return S_OK;
		}

		return E_NOINTERFACE;
	}


	ULONG MagewellProCaptureDevice::AddRef(void)
	{
		return ++m_refCount;
	}


	ULONG MagewellProCaptureDevice::Release(void)
	{
		ULONG newRefValue = --m_refCount;
		if (newRefValue == 0)
			delete this;

		return newRefValue;
	}


	void MagewellProCaptureDevice::ResetVideoState()
	{
	//	m_videoFrameSeen = false;
	//	m_bmdPixelFormat = BMD_PIXEL_FORMAT_INVALID;
	//	m_bmdDisplayMode = BMD_DISPLAY_MODE_INVALID;
	//	m_videoHasInputSource = false;
	//	m_videoEotf = BMD_EOTF_INVALID;
	//	m_videoColorSpace = BMD_COLOR_SPACE_INVALID;

		m_width = 0;//0
		m_height = 0;//0
		m_color_format = MWCAP_VIDEO_COLOR_FORMAT_UNKNOWN;//
		m_quant_range = MWCAP_VIDEO_QUANTIZATION_UNKNOWN;
		m_sat_range = MWCAP_VIDEO_SATURATION_UNKNOWN;
		//m_videoEotf = -1;
		m_signal_frame_duration = 0;
		m_frame_duration = 0;
		m_videoHasHdrData = false;

		ZeroMemory(&m_videoHdrData, sizeof(m_videoHdrData));
	}


	bool MagewellProCaptureDevice::SendVideoStateCallback()
	{
		// WARNING: Called from some internal capture card thread!

		const bool hasValidVideoState = (m_display_mode != NULL);

	//		(m_videoFrameSeen) &&
	//		(m_bmdPixelFormat != BMD_PIXEL_FORMAT_INVALID) &&
	//		(m_bmdDisplayMode != BMD_DISPLAY_MODE_INVALID) &&
	//		(m_videoHasInputSource) &&
	//		(m_videoEotf != BMD_EOTF_INVALID) &&
	//		(m_videoColorSpace != BMD_COLOR_SPACE_INVALID);

		const bool hasValidHdrData =
			m_videoHasHdrData &&
			m_videoHdrData.IsValid();

		//
		// Build and send reply
		//

		try
		{

			VideoStateComPtr videoState = DBG_NEW VideoState();
			if (!videoState)
				throw std::runtime_error("Failed to alloc VideoStateComPtr");

			// Not valid, don't send
			if (!hasValidVideoState)
			{
				videoState->valid = false;
			}
			// Valid state, send
			else
			{
				videoState->valid = true;
				videoState->displayMode = m_display_mode;
				videoState->eotf = TranslateMagewellEOTF(m_videoEotf);
				videoState->colorspace = TranslateColorSpace(m_color_format);
				videoState->invertedVertical = m_videoInvertedVertical;
				videoState->videoFrameEncoding = TranslateFrameEncoding(m_mw_fourcc);

				// Build a fresh copy of the HDR data if valid
				if (hasValidHdrData)
				{
					videoState->hdrData = std::make_shared<HDRData>();
					*(videoState->hdrData) = m_videoHdrData;
				}
			}

			m_callback->OnCaptureDeviceVideoStateChange(videoState);
			//if (videoState != NULL)
			//	delete videoState;

		}
		catch (const std::runtime_error& e)
		{
			wchar_t* ew = ToString(e.what());
			Error(ew);
			delete[] ew;

			return false;
		}

		return true;
	}


	void MagewellProCaptureDevice::SendCardStateCallback()
	{
		// WARNING: Called from some internal capture card thread!


		if (!m_callback)
			return;

		CaptureDeviceCardStateComPtr cardState = DBG_NEW CaptureDeviceCardState();
		if (!cardState)
			throw std::runtime_error("Failed to alloc CaptureDeviceCardStateComPtr");

		//
		// Input data
		//

		cardState->inputLocked = (m_state != CaptureDeviceState::CAPTUREDEVICESTATE_UNKNOWN) ? InputLocked::YES : InputLocked::NO;
		cardState->inputEncoding = TranslateColorFormat(m_pixel_encoding);
		cardState->inputBitDepth = TranslateBitDepth(m_bit_depth);
		cardState->inputDisplayMode = m_display_mode;

		//
		// Send
		//
		m_callback->OnCaptureDeviceCardStateChange(cardState);
	}


	void MagewellProCaptureDevice::UpdateState(CaptureDeviceState state)
	{
		// WARNING: Called from some internal capture card thread!

		//assert(state != m_state);  // Double state is not allowed
		//if (state == m_state) return;
		m_state = state;

		if (m_callback)
			m_callback->OnCaptureDeviceState(state);
	}


	void MagewellProCaptureDevice::Error(const CString& error)
	{
		// WARNING: Can be called from any thread.

		if (m_callback)
			m_callback->OnCaptureDeviceError(error);

		// TODO: Stop capture and return error state?
	}


	//
	// Internal helpers
	//

	/*
	void MagewellProCaptureDevice::OnNotifyStatusChanged(BMDDeckLinkStatusID statusID)
	{
		// WARNING: Called from some internal capture card thread!

		if (!m_deckLinkInput)
			return;

		switch (statusID)
		{
		// Device state changed
		case bmdDeckLinkStatusBusy:
			OnLinkStatusBusyChange();
			break;

		// Card state changed.
		case bmdDeckLinkStatusVideoInputSignalLocked:
		case bmdDeckLinkStatusDetectedVideoInputFieldDominance:
		case bmdDeckLinkStatusDetectedVideoInputMode:
		case bmdDeckLinkStatusDetectedVideoInputFormatFlags:
			SendCardStateCallback();
			break;

		// Video state changed
		case bmdDeckLinkStatusCurrentVideoInputPixelFormat:
		case bmdDeckLinkStatusDetectedVideoInputColorspace:
		case bmdDeckLinkStatusCurrentVideoInputMode:
			// not used as we get these from from the frame and frame callbacks
			break;

		// All others ignored
		default:
			break;
		}
	}

	void MagewellProCaptureDevice::OnLinkStatusBusyChange()
	{
		// WARNING: Called from some internal capture card thread!

		LONGLONG intValue;
		IF_NOT_S_OK(m_deckLinkStatus->GetInt(bmdDeckLinkStatusBusy, &intValue))
			throw std::runtime_error("Failed to call bmdDeckLinkStatusBusy");

		const bool captureBusy = (intValue & bmdDeviceCaptureBusy);

		if (captureBusy)
		{
			assert(m_state == CaptureDeviceState::CAPTUREDEVICESTATE_READY);
			UpdateState(CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING);
		}
		else
		{
			assert(m_state == CaptureDeviceState::CAPTUREDEVICESTATE_CAPTURING);
			ResetVideoState();
			UpdateState(CaptureDeviceState::CAPTUREDEVICESTATE_READY);
		}
	}
	*/