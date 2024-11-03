#include "pch.h"
#include "MagewellVideoFrame.h"

MagewellVideoFrame::~MagewellVideoFrame()
{
}

MagewellVideoFrame::MagewellVideoFrame()
{
}

HRESULT	MagewellVideoFrame::QueryInterface(REFIID iid, LPVOID* ppv)
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


ULONG MagewellVideoFrame::AddRef(void)
{
	return ++m_refCount;
}


ULONG MagewellVideoFrame::Release(void)
{
	ULONG newRefValue = --m_refCount;
	if (newRefValue == 0)
		delete this;

	return newRefValue;
}
