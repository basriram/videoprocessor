#include "pch.h"
#include <vector>
#include "DirectSoundAudioRenderer.h"
#include <format>
#include <iostream>
#include <Windows.h>
#include <mmdeviceapi.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <vector>
#include <propvarutil.h>
#include <Audioclient.h>

#pragma comment(lib, "Propsys.lib")

//-----------------------------------------------------------
// Play an exclusive-mode stream on the default audio
// rendering device. The PlayExclusiveStream function uses
// event-driven buffering and MMCSS to play the stream at
// the minimum latency supported by the device.
//-----------------------------------------------------------

// REFERENCE_TIME time units per second and per millisecond
#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

#define EXIT_ON_ERROR(hres)  \
              if (FAILED(hres)) { goto Exit; }
#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

// A device entry that the user can select from a menu.
struct device {
    WCHAR friendlyName[128];
    IMMDevice* pDevice;
};

DirectSoundAudioRenderer::DirectSoundAudioRenderer() {
}

DirectSoundAudioRenderer::~DirectSoundAudioRenderer() {

}

void DirectSoundAudioRenderer::initialize(AnAudioSourceComPtr pMySource) {
    this->pMySource = pMySource;
}

HRESULT DirectSoundAudioRenderer::GetStreamFormat(IAudioClient* audioClient, WAVEFORMATEX** pfmx) {

    WAVEFORMATEXTENSIBLE *mypfmx = (WAVEFORMATEXTENSIBLE*)CoTaskMemAlloc(sizeof(WAVEFORMATEXTENSIBLE));

    mypfmx->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;

    mypfmx->Format.nChannels = 2;

    mypfmx->Format.nSamplesPerSec = 48000L;
    //mypfmx->Format.nSamplesPerSec = 44100L;
    mypfmx->Format.wBitsPerSample = 16;
    mypfmx->Format.nBlockAlign = (WORD)((mypfmx->Format.wBitsPerSample / 8) * mypfmx->Format.nChannels);

    //mypfmx->Format.nAvgBytesPerSec = 352800L;
    mypfmx->Format.nAvgBytesPerSec = (DWORD)(mypfmx->Format.nSamplesPerSec * mypfmx->Format.nBlockAlign);
    mypfmx->Samples.wValidBitsPerSample = mypfmx->Format.wBitsPerSample;
    //pfmx->Format.nBlockAlign = 8; // Same as the usual


    mypfmx->Format.cbSize = 22; // After this to GUID

    mypfmx->dwChannelMask = 0;
    /*
    SPEAKER_FRONT_LEFT |
        SPEAKER_FRONT_RIGHT |
        SPEAKER_BACK_LEFT |
        SPEAKER_BACK_RIGHT|
        SPEAKER_FRONT_CENTER |
        SPEAKER_SIDE_LEFT |
      SPEAKER_SIDE_RIGHT |
        SPEAKER_LOW_FREQUENCY;
    */
    // Quadraphonic = 0x00000033

    mypfmx->SubFormat = KSDATAFORMAT_SUBTYPE_PCM; // Specify PCM

    WAVEFORMATEX* pClosestMatch;

    // We try to get a shared mode format.
    pClosestMatch = NULL;

    if (S_OK == audioClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, (WAVEFORMATEX*)mypfmx, &pClosestMatch)) {
      //  cout << "Exact format match for shared mode." << endl;
       *pfmx =(WAVEFORMATEX*) mypfmx;
       // pSharedRenderFormat = &format;
        return 0;
    }
    else if (pClosestMatch) {
       // cout << "Close match for shared mode." << endl;
        *pfmx = pClosestMatch;
        //pSharedRenderFormat = pClosestMatch;
        return 0;
    }

    // We try to get an exclusive mode format.
    pClosestMatch = NULL;

    if (S_OK == audioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, (WAVEFORMATEX*)mypfmx, &pClosestMatch)) {
        //cout << "Exact format match for exclusive mode." << endl;
        *pfmx = (WAVEFORMATEX*)mypfmx;
       // pExclusiveRenderFormat = &format;
        return 0;
    }
    else if (pClosestMatch) {
       // cout << "Close format match for exclusive mode." << endl;
        //pExclusiveRenderFormat = pClosestMatch;
        *pfmx = pClosestMatch;
        return 0;
    }

    return 0;
}

