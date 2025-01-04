#pragma once
class AnAudioSource:
	public IUnknown
{
public:
	virtual void setFormat(WAVEFORMATEX* pwfx) = 0;
	virtual bool LoadData(UINT32 bufferFrameCount, BYTE* pData, DWORD* flags) = 0;
};

typedef CComPtr<AnAudioSource> AnAudioSourceComPtr;