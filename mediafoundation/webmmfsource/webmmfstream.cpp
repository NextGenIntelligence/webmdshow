#include "webmmfsource.hpp"
#include "webmmfstream.hpp"
#include <mfapi.h>
#include <mferror.h>
#include <cassert>
#include <comdef.h>
#include <vfwmsgs.h>

_COM_SMARTPTR_TYPEDEF(IMFMediaEventQueue, __uuidof(IMFMediaEventQueue));
_COM_SMARTPTR_TYPEDEF(IMFSample, __uuidof(IMFSample));


namespace WebmMfSourceLib
{

WebmMfStream::WebmMfStream(
    WebmMfSource* pSource,
    IMFStreamDescriptor* pDesc) :
    m_pSource(pSource),
    m_pDesc(pDesc),
    m_pBaseCluster(0),
    m_pCurr(0),
    m_pStop(0),
    m_bDiscontinuity(true)
{
    const ULONG n = m_pDesc->AddRef();
    n;

    HRESULT hr = MFCreateEventQueue(&m_pEvents);
    assert(SUCCEEDED(hr));
    assert(m_pEvents);
}


WebmMfStream::~WebmMfStream()
{
    const ULONG n = m_pDesc->Release();
    n;
}

HRESULT WebmMfStream::QueryInterface(const IID& iid, void** ppv)
{
    if (ppv == 0)
        return E_POINTER;

    IUnknown*& pUnk = reinterpret_cast<IUnknown*&>(*ppv);

    if (iid == __uuidof(IUnknown))
    {
        pUnk = this;  //must be nondelegating
    }
    else if (iid == __uuidof(IMFMediaEventGenerator))
    {
        pUnk = static_cast<IMFMediaEventGenerator*>(this);
    }
    else if (iid == __uuidof(IMFMediaStream))
    {
        pUnk = static_cast<IMFMediaStream*>(this);
    }
    else
    {
        pUnk = 0;
        return E_NOINTERFACE;
    }

    pUnk->AddRef();
    return S_OK;
}


ULONG WebmMfStream::AddRef()
{
    return m_pSource->AddRef();
}


ULONG WebmMfStream::Release()
{
    return m_pSource->Release();
}


HRESULT WebmMfStream::GetEvent(
    DWORD dwFlags,
    IMFMediaEvent** ppEvent)
{
    WebmMfSource::Lock lock;

    HRESULT hr = lock.Seize(m_pSource);

    if (FAILED(hr))
        return hr;

    if (m_pSource->m_bShutdown)
        return MF_E_SHUTDOWN;

    const IMFMediaEventQueuePtr pEvents(m_pEvents);
    assert(pEvents);

    hr = lock.Release();
    assert(SUCCEEDED(hr));

    return pEvents->GetEvent(dwFlags, ppEvent);
}


HRESULT WebmMfStream::BeginGetEvent(
    IMFAsyncCallback* pCallback,
    IUnknown* pState)
{
    WebmMfSource::Lock lock;

    HRESULT hr = lock.Seize(m_pSource);

    if (FAILED(hr))
        return hr;

    if (m_pSource->m_bShutdown)
        return MF_E_SHUTDOWN;

    assert(m_pEvents);

    return m_pEvents->BeginGetEvent(pCallback, pState);
}


HRESULT WebmMfStream::EndGetEvent(
    IMFAsyncResult* pResult,
    IMFMediaEvent** ppEvent)
{
    WebmMfSource::Lock lock;

    HRESULT hr = lock.Seize(m_pSource);

    if (FAILED(hr))
        return hr;

    if (m_pSource->m_bShutdown)
        return MF_E_SHUTDOWN;

    assert(m_pEvents);

    return m_pEvents->EndGetEvent(pResult, ppEvent);
}


HRESULT WebmMfStream::QueueEvent(
    MediaEventType t,
    REFGUID g,
    HRESULT hrStatus,
    const PROPVARIANT* pValue)
{
    WebmMfSource::Lock lock;

    HRESULT hr = lock.Seize(m_pSource);

    if (FAILED(hr))
        return hr;

    if (m_pSource->m_bShutdown)
        return MF_E_SHUTDOWN;

    assert(m_pEvents);

    return m_pEvents->QueueEventParamVar(t, g, hrStatus, pValue);
}


HRESULT WebmMfStream::GetMediaSource(IMFMediaSource** pp)
{
#if 0
    if (pp == 0)
        return E_POINTER;

    IMFMediaSource*& p = *pp;

    p = m_pSource;
    p->AddRef();

    return S_OK;
#else
    //TODO: check for shutdown?

    return m_pSource->IUnknown::QueryInterface(pp);
#endif
}


HRESULT WebmMfStream::GetStreamDescriptor(IMFStreamDescriptor** pp)
{
    if (pp == 0)
        return E_POINTER;

    //TODO: check for shutdown?

    IMFStreamDescriptor*& p = *pp;

    p = m_pDesc;
    p->AddRef();

    return S_OK;
}


HRESULT WebmMfStream::RequestSample(IUnknown* pToken)
{
    WebmMfSource::Lock lock;

    HRESULT hr = lock.Seize(m_pSource);
    assert(SUCCEEDED(hr));  //TODO

    if (m_pSource->m_bShutdown)
        return MF_E_SHUTDOWN;

    //TODO: check for EOS, and return MF_E_END_OF_STREAM
    //TODO: if source is STATE_STOPPED, then return MF_E_INVALIDREQUEST

    IMFSamplePtr pSample;

    hr = MFCreateSample(&pSample);
    assert(SUCCEEDED(hr));
    assert(pSample);

    for (;;)
    {
        hr = PopulateSample(pSample);

        if (hr != VFW_E_BUFFER_UNDERFLOW)
            break;

        hr = Preload();  //TODO: file-based read assumed
        assert(SUCCEEDED(hr));
    }

    if (hr == S_OK)  //have a sample
    {
        //WavSource sample says:
        // NOTE: If we processed sample requests asynchronously, we would
        // need to call AddRef on the token and put the token onto a FIFO
        // queue. See documenation for IMFMediaStream::RequestSample.

        if (pToken)
        {
            hr = pSample->SetUnknown(MFSampleExtension_Token, pToken);
            assert(SUCCEEDED(hr));
        }

        //if source.state = paused then
        //  queue sample in sample queue
        //else
        //  deliver sample

        //TODO: this is the non-paused case:
        //hr = QueueEventWithIUnknown(this, MEMediaSample, S_OK, pSample);
        //assert(SUCCEEDED(hr));  //TODO

        PROPVARIANT prop;

        prop.vt = VT_UNKNOWN;
        prop.punkVal = pSample;
        //TODO: addref here?

        //hr = QueueEvent(MEMediaSample, GUID_NULL, S_OK, &prop);
        //assert(SUCCEEDED(hr));

        hr = m_pEvents->QueueEventParamVar(
                MEMediaSample,
                GUID_NULL,
                S_OK,
                &prop);

        assert(SUCCEEDED(hr));

        //TODO: clear prop var?

        //if eos
        //  m_bEOS = true;
        //  hr = QueueEvent(MEEndOfStream, GUID_NULL, S_OK, 0);

        //TODO: handle EOS here?

        return S_OK;
    }

    assert(hr == S_FALSE);  //EOS

    //queue event MEEndOfPresentation?
    //return MF_E_END_OF_STREAM?

    return S_OK;  //TODO: handle EOS
}


HRESULT WebmMfStream::Preload()
{
    mkvparser::Track* const pTrack = GetTrack();

    mkvparser::Segment* const pSegment = pTrack->m_pSegment;

    mkvparser::Cluster* pCluster;
    LONGLONG pos;

    //TODO: file-based load is assumed here
    //We need to determine how MF handles network streams.

    //TODO: is it possible to be smarter here, to keep
    //parsing until we have a cluster containing a block from
    //this track?

    const long result = pSegment->ParseCluster(pCluster, pos);
    result;
    assert(result >= 0);

    const bool bDone = pSegment->AddCluster(pCluster, pos);

    return bDone ? S_FALSE : S_OK;
}


HRESULT WebmMfStream::PopulateSample(IMFSample* pSample)
{
    mkvparser::Track* const pTrack = GetTrack();

    if (m_pCurr == 0)
    {
        const long result = pTrack->GetFirst(m_pCurr);

        if (result == mkvparser::E_BUFFER_NOT_FULL)
            return result;  //try again later

        assert(result >= 0);
        assert(m_pCurr);

        //TODO:
        //This doesn't seem correct: the base cluster must be the
        //same for all streams (since that is how time is calculated),
        //but we can't really guarantee that here (unless we also
        //have a guarantee that all streams are initialized to
        //pCurr=NULL, in which case all streams would be all have
        //the same base).
        //
        //The other problem is that there's no guarantee that
        //m_pCurr is on the first cluster in the segment.  Technically
        //it doesn't matter, but if pCurr is far away then there will
        //be a large gap in time before this frame renders.
        //
        //I'd feel better if the initialization step were explicit,
        //and for all streams simultaneously; say, during the
        //transition to paused/running.  This would have the effect
        //of setting pCurr for all streams to some non-NULL value,
        //for which we could then test.
        //END TODO.

        m_pBaseCluster = pTrack->m_pSegment->GetFirst();
        assert(m_pBaseCluster);
    }

    if (m_pStop == 0)  //intepreted to mean "play until end"
    {
        if (m_pCurr->EOS())
            return S_FALSE;  //send EOS downstream
    }
    else if (m_pCurr == m_pStop)
    {
        return S_FALSE;  //EOS
    }

    const mkvparser::BlockEntry* pNextBlock;

    const long result = pTrack->GetNext(m_pCurr, pNextBlock);

    if (result == mkvparser::E_BUFFER_NOT_FULL)
        return VFW_E_BUFFER_UNDERFLOW;

    assert(result >= 0);
    assert(pNextBlock);

    HRESULT hr = OnPopulateSample(pNextBlock, pSample);
    assert(SUCCEEDED(hr));  //TODO
    assert(hr == S_OK);     //TODO: for now, assume we never throw away

    m_pCurr = pNextBlock;

#if 0  //TODO: resolve this
    if (hr != S_OK)
        return 2;  //throw away this sample
#endif

    if (m_bDiscontinuity)
    {
        hr = pSample->SetUINT32(MFSampleExtension_Discontinuity, TRUE);
        assert(SUCCEEDED(hr));

        m_bDiscontinuity = false;
    }

    return S_OK;  //TODO
}


}  //end namespace WebmMfSource