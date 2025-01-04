/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <vector>
#include <atomic>

#include <LibMWCapture/MWCapture.h>
#include "ring_buffer/ring_buffer.h"

#include <VideoFrame.h>
#include <ACaptureDevice.h>
#include <ITimingClock.h>
#include <AnAudioSource.h>
#include <DirectSoundAudioRenderer.h>

// Known invalid values
//#define BMD_PIXEL_FORMAT_INVALID (BMDPixelFormat)0
//#define BMD_DISPLAY_MODE_INVALID (BMDDisplayMode)0
//#define BMD_TIME_SCALE_INVALID (BMDTimeScale)0
//#define BMD_EOTF_INVALID -1
//#define BMD_COLOR_SPACE_INVALID -1


/**
 * Magewell Pro Capture SDK capable capture device
 */
class MagewellProCaptureDevice:
	public ACaptureDevice,
	public ITimingClock, 
	public AnAudioSource
{
public:
	int										 m_capture_frame_count;

	MagewellProCaptureDevice();
	virtual ~MagewellProCaptureDevice();

	// ACaptureDevice
	void SetCallbackHandler(ICaptureDeviceCallback*) override;
	CString GetName() override;
	bool CanCapture() override;
	void StartCapture() override;
	void StopCapture() override;
	CaptureInputId CurrentCaptureInputId() override;
	CaptureInputs SupportedCaptureInputs() override;
	void SetCaptureInput(const CaptureInputId) override;
	ITimingClock* GetTimingClock() override;
	void SetFrameOffsetMs(int) override;
	double HardwareLatencyMs() const override { return m_hardwareLatencyMs; }
	uint64_t VideoFrameCapturedCount() const override { return m_capturedVideoFrameCount; }
	uint64_t VideoFrameMissedCount() const override { return m_missedVideoFrameCount; }

	// ITimingClock
	timingclocktime_t TimingClockNow() override;
	timingclocktime_t TimingClockTicksPerSecond() const override;
	const TCHAR* TimingClockDescription() override;

	bool VideoInputHDRModeChanged();

	HRESULT __stdcall VideoInputFormatChanged();
	HRESULT __stdcall CardStateChanged();

	// IUnknown
	HRESULT	QueryInterface(REFIID iid, LPVOID* ppv) override;
	ULONG AddRef() override;
	ULONG Release() override;
	HANDLE signalLockedEvent;
	HANDLE frameAvailableEvent;
	HANDLE interruptEvent;
	DWORD check_input_signal();
	DWORD capture_by_input();
	DWORD render_by_input();
	DWORD render_audio_by_input();
	DWORD start_audio_output();
	static void init();
	static void exit();
	static bool refresh_devices();
	static int get_channel_count();
	static bool get_channel_info_by_index(int index, MWCAP_CHANNEL_INFO* p_channel_info);
	static bool get_device_name_by_index(int index, char* p_device_name);

	virtual bool set_device(int magewell_device_index);
	virtual bool set_mirror_and_reverse(bool is_mirror, bool is_reverse);

	float get_capture_fps();

	bool set_resolution(int width, int height);
	bool set_mw_fourcc(DWORD mw_fourcc);
	bool set_audio_channels(int channel_num);
	bool set_audio_sample_rate(int sample_rate);
	bool get_device_name(char* p_device_name);
	bool get_resolution(int* p_width, int* p_height);
	bool get_mw_fourcc(DWORD* p_mw_fourcc);
	bool get_audio_channels(int* p_channel_num);
	bool get_audio_sample_rate(int* p_sample_rate);
	bool get_audio_bit_per_sample(int* p_bit_per_sample);
	bool get_mirror_and_reverse(bool* p_is_mirror, bool* p_is_reverse);

	 void setFormat(WAVEFORMATEX* pwfx) ;
	 bool LoadData(UINT32 bufferFrameCount, BYTE* pData, DWORD* flags) ;
	CRingBuffer* m_p_video_buffer;
	CRingBuffer* m_p_audio_buffer;
protected:
	bool                            m_is_start;
	int								m_width;
	int								m_height;
	DWORD                           m_mw_fourcc;
	int                             m_is_mirror;
	int                             m_is_reverse;

	int                             m_audio_sample_rate;
	int                             m_audio_channel_num;
	int                             m_audio_bit_per_sample;
	DirectSoundAudioRendererComPtr	m_audio_rendrer;
	AnAudioSourceComPtr				m_audio_device;

	bool check_video_buffer();
	bool check_audio_buffer();
	int                             m_signal_frame_duration;
	HCHANNEL                        m_channel_handle;
	MW_FAMILY_ID                    m_device_family;
	int                             m_device_index;
	bool                            m_use_common_api;
	bool                            m_user_video_buffer;
	bool                            m_user_audio_buffer;
	int                             m_prev_frame_count;
	long long                       m_prev_time;
	float                           m_fps;
private:
	virtual void prev_frame_capture_process();
	bool check();
	bool                            m_video_capturing;
	bool                            m_audio_capturing;
	bool                            m_capture_video;
	bool                            m_capture_audio;

	BOOLEAN							m_bottom_up;//false

	DWORD							m_process_switchs;//0
	int								m_parital_notify;//0
	HOSD							m_OSD_image;//NULL
	RECT* m_p_OSD_rects;//NULL
	int								m_OSD_rects_num;//0
	SHORT							m_contrast;//100
	SHORT							m_brightness;//0
	SHORT							m_saturation;//100
	SHORT							m_hue;//0
	MWCAP_VIDEO_DEINTERLACE_MODE			m_deinterlace_mode;//MWCAP_VIDEO_DEINTERLACE_BLEND
	MWCAP_VIDEO_ASPECT_RATIO_CONVERT_MODE	m_aspect_ratio_convert_mode;//MWCAP_VIDEO_ASPECT_RATIO_IGNORE
	RECT* m_p_rect_src;//NULL
	RECT* m_p_rect_dest;//NULL
	int										m_aspect_x;//0
	int										m_aspect_y;//0
	int										m_frame_duration;
	bool									m_is_interlaced;
	HDMI_PXIEL_ENCODING						m_pixel_encoding; //HDMI_ENCODING_YUV_422
	MWCAP_VIDEO_COLOR_FORMAT				m_color_format;//MWCAP_VIDEO_COLOR_FORMAT_UNKNOWN
	MWCAP_VIDEO_QUANTIZATION_RANGE			m_quant_range;//MWCAP_VIDEO_QUANTIZATION_UNKNOWN
	MWCAP_VIDEO_SATURATION_RANGE			m_sat_range;//MWCAP_VIDEO_SATURATION_UNKNOWN
	DisplayModeSharedPtr					m_display_mode;
	BYTE									m_bit_depth; // Bit Depth
	HANDLE  m_signal_thread;
	HANDLE	m_video_thread;
	HANDLE	m_render_thread;
	HANDLE  m_audio_render_thread;
	HANDLE  m_start_audio_output_thread;
	CString m_name;
	timingclocktime_t m_previousTimingClockFrameTime = TIMING_CLOCK_TIME_INVALID;

	bool m_canCapture = true;
	std::vector<CaptureInput> m_captureInputSet;

	timingclocktime_t m_frameOffsetTicks = 0;
	double m_hardwareLatencyMs = 0;

	// If false this will not send any more frames out.
	std::atomic_bool m_outputCaptureData = false;

	std::atomic_bool m_outputAudioCaptureData = false;


	// This is set if the card is capturing, can have only one input supports in here for now.
	// (This implies we support only one callback whereas the hardware supports this per input.)
	int m_captureInputId = 0;
	ICaptureDeviceCallback* m_callback = nullptr;

	// We keep the current state of the video here so that we can determine if we have enough
	// data to feed the downstream client. Blackmagic gives you this through many channels and
	// we need to wait for all to be delivered before sending out OnCaptureDeviceVideoStateChange()
	// and OnCaptureDeviceVideoFrame(). There a few helper functions for this as well here.
	// WARNING: R/W from the capture thread, do not read from other thread
	bool m_videoFrameSeen = false;
	timingclocktime_t m_ticksPerFrame = TIMING_CLOCK_TIME_INVALID;
	bool m_videoHasInputSource = false;
	bool m_videoInvertedVertical = false;
	LONGLONG m_videoEotf = 0;
	LONGLONG m_videoColorSpace = 0;
	bool m_videoHasHdrData = false;
	HDRData m_videoHdrData;
	uint64_t m_capturedVideoFrameCount = 0;
	uint64_t m_missedVideoFrameCount = 0;

	void ResetVideoState();

	// Try to create and send a VideoState callback, upon failure will internally call Error() and return false
	bool SendVideoStateCallback();
	void SendCardStateCallback();

	// Current state, update through UpdateState()
	// WARNING: R/W from the capture thread, do not read from other thread
	CaptureDeviceState m_state = CaptureDeviceState::CAPTUREDEVICESTATE_UNKNOWN;
	void UpdateState(CaptureDeviceState state);

	// An error occurred and capture will not proceed (until the input changes)
	void Error(const CString& error);

	// Internal helpers
	//void OnNotifyStatusChanged(BMDDeckLinkStatusID statusID);
	//void OnLinkStatusBusyChange();

	std::atomic<ULONG> m_refCount;
};