HRESULT DirectSoundAudioRenderer::PlayExclusiveStream()
{
    HRESULT hr;
    REFERENCE_TIME hnsRequestedDuration = 0;
    IMMDeviceEnumerator* pEnumerator = NULL;
    IMMDevice* pDevice = NULL;
    IAudioClient* pAudioClient = NULL;
    IAudioRenderClient* pRenderClient = NULL;
    WAVEFORMATEX* pwfx = NULL;
    HANDLE hEvent = NULL;
    HANDLE hTask = NULL;
    UINT32 bufferFrameCount;
    BYTE* pData;
    DWORD flags = 0;
    DWORD taskIndex = 0;
    hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator, NULL,
        CLSCTX_ALL, IID_IMMDeviceEnumerator,
        (void**)&pEnumerator);
    EXIT_ON_ERROR(hr)


        // We only care about render devices.
        device mydev[10];

    // We enumerate them.
    IMMDeviceCollection* pRenderCollection;
    hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pRenderCollection);
    UINT cRenderDevices;
    hr = pRenderCollection->GetCount(&cRenderDevices);
    for (UINT i = 0; i < cRenderDevices; i++) {
        // For each device, we create a device entry.
        device dev;

        // We store the pointer to the device in dev.pDevice.
        hr = pRenderCollection->Item(i, &dev.pDevice);

        // We store the friendly name in dev.friendlyName;
        IPropertyStore* pProperties;
        hr = dev.pDevice->OpenPropertyStore(STGM_READ, &pProperties);
        PROPVARIANT friendlyName;
        hr = pProperties->GetValue(PKEY_Device_FriendlyName, &friendlyName);
        hr = PropVariantToString(friendlyName, dev.friendlyName, 128);
        mydev[i] = dev;
        pProperties->Release();
    }
    pRenderCollection->Release();

      ///  hr = pEnumerator->GetDefaultAudioEndpoint(
      //      eRender, eConsole, &pDevice);

    pDevice = mydev[0].pDevice;

        hr = pDevice->Activate(
            IID_IAudioClient, CLSCTX_ALL,
            NULL, (void**)&pAudioClient);
    EXIT_ON_ERROR(hr)

        // Call a helper function to negotiate with the audio
        // device for an exclusive-mode stream format.
      GetStreamFormat(pAudioClient, &pwfx);
  

        // Initialize the stream to play at the minimum latency.
        hr = pAudioClient->GetDevicePeriod(NULL, &hnsRequestedDuration);
    EXIT_ON_ERROR(hr)

        hr = pAudioClient->Initialize(
            AUDCLNT_SHAREMODE_EXCLUSIVE,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            hnsRequestedDuration,
            hnsRequestedDuration,
            (WAVEFORMATEX*)pwfx,
            NULL);

  //  if (hr != S_OK) {
    //    hr = pAudioClient->Initialize(
   //         AUDCLNT_SHAREMODE_SHARED,
   //         AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
  //          hnsRequestedDuration,
   //         hnsRequestedDuration,
   //         (WAVEFORMATEX*)pwfx,
    //        NULL);
   // }

    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
        // Align the buffer if needed, see IAudioClient::Initialize() documentation
        UINT32 nFrames = 0;
        hr = pAudioClient->GetBufferSize(&nFrames);
        EXIT_ON_ERROR(hr)
            hnsRequestedDuration = (REFERENCE_TIME)((double)REFTIMES_PER_SEC / pwfx->nSamplesPerSec * nFrames + 0.5);
        hr = pAudioClient->Initialize(
            AUDCLNT_SHAREMODE_EXCLUSIVE,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            hnsRequestedDuration,
            hnsRequestedDuration,
            (WAVEFORMATEX*)pwfx,
            NULL);
    }
    EXIT_ON_ERROR(hr)

        // Tell the audio source which format to use.
       pMySource->setFormat(pwfx);

        // Create an event handle and register it for
        // buffer-event notifications.
        hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (hEvent == NULL)
    {
        hr = E_FAIL;
        goto Exit;
    }

    hr = pAudioClient->SetEventHandle(hEvent);
    EXIT_ON_ERROR(hr);

    // Get the actual size of the two allocated buffers.
    hr = pAudioClient->GetBufferSize(&bufferFrameCount);
    EXIT_ON_ERROR(hr)

        hr = pAudioClient->GetService(
            IID_IAudioRenderClient,
            (void**)&pRenderClient);
    EXIT_ON_ERROR(hr)

        // To reduce latency, load the first buffer with data
        // from the audio source before starting the stream.
        hr = pRenderClient->GetBuffer(bufferFrameCount, &pData);
    EXIT_ON_ERROR(hr)

        if (pMySource->LoadData(bufferFrameCount, pData, &flags)) {

            hr = pRenderClient->ReleaseBuffer(bufferFrameCount, flags);
            EXIT_ON_ERROR(hr)
        }

        // Ask MMCSS to temporarily boost the thread priority
        // to reduce glitches while the low-latency stream plays.
  //      hTask = AvSetMmThreadCharacteristics(TEXT("Pro Audio"), &taskIndex);
  //  if (hTask == NULL)
  //  {
   //     hr = E_FAIL;
    //    EXIT_ON_ERROR(hr)
   // }
    hr = pAudioClient->Reset();
    EXIT_ON_ERROR(hr)

    hr = pAudioClient->Start();  // Start playing.
    EXIT_ON_ERROR(hr)

        // Each loop fills one of the two buffers.
        while (flags != AUDCLNT_BUFFERFLAGS_SILENT)
        {
            // Wait for next buffer event to be signaled.
            DWORD retval = WaitForSingleObject(hEvent, 2000);
            if (retval != WAIT_OBJECT_0)
            {
                // Event handle timed out after a 2-second wait.
                pAudioClient->Stop();
                hr = ERROR_TIMEOUT;
                goto Exit;
            }

            // Grab the next empty buffer from the audio device.
            hr = pRenderClient->GetBuffer(bufferFrameCount, &pData);
           EXIT_ON_ERROR(hr)

                // Load the buffer with data from the audio source.
                if (pMySource->LoadData(bufferFrameCount, pData, &flags)) {
                    hr = pRenderClient->ReleaseBuffer(bufferFrameCount, flags);
                    EXIT_ON_ERROR(hr)
                }
        }

    // Wait for the last buffer to play before stopping.
    Sleep((DWORD)(hnsRequestedDuration / REFTIMES_PER_MILLISEC));

    hr = pAudioClient->Stop();  // Stop playing.
    EXIT_ON_ERROR(hr)

        Exit:
    if (hEvent != NULL)
    {
        CloseHandle(hEvent);
    }
   // if (hTask != NULL)
   // {
    //    AvRevertMmThreadCharacteristics(hTask);
    //}
    CoTaskMemFree(pwfx);
    SAFE_RELEASE(pEnumerator)
        SAFE_RELEASE(pDevice)
        SAFE_RELEASE(pAudioClient)
        SAFE_RELEASE(pRenderClient)

        return hr;
}

HRESULT	DirectSoundAudioRenderer::QueryInterface(REFIID iid, LPVOID* ppv)
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


ULONG DirectSoundAudioRenderer::AddRef(void)
{
    return ++m_refCount;
}


ULONG DirectSoundAudioRenderer::Release(void)
{
    ULONG newRefValue = --m_refCount;
    if (newRefValue == 0)
        delete this;

    return newRefValue;
}

