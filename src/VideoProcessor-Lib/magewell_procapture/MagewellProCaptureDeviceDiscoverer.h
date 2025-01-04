/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once


#include <atomic>
#include <map>
#include <atlbase.h>
#include "vector"
#include "MagewellProCaptureDevice.h"
using namespace std;

#include <ACaptureDeviceDiscoverer.h>


/**
 * Magewell Pro Capture  disoverer
 */
class MagewellProCaptureDeviceDiscoverer:
		public ACaptureDeviceDiscoverer
{
public:

	MagewellProCaptureDeviceDiscoverer(ICaptureDeviceDiscovererCallback& callback);
	virtual ~MagewellProCaptureDeviceDiscoverer();

	// ACaptureDeviceDiscoverer
	virtual void Start() override;
	virtual void Stop() override;

	// IUnknown
	HRESULT	STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID* ppv) override;
	ULONG STDMETHODCALLTYPE	AddRef() override;
	ULONG STDMETHODCALLTYPE	Release() override;

private:
	MagewellProCaptureDevice* m_p_capture;
	ACaptureDeviceComPtr captureDevice;
	DirectSoundAudioRendererComPtr audioRenderer;
	// Maps between the IDeckLink ptr and the capture device
	std::map<MagewellProCaptureDevice*, ACaptureDeviceComPtr> m_captureDeviceMap;

	std::atomic<ULONG> m_refCount;
};
