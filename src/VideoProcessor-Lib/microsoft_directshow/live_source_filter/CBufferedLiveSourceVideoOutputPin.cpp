/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>

#include "CBufferedLiveSourceVideoOutputPin.h"


CBufferedLiveSourceVideoOutputPin::CBufferedLiveSourceVideoOutputPin(
	CLiveSource* filter,
	CCritSec* pLock,
	HRESULT* phr):
	ALiveSourceVideoOutputPin(filter, pLock, phr)
{
	// Phase 1: Create auto-reset event for frame queue notification.
	// Initial state is non-signaled; signaled by OnVideoFrame when frames arrive.
	m_hFrameEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!m_hFrameEvent) {
		DbgLog((LOG_ERROR, 1, TEXT("CBufferedLiveSourceVideoOutputPin: Failed to create frame event, falling back to Sleep(1)")));
	}
}


CBufferedLiveSourceVideoOutputPin::~CBufferedLiveSourceVideoOutputPin()
{
	PurgeQueue();
	if (m_hFrameEvent) {
		CloseHandle(m_hFrameEvent);
		m_hFrameEvent = NULL;
	}
}


HRESULT CBufferedLiveSourceVideoOutputPin::Active()
{
	if (m_frameQueueMaxSize == 0)
		throw std::runtime_error("Call SetFrameQueueMaxSize() before activating the graph");

	{
		CAutoLock lock(m_pLock);

		if (m_pFilter->IsActive())
			return S_FALSE;	// succeeded, but did not allocate resources (they already exist...)

		assert(IsConnected());
		assert(!m_isActive);

		HRESULT hr = ALiveSourceVideoOutputPin::Active();
		if (FAILED(hr))
			return hr;

		assert(!ThreadExists());

		{
			CAutoLock lock2(&m_filterCritSec);

			m_isActive = true;
		}

		// start the thread
		if (!Create())
			return E_FAIL;

		return S_OK;
	}
}


HRESULT CBufferedLiveSourceVideoOutputPin::Inactive()
{
	{
		CAutoLock lock(m_pLock);

		// do nothing if not connected - its ok not to connect to all pins of a source filter
		if (!IsConnected())
			return NOERROR;

		HRESULT hr = ALiveSourceVideoOutputPin::Inactive();
		if (FAILED(hr))
			return hr;

		{
			CAutoLock lock2(&m_filterCritSec);

			m_isActive = false;

			PurgeQueue();
		}

		// Phase 1: Signal the frame event to wake the worker thread so it can see
		// that m_isActive == false and exit promptly. Without this, the thread could
		// block on WaitForSingleObject for up to 16ms before checking the flag.
		if (m_hFrameEvent) {
			SetEvent(m_hFrameEvent);
		}

		if (ThreadExists())
		{
			Close();
		}
	}

	return S_OK;
}


HRESULT CBufferedLiveSourceVideoOutputPin::OnVideoFrame(VideoFrame& videoFrame)
{
	{
		CAutoLock lock(&m_filterCritSec);

		// Reject frames if not processing
		if (!m_isActive)
			return S_OK;

		// If this frame's timestamp is lower or equal to the one before it,
		// erase that earlier one
		while (!m_videoFrameQueue.empty())
		{
			VideoFrame lastFrame = m_videoFrameQueue.back();

			// Previous one was younger, nothing to do
			if (videoFrame.GetTimingTimestamp() > lastFrame.GetTimingTimestamp())
				break;

			// Previous one was older or equal, erase
			lastFrame.SourceBufferRelease();
			m_videoFrameQueue.pop_back();
			++m_droppedFrameCount;
		}

		// If full throw away oldest to make space
		if (m_videoFrameQueue.size() >= m_frameQueueMaxSize)
		{
			m_videoFrameQueue.front().SourceBufferRelease();
			m_videoFrameQueue.pop_front();
			++m_droppedFrameCount;
		}

		// Prevent from getting cleaned up and add to queue
		videoFrame.SourceBufferAddRef();
		m_videoFrameQueue.push_back(videoFrame);
	}

	// Phase 1: Signal the worker thread that a new frame is available.
	// This replaces the old Sleep(1) polling with an event-based wakeup.
	// The event is auto-reset, so the signal is consumed by one WaitForSingleObject call.
	if (m_hFrameEvent) {
		SetEvent(m_hFrameEvent);
	}

	return S_OK;
}


