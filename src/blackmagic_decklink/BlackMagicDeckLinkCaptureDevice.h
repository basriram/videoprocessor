/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <vector>
#include <mutex>

#include <DeckLinkAPI_h.h>

#include <VideoFrame.h>
#include <ACaptureDevice.h>


typedef CComPtr<IDeckLink> IDeckLinkComPtr;

// Known invalid values
#define BMD_PIXEL_FORMAT_INVALID (BMDPixelFormat)0
#define BMD_DISPLAY_MODE_INVALID (BMDDisplayMode)0
#define BMD_EOTF_INVALID -1
#define BMD_COLOR_SPACE_INVALID -1


/**
 * BlackMagic DeckLink SDK capable capture device
 */
class BlackMagicDeckLinkCaptureDevice:
	public ACaptureDevice,
	public IDeckLinkInputCallback,
	public IDeckLinkProfileCallback,
	public IDeckLinkNotificationCallback
{
public:

	BlackMagicDeckLinkCaptureDevice(const IDeckLinkComPtr& deckLinkDevice);
	virtual ~BlackMagicDeckLinkCaptureDevice();

	// ACaptureDevice
	virtual void SetCallbackHandler(ICaptureDeviceCallback*) override;
	virtual CString GetName() override;
	virtual bool CanCapture() override;
	virtual void StartCapture() override;
	virtual void StopCapture() override;
	virtual CaptureInputId CurrentCaptureInputId() override;
	virtual CaptureInputs SupportedCaptureInputs() override;
	virtual void SetCaptureInput(const CaptureInputId) override;

	// IDeckLinkInputCallback
	HRESULT VideoInputFormatChanged(BMDVideoInputFormatChangedEvents notificationEvents, IDeckLinkDisplayMode* newDisplayMode, BMDDetectedVideoInputFormatFlags detectedSignalFlags) override;
	HRESULT VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame, IDeckLinkAudioInputPacket* audioPacket) override;

	// IDeckLinkProfileCallback
	HRESULT	ProfileChanging(IDeckLinkProfile* profileToBeActivated, BOOL streamsWillBeForcedToStop) override;
	HRESULT	ProfileActivated(IDeckLinkProfile* activatedProfile) override;

	// IDeckLinkNotificationCallback
	HRESULT Notify(BMDNotifications topic, uint64_t param1, uint64_t param2) override;

	// IUnknown
	HRESULT	QueryInterface(REFIID iid, LPVOID* ppv) override;
	ULONG AddRef() override;
	ULONG Release() override;

private:
	IDeckLinkComPtr m_deckLink;
	CComQIPtr<IDeckLinkConfiguration> m_deckLinkConfiguration;
	CComQIPtr<IDeckLinkProfileAttributes> m_deckLinkAttributes;
	CComQIPtr<IDeckLinkProfileManager> m_deckLinkProfileManager;
	CComQIPtr<IDeckLinkNotification> m_deckLinkNotification;
	CComQIPtr<IDeckLinkStatus> m_deckLinkStatus;
	bool m_canCapture = true;
	std::vector<CaptureInput> m_captureInputSet;

	// Main lock to prevent undesired interactions between callbacks from capture and user input
	std::mutex m_mutex;

	// This is set if the card is capturing, can have only one input supports in here for now.
	// (This implies we support only one callback whereas the hardware supports this per input.)
	BMDVideoConnection m_captureInputId = bmdVideoConnectionUnspecified;
	CComQIPtr<IDeckLinkInput> m_deckLinkInput = nullptr;
	ICaptureDeviceCallback* m_callback = nullptr;

	// We keep the current state of the video here so that we can determine if we have enough
	// data to feed the downstream client. Blackmagic gives you this through many channels and
	// we need to wait for all to be delivered before sending out OnCaptureDeviceVideoStateChange()
	// and OnCaptureDeviceVideoFrame(). There a few helper functions for this as well here.
	// WARNING: R/W from the capture thread.
	bool m_videoFrameSeen = false;
	BMDPixelFormat m_pixelFormat = BMD_PIXEL_FORMAT_INVALID;
	BMDDisplayMode m_videoDisplayMode = BMD_DISPLAY_MODE_INVALID;
	bool m_videoHasInputSource = false;
	LONGLONG m_videoEotf = BMD_EOTF_INVALID;
	LONGLONG m_videoColorSpace = BMD_COLOR_SPACE_INVALID;
	bool m_videoHasHdrData = false;
	HDRData m_videoHdrData;

	void ResetVideoState();
	void SendVideoStateCallback();
	void SendCardStateCallback();

	// Current state, update through UpdateState()
	// WARNING: R/W from the capture thread.
	CaptureDeviceState m_state = CaptureDeviceState::CAPTUREDEVICESTATE_UNKNOWN;
	void UpdateState(CaptureDeviceState state);

	// Internal helpers
	void OnNotifyStatusChanged(BMDDeckLinkStatusID statusID);
	void OnLinkStatusBusyChange();

	std::atomic<ULONG> m_refCount;
};
