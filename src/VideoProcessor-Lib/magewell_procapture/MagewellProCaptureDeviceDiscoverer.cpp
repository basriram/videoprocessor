/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include <algorithm>

#include "MagewellProCaptureDeviceDiscoverer.h"
#include "MagewellProCaptureDevice.h"


MagewellProCaptureDeviceDiscoverer::MagewellProCaptureDeviceDiscoverer(ICaptureDeviceDiscovererCallback& callback):
	ACaptureDeviceDiscoverer(callback)
{
}


MagewellProCaptureDeviceDiscoverer::~MagewellProCaptureDeviceDiscoverer()
{
	for (auto& kv : m_captureDeviceMap)
	{
		m_callback.OnCaptureDeviceLost(kv.second);
		kv.second.Release();
	}

	m_captureDeviceMap.clear();
}


void MagewellProCaptureDeviceDiscoverer::Start()
{
//	if (m_deckLinkDiscovery)
	//	throw std::runtime_error("Discoverer already started");
	ACaptureDeviceComPtr captureDevice;
	m_p_capture = new MagewellProCaptureDevice();
	m_p_capture->refresh_devices();
	m_p_capture->AddRef();
	captureDevice.Attach(m_p_capture);

	m_captureDeviceMap[m_p_capture] = captureDevice;
	m_callback.OnCaptureDeviceFound(captureDevice);
}


void MagewellProCaptureDeviceDiscoverer::Stop()
{
//	if (!m_deckLinkDiscovery)
//		throw std::runtime_error("Discoverer not running");

	// Stop notifications and end discovery
//	m_deckLinkDiscovery->UninstallDeviceNotifications();
//	m_deckLinkDiscovery.Release();

	// Send CaptureDeviceLost for all known
	m_p_capture->StopCapture();

}



HRESULT	MagewellProCaptureDeviceDiscoverer::QueryInterface(REFIID iid, LPVOID* ppv)
{
	if (!ppv)
		return E_INVALIDARG;

	// Initialise the return result
	*ppv = nullptr;

	if (iid == IID_IUnknown)
	{
		*ppv = this;
		AddRef();
		return S_OK;
	}

	return E_NOINTERFACE;
}


ULONG MagewellProCaptureDeviceDiscoverer::AddRef(void)
{
	return ++m_refCount;
}


ULONG MagewellProCaptureDeviceDiscoverer::Release(void)
{
	ULONG newRefValue = --m_refCount;
	if (newRefValue == 0)
		delete this;

	return newRefValue;
}