void CBufferedLiveSourceVideoOutputPin::SetFrameQueueMaxSize(size_t frameQueueMaxSize)
{
	if (frameQueueMaxSize <= 0)
		throw std::runtime_error("Frame queue size must be > 0");

	{
		CAutoLock lock(&m_filterCritSec);

		m_frameQueueMaxSize = frameQueueMaxSize;

		// If full throw away oldest to make space if needed
		while (m_videoFrameQueue.size() >= m_frameQueueMaxSize)
		{
			m_videoFrameQueue.front().SourceBufferRelease();
			m_videoFrameQueue.pop_front();
			++m_droppedFrameCount;
		}
	}
}


size_t CBufferedLiveSourceVideoOutputPin::GetFrameQueueSize()
{
	{
		CAutoLock lock(&m_filterCritSec);

		return m_videoFrameQueue.size();
	}
}


void CBufferedLiveSourceVideoOutputPin::Reset()
{
	PurgeQueue();
	ALiveSourceVideoOutputPin::Reset();
}


DWORD CBufferedLiveSourceVideoOutputPin::ThreadProc()
{
	// ! WARNING: Runs in inner thread

	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin worker thread starting")));

	while (true)
	{
		// Phase 1: Wait for frame event instead of Sleep(1) polling.
		// Sleep(1) has ~15ms granularity on typical Windows desktop systems, causing
		// up to 15ms of unnecessary latency per frame. WaitForSingleObject on the
		// auto-reset frame event wakes within microseconds when OnVideoFrame signals it.
		// 16ms timeout = ~1 frame at 60fps, ensuring the thread never blocks indefinitely.
		WaitForSingleObject(m_hFrameEvent, 16);

		VideoFrame videoFrame;

		{
			CAutoLock lock(&m_filterCritSec);

			// Stop thread
			if (!m_isActive)
				break;

			// For most timing empty is really empty, however for the clock-to-clock
			// we need to keep one frame in.
			if (m_timestamp == DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK)
			{
				if (m_videoFrameQueue.size() <= 1)
					continue;
			}
			else
			{
				if (m_videoFrameQueue.empty())
					continue;
			}

			// Get the front frame (oldest)
			videoFrame = m_videoFrameQueue.front();
			m_videoFrameQueue.pop_front();

			// Get the current front's start time
			switch (m_timestamp)
			{
			case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_CLOCK:
				assert(!m_videoFrameQueue.empty());
				// break;  not here intentionally

			case DirectShowStartStopTimeMethod::DS_SSTM_CLOCK_SMART:

				if (!m_videoFrameQueue.empty())
				{
					m_nextVideoFrameStartTime =
						(REFERENCE_TIME)(
						m_videoFrameQueue.front().GetTimingTimestamp() *
						(10000000.0 / m_timingClock->TimingClockTicksPerSecond()));
				}
				else
				{
					m_nextVideoFrameStartTime = REFERENCE_TIME_INVALID;
				}
				break;
			}
		}

		// Get buffer for sample
		// Note you can fill in start and stop time, but following the code shows that they are unused.
		IMediaSample* pSample = nullptr;
		HRESULT hr = this->GetDeliveryBuffer(&pSample, nullptr, nullptr, 0);
		if (FAILED(hr))
		{
			videoFrame.SourceBufferRelease();
			return -1;
		}

		// Convert
		hr = RenderVideoFrameIntoSample(videoFrame, pSample);
		if (FAILED(hr))
		{
			videoFrame.SourceBufferRelease();
			pSample->Release();
			return -2;
		}
		if (hr == S_FRAME_NOT_RENDERED)
		{
			videoFrame.SourceBufferRelease();
			pSample->Release();
			continue;
		}

		// Deliver frame to renderer
		hr = this->Deliver(pSample);
		if (FAILED(hr))
		{
			DbgLog((LOG_TRACE, 1,
				TEXT("::FillBuffer(#%I64u): Failed to deliver sample, error: %i"),
				videoFrame.GetCounter(), hr));

			videoFrame.SourceBufferRelease();
			pSample->Release();
			return -3;
		}

		videoFrame.SourceBufferRelease();
		pSample->Release();
	}

	DbgLog((LOG_TRACE, 1, TEXT("CBufferedLiveSourceVideoOutputPin worker thread exiting")));

	return 0;
}


void CBufferedLiveSourceVideoOutputPin::PurgeQueue()
{
	{
		CAutoLock lock(&m_filterCritSec);

		while (!m_videoFrameQueue.empty())
		{
			VideoFrame popFrame = m_videoFrameQueue.front();
			popFrame.SourceBufferRelease();
			m_videoFrameQueue.pop_front();
			++m_droppedFrameCount;
		}
	}
}
