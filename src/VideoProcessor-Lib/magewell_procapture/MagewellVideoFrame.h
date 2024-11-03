#pragma once
class MagewellVideoFrame :
	public IUnknown
{
public:
	MagewellVideoFrame();
	~MagewellVideoFrame();
	HRESULT	QueryInterface(REFIID iid, LPVOID* ppv) override;
	ULONG AddRef() override;
	ULONG Release() override;

private:

	std::atomic<ULONG> m_refCount;

};

