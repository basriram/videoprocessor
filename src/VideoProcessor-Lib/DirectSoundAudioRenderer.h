#pragma once
#include <AnAudioSource.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
class DirectSoundAudioRenderer:
	public IUnknown
{
public:
	DirectSoundAudioRenderer();
	~DirectSoundAudioRenderer();
	void initialize(AnAudioSourceComPtr);
	HRESULT PlayExclusiveStream();
	HRESULT GetStreamFormat(IAudioClient* audioClient, WAVEFORMATEX** pfmx);
	HRESULT	STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID* ppv) override;
	ULONG STDMETHODCALLTYPE	AddRef() override;
	ULONG STDMETHODCALLTYPE	Release() override;

protected:
	AnAudioSourceComPtr pMySource;
	std::atomic<ULONG> m_refCount;
};
typedef CComPtr<DirectSoundAudioRenderer> DirectSoundAudioRendererComPtr;

