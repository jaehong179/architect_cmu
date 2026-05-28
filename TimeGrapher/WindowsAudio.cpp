#include <QtGlobal>
#if defined(Q_OS_WIN)
#include "WindowsAudio.h"

#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#define INITGUID

#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <devicetopology.h>
#include <devpkey.h>
#include <functiondiscoverykeys_devpkey.h>
#include <functiondiscoverykeys.h>
#include <propsys.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

struct AudioDeviceInfo
{
    // Usually the most useful user-facing endpoint name.
    // Example: "Microphone Array (Realtek(R) Audio)"
    std::wstring endpointFriendlyName;

    // Device description.
    // Example: "Microphone Array"
    std::wstring deviceName;

    // Adapter/interface friendly name.
    // Example: "Realtek(R) Audio" or "USB Audio Device"
    std::wstring adapterFriendlyName;

    // The unique Windows endpoint ID.
    // Use this if multiple devices have identical names.
    std::wstring endpointId;

    DWORD state = 0;
    EDataFlow dataFlow = eAll;
};

struct AgcStats
{
    int foundCount = 0;
    int changedCount = 0;
};

struct MatchCandidate
{
    ComPtr<IMMDevice> device;
    AudioDeviceInfo info;
    int score = 0;
};


static std::wstring ToLower(std::wstring s);

static bool EqualsIgnoreCase(const std::wstring& a,
                             const std::wstring& b);

static bool ContainsIgnoreCase(const std::wstring& haystack,
                               const std::wstring& needle);

static int MatchScore(const std::wstring& value,
                      const std::wstring& filter,
                      int exactScore,
                      int containsScore);

static std::wstring DataFlowToString(EDataFlow flow);

static std::wstring DeviceStateToString(DWORD state);

static std::wstring ReadStringProperty(IPropertyStore* props,
                                       const PROPERTYKEY& key);

static std::wstring GetDeviceId(IMMDevice* device);

static AudioDeviceInfo GetAudioDeviceInfo(IMMDevice* device,
                                          EDataFlow flow);

static void PrintAudioDeviceInfo(const AudioDeviceInfo& info);

static HRESULT CreateDeviceEnumerator(ComPtr<IMMDeviceEnumerator>& enumerator);

static bool SingleNameMatchesDevice(const AudioDeviceInfo& info,
                                    const std::wstring& name);

static int SingleNameMatchScore(const AudioDeviceInfo& info,
                                const std::wstring& name);

static bool TwoNameMatchesDevice(const AudioDeviceInfo& info,
                                 const std::wstring& micNameFilter,
                                 const std::wstring& deviceNameFilter);

static int TwoNameMatchScore(const AudioDeviceInfo& info,
                             const std::wstring& micNameFilter,
                             const std::wstring& deviceNameFilter);

static HRESULT FindEndpointById(EDataFlow flow,
                                const std::wstring& endpointId,
                                ComPtr<IMMDevice>& outDevice,
                                AudioDeviceInfo& outInfo);

static HRESULT FindEndpointByOneName(EDataFlow flow,
                                     const std::wstring& name,
                                     ComPtr<IMMDevice>& outDevice,
                                     AudioDeviceInfo& outInfo);

static HRESULT FindEndpointByTwoNames(EDataFlow flow,
                                      const std::wstring& micNameFilter,
                                      const std::wstring& deviceNameFilter,
                                      ComPtr<IMMDevice>& outDevice,
                                      AudioDeviceInfo& outInfo);

static HRESULT GetEndpointVolumePercent(IMMDevice* device,
                                        float& outPercent);

static HRESULT SetEndpointVolumePercent(IMMDevice* device,
                                        float percent);

static std::wstring GetPartName(IPart* part);

static std::wstring GetPartGlobalId(IPart* part);

static UINT GetPartLocalId(IPart* part);

static std::wstring PartTypeToString(PartType type);

static std::wstring GetPartTypeText(IPart* part);

static std::wstring MakePartVisitedKey(IPart* part);

static void VisitPartForAgc(IPart* part,
                            bool changeAgc,
                            bool enableAgc,
                            std::set<std::wstring>& visited,
                            AgcStats& stats,
                            int depth);

static void VisitPartListForAgc(IPart* part,
                                bool outgoing,
                                bool changeAgc,
                                bool enableAgc,
                                std::set<std::wstring>& visited,
                                AgcStats& stats,
                                int depth);

static HRESULT PrintOrSetAgcForDevice(IMMDevice* device,
                                      bool changeAgc,
                                      bool enableAgc,
                                      AgcStats& stats);

static HRESULT ListEndpoints(EDataFlow flow);

static HRESULT RunAgcForMatchedDevice(IMMDevice* micDevice,
                                      const AudioDeviceInfo& micInfo,
                                      bool changeAgc,
                                      bool enableAgc);

static HRESULT RunSetMicForMatchedDevice(IMMDevice* micDevice,
                                         const AudioDeviceInfo& micInfo,
                                         float volumePercent);

static std::wstring asciiToWstring(const char* s);


void WindowsSetSoundParameters(const char *endpoint_name,const char *mic_name,int volume_percent)
{
    ComPtr<IMMDevice> micDevice;
    AudioDeviceInfo micInfo;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool didInitCom = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
    {
        std::wcerr << L"CoInitializeEx failed. HRESULT=0x"
                   << std::hex << hr << std::dec << L"\n";
        return;
    }
    const std::wstring micName = asciiToWstring(mic_name);
    const std::wstring deviceName = asciiToWstring(endpoint_name);
    hr = FindEndpointByTwoNames(
        eCapture,
        micName,
        deviceName,
        micDevice,
        micInfo);

    if (FAILED(hr))
     {
       std::wcerr << L"Microphone not found or ambiguous. HRESULT=0x"
               << std::hex << hr << std::dec << L"\n";
        if (didInitCom)
            CoUninitialize();
        return;
     }
    hr = RunAgcForMatchedDevice(
        micDevice.Get(),
        micInfo,
        false,
        false);

    if (FAILED(hr))
    {
        std::wcerr << L"Disable ACG Failed. HRESULT=0x"
                   << std::hex << hr << std::dec << L"\n";
        if (didInitCom)
            CoUninitialize();
        return;
    }
    hr = RunSetMicForMatchedDevice(
         micDevice.Get(),
         micInfo,
         volume_percent);
    if (FAILED(hr))
    {
        std::wcerr << L"Set Volume Failed. HRESULT=0x"
                   << std::hex << hr << std::dec << L"\n";
        if (didInitCom)
            CoUninitialize();
        return;
    }
    if (didInitCom)
        CoUninitialize();
}
void WindowsListSoundCardsAndElements(void)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool didInitCom = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
    {
        std::wcerr << L"CoInitializeEx failed. HRESULT=0x"
                   << std::hex << hr << std::dec << L"\n";
        return;
    }

#if 0
    hr = ListEndpoints(eRender);
    if (FAILED(hr))
    {
        std::wcerr << L"Failed to list playback devices. HRESULT=0x"
                   << std::hex << hr << std::dec << L"\n";
    }
#endif
    hr = ListEndpoints(eCapture);
    if (FAILED(hr))
    {
        std::wcerr << L"Failed to list capture devices. HRESULT=0x"
                   << std::hex << hr << std::dec << L"\n";
    }
    if (didInitCom)
        CoUninitialize();

}

static std::wstring asciiToWstring(const char* s)
{
    if (!s)
        return L"";

    std::wstring out;
    while (*s)
    {
        out.push_back(static_cast<wchar_t>(*s));
        ++s;
    }
    return out;
}

static std::wstring ToLower(std::wstring s)
{
    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        [](wchar_t c)
        {
            return static_cast<wchar_t>(std::towlower(c));
        });

    return s;
}

static bool EqualsIgnoreCase(const std::wstring& a,
                             const std::wstring& b)
{
    return ToLower(a) == ToLower(b);
}

static bool ContainsIgnoreCase(const std::wstring& haystack,
                               const std::wstring& needle)
{
    if (needle.empty())
        return false;

    return ToLower(haystack).find(ToLower(needle)) != std::wstring::npos;
}

static int MatchScore(const std::wstring& value,
                      const std::wstring& filter,
                      int exactScore,
                      int containsScore)
{
    if (filter.empty() || value.empty())
        return 0;

    // All user-supplied name filters are substring filters.
    // Exact matches are NOT required. Exact matches only receive a higher
    // score so that, when multiple devices contain the same substring, the
    // most specific match is preferred.
    if (!ContainsIgnoreCase(value, filter))
        return 0;

    if (EqualsIgnoreCase(value, filter))
        return exactScore;

    return containsScore;
}

static std::wstring DataFlowToString(EDataFlow flow)
{
    switch (flow)
    {
    case eRender:  return L"Playback";
    case eCapture: return L"Capture/Microphone";
    case eAll:     return L"All";
    default:       return L"Unknown";
    }
}

static std::wstring DeviceStateToString(DWORD state)
{
    std::wstring s;

    if (state & DEVICE_STATE_ACTIVE)
        s += L"ACTIVE ";

    if (state & DEVICE_STATE_DISABLED)
        s += L"DISABLED ";

    if (state & DEVICE_STATE_NOTPRESENT)
        s += L"NOTPRESENT ";

    if (state & DEVICE_STATE_UNPLUGGED)
        s += L"UNPLUGGED ";

    if (s.empty())
        s = L"UNKNOWN";

    return s;
}

static std::wstring ReadStringProperty(IPropertyStore* props,
                                       const PROPERTYKEY& key)
{
    if (!props)
        return L"";

    PROPVARIANT value;
    PropVariantInit(&value);

    std::wstring result;

    HRESULT hr = props->GetValue(key, &value);
    if (SUCCEEDED(hr) && value.vt == VT_LPWSTR && value.pwszVal)
        result = value.pwszVal;

    PropVariantClear(&value);
    return result;
}

static std::wstring GetDeviceId(IMMDevice* device)
{
    if (!device)
        return L"";

    LPWSTR id = nullptr;
    HRESULT hr = device->GetId(&id);

    std::wstring result;

    if (SUCCEEDED(hr) && id)
    {
        result = id;
        CoTaskMemFree(id);
    }

    return result;
}

static AudioDeviceInfo GetAudioDeviceInfo(IMMDevice* device,
                                          EDataFlow flow)
{
    AudioDeviceInfo info;
    info.dataFlow = flow;

    if (!device)
        return info;

    device->GetState(&info.state);
    info.endpointId = GetDeviceId(device);

    ComPtr<IPropertyStore> props;
    HRESULT hr = device->OpenPropertyStore(STGM_READ, &props);

    if (SUCCEEDED(hr) && props)
    {
        info.endpointFriendlyName =
            ReadStringProperty(props.Get(), PKEY_Device_FriendlyName);

        info.deviceName =
            ReadStringProperty(props.Get(), PKEY_Device_DeviceDesc);

        info.adapterFriendlyName =
            ReadStringProperty(props.Get(), PKEY_DeviceInterface_FriendlyName);
    }

    return info;
}

static void PrintAudioDeviceInfo(const AudioDeviceInfo& info)
{
    std::wcout << L"  Endpoint friendly name : "
               << info.endpointFriendlyName << L"\n";

    std::wcout << L"  Device name            : "
               << info.deviceName << L"\n";

    std::wcout << L"  Adapter/interface name : "
               << info.adapterFriendlyName << L"\n";

    std::wcout << L"  Flow                   : "
               << DataFlowToString(info.dataFlow) << L"\n";

    std::wcout << L"  State                  : "
               << DeviceStateToString(info.state) << L"\n";

    std::wcout << L"  Endpoint ID            : "
               << info.endpointId << L"\n";
}

static HRESULT CreateDeviceEnumerator(ComPtr<IMMDeviceEnumerator>& enumerator)
{
    return CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(enumerator.GetAddressOf()));
}

static void PrintMatchCandidates(const std::vector<MatchCandidate>& matches)
{
    for (size_t i = 0; i < matches.size(); ++i)
    {
        std::wcerr << L"Candidate " << i << L", score "
                   << matches[i].score << L":\n";

        PrintAudioDeviceInfo(matches[i].info);
        std::wcerr << L"\n";
    }
}

static bool SingleNameMatchesDevice(const AudioDeviceInfo& info,
                                    const std::wstring& name)
{
    return ContainsIgnoreCase(info.endpointFriendlyName, name) ||
           ContainsIgnoreCase(info.deviceName, name) ||
           ContainsIgnoreCase(info.adapterFriendlyName, name) ||
           ContainsIgnoreCase(info.endpointId, name);
}

static int SingleNameMatchScore(const AudioDeviceInfo& info,
                                const std::wstring& name)
{
    int score = 0;

    score = std::max(score, MatchScore(info.endpointFriendlyName, name, 400, 40));
    score = std::max(score, MatchScore(info.deviceName,           name, 300, 30));
    score = std::max(score, MatchScore(info.adapterFriendlyName,  name, 200, 20));
    score = std::max(score, MatchScore(info.endpointId,           name, 1000, 10));

    return score;
}

// Safer two-filter substring matching.
//
// micNameFilter is intended to match the actual microphone endpoint, such as:
//   "Microphone Array"
//   "USB"
//   "Array"
//
// deviceNameFilter is intended to disambiguate the device/adapter side, such as:
//   "Realtek"
//   "USB Audio"
//   "Logitech"
//   or even a substring of the endpoint ID.
//
// BOTH substring filters must match the same endpoint. Exact equality is not
// required for either filter.
static bool TwoNameMatchesDevice(const AudioDeviceInfo& info,
                                 const std::wstring& micNameFilter,
                                 const std::wstring& deviceNameFilter)
{
    const bool micFilterMatches =
        ContainsIgnoreCase(info.endpointFriendlyName, micNameFilter) ||
        ContainsIgnoreCase(info.deviceName, micNameFilter);

    const bool deviceFilterMatches =
        ContainsIgnoreCase(info.deviceName, deviceNameFilter) ||
        ContainsIgnoreCase(info.adapterFriendlyName, deviceNameFilter) ||
        ContainsIgnoreCase(info.endpointFriendlyName, deviceNameFilter) ||
        ContainsIgnoreCase(info.endpointId, deviceNameFilter);

    return micFilterMatches && deviceFilterMatches;
}

static int TwoNameMatchScore(const AudioDeviceInfo& info,
                             const std::wstring& micNameFilter,
                             const std::wstring& deviceNameFilter)
{
    int micScore = 0;
    micScore = std::max(micScore, MatchScore(info.endpointFriendlyName, micNameFilter, 500, 50));
    micScore = std::max(micScore, MatchScore(info.deviceName,           micNameFilter, 400, 40));

    int deviceScore = 0;
    deviceScore = std::max(deviceScore, MatchScore(info.deviceName,           deviceNameFilter, 300, 30));
    deviceScore = std::max(deviceScore, MatchScore(info.adapterFriendlyName,  deviceNameFilter, 300, 30));
    deviceScore = std::max(deviceScore, MatchScore(info.endpointFriendlyName, deviceNameFilter, 100, 10));
    deviceScore = std::max(deviceScore, MatchScore(info.endpointId,           deviceNameFilter, 1000, 10));

    return micScore + deviceScore;
}

static HRESULT FindEndpointById(EDataFlow flow,
                                const std::wstring& endpointId,
                                ComPtr<IMMDevice>& outDevice,
                                AudioDeviceInfo& outInfo)
{
    outDevice.Reset();
    outInfo = AudioDeviceInfo{};

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CreateDeviceEnumerator(enumerator);
    if (FAILED(hr))
        return hr;

    ComPtr<IMMDevice> device;
    hr = enumerator->GetDevice(endpointId.c_str(), &device);
    if (FAILED(hr))
        return hr;

    AudioDeviceInfo info = GetAudioDeviceInfo(device.Get(), flow);

    // Make sure the endpoint ID is really for the requested flow.  The MMDevice
    // API can return an endpoint by ID regardless of whether the caller meant
    // capture or playback, so confirm by comparing it against the active list
    // for the requested flow.
    ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr))
        return hr;

    UINT count = 0;
    hr = collection->GetCount(&count);
    if (FAILED(hr))
        return hr;

    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> listedDevice;
        if (FAILED(collection->Item(i, &listedDevice)) || !listedDevice)
            continue;

        std::wstring listedId = GetDeviceId(listedDevice.Get());
        if (EqualsIgnoreCase(listedId, endpointId))
        {
            outDevice = device;
            outInfo = info;
            return S_OK;
        }
    }

    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

static HRESULT FindEndpointByOneName(EDataFlow flow,
                                     const std::wstring& name,
                                     ComPtr<IMMDevice>& outDevice,
                                     AudioDeviceInfo& outInfo)
{
    outDevice.Reset();
    outInfo = AudioDeviceInfo{};

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CreateDeviceEnumerator(enumerator);
    if (FAILED(hr))
        return hr;

    ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints(
        flow,
        DEVICE_STATE_ACTIVE,
        &collection);

    if (FAILED(hr))
        return hr;

    UINT count = 0;
    hr = collection->GetCount(&count);
    if (FAILED(hr))
        return hr;

    // FIRST PASS:
    // If the caller provided the full endpoint friendly name, treat that as an
    // exact match, not just a substring.  This makes the normal name command
    // deterministic when the exact endpoint friendly name is known.
    std::vector<MatchCandidate> exactEndpointFriendlyNameMatches;

    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, &device)) || !device)
            continue;

        AudioDeviceInfo info = GetAudioDeviceInfo(device.Get(), flow);

        if (EqualsIgnoreCase(info.endpointFriendlyName, name))
        {
            MatchCandidate c;
            c.device = device;
            c.info = info;
            c.score = 10000;
            exactEndpointFriendlyNameMatches.push_back(c);
        }
    }

    if (exactEndpointFriendlyNameMatches.size() == 1)
    {
        outDevice = exactEndpointFriendlyNameMatches[0].device;
        outInfo = exactEndpointFriendlyNameMatches[0].info;
        return S_OK;
    }

    if (exactEndpointFriendlyNameMatches.size() > 1)
    {
        std::wcerr << L"Ambiguous exact endpoint-friendly-name match for: "
                   << name << L"\n";
        std::wcerr << L"Use --set-mic-id or --agc-id with the endpoint ID from --list.\n\n";

        PrintMatchCandidates(exactEndpointFriendlyNameMatches);
        return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
    }

    // SECOND PASS:
    // No exact endpoint-friendly-name match was found, so fall back to
    // case-insensitive substring matching across all useful device identity
    // strings.
    std::vector<MatchCandidate> matches;

    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, &device)) || !device)
            continue;

        AudioDeviceInfo info = GetAudioDeviceInfo(device.Get(), flow);

        if (SingleNameMatchesDevice(info, name))
        {
            MatchCandidate c;
            c.device = device;
            c.info = info;
            c.score = SingleNameMatchScore(info, name);
            matches.push_back(c);
        }
    }

    if (matches.empty())
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    std::sort(matches.begin(), matches.end(),
              [](const MatchCandidate& a, const MatchCandidate& b)
              {
                  return a.score > b.score;
              });

    if (matches.size() > 1 && matches[0].score == matches[1].score)
    {
        std::wcerr << L"Ambiguous substring device-name match for: " << name << L"\n";
        std::wcerr << L"Use the safer two-name form or --set-mic-id / --agc-id.\n\n";

        PrintMatchCandidates(matches);
        return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
    }

    outDevice = matches[0].device;
    outInfo = matches[0].info;
    return S_OK;
}

static HRESULT FindEndpointByTwoNames(EDataFlow flow,
                                      const std::wstring& micNameFilter,
                                      const std::wstring& deviceNameFilter,
                                      ComPtr<IMMDevice>& outDevice,
                                      AudioDeviceInfo& outInfo)
{
    outDevice.Reset();
    outInfo = AudioDeviceInfo{};

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CreateDeviceEnumerator(enumerator);
    if (FAILED(hr))
        return hr;

    ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints(
        flow,
        DEVICE_STATE_ACTIVE,
        &collection);

    if (FAILED(hr))
        return hr;

    UINT count = 0;
    hr = collection->GetCount(&count);
    if (FAILED(hr))
        return hr;

    // FIRST PASS:
    // If micNameFilter is the full endpoint friendly name, use exact matching
    // for that field.  The second deviceNameFilter must still match the same
    // endpoint as a case-insensitive substring so it can disambiguate adapter,
    // device, or endpoint ID.
    std::vector<MatchCandidate> exactEndpointFriendlyNameMatches;

    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, &device)) || !device)
            continue;

        AudioDeviceInfo info = GetAudioDeviceInfo(device.Get(), flow);

        if (EqualsIgnoreCase(info.endpointFriendlyName, micNameFilter))
        {
            const bool deviceFilterMatches =
                ContainsIgnoreCase(info.deviceName, deviceNameFilter) ||
                ContainsIgnoreCase(info.adapterFriendlyName, deviceNameFilter) ||
                ContainsIgnoreCase(info.endpointFriendlyName, deviceNameFilter) ||
                ContainsIgnoreCase(info.endpointId, deviceNameFilter);

            if (deviceFilterMatches)
            {
                MatchCandidate c;
                c.device = device;
                c.info = info;
                c.score = 10000 + TwoNameMatchScore(info, micNameFilter, deviceNameFilter);
                exactEndpointFriendlyNameMatches.push_back(c);
            }
        }
    }

    if (exactEndpointFriendlyNameMatches.size() == 1)
    {
        outDevice = exactEndpointFriendlyNameMatches[0].device;
        outInfo = exactEndpointFriendlyNameMatches[0].info;
        return S_OK;
    }

    if (exactEndpointFriendlyNameMatches.size() > 1)
    {
        std::wcerr << L"Ambiguous exact endpoint-friendly-name match.\n";
        std::wcerr << L"  endpointFriendlyName = " << micNameFilter << L"\n";
        std::wcerr << L"  deviceNameSubstring  = " << deviceNameFilter << L"\n";
        std::wcerr << L"Use --set-mic-id or --agc-id with the endpoint ID from --list.\n\n";

        PrintMatchCandidates(exactEndpointFriendlyNameMatches);
        return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
    }

    // SECOND PASS:
    // No exact endpoint-friendly-name match was found, so fall back to the
    // normal two-substring matching behavior.
    std::vector<MatchCandidate> matches;

    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, &device)) || !device)
            continue;

        AudioDeviceInfo info = GetAudioDeviceInfo(device.Get(), flow);

        if (TwoNameMatchesDevice(info, micNameFilter, deviceNameFilter))
        {
            MatchCandidate c;
            c.device = device;
            c.info = info;
            c.score = TwoNameMatchScore(info, micNameFilter, deviceNameFilter);
            matches.push_back(c);
        }
    }

    if (matches.empty())
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    std::sort(matches.begin(), matches.end(),
              [](const MatchCandidate& a, const MatchCandidate& b)
              {
                  return a.score > b.score;
              });

    if (matches.size() > 1 && matches[0].score == matches[1].score)
    {
        std::wcerr << L"Ambiguous two-substring device match.\n";
        std::wcerr << L"  micNameSubstring    = " << micNameFilter << L"\n";
        std::wcerr << L"  deviceNameSubstring = " << deviceNameFilter << L"\n";
        std::wcerr << L"Use --set-mic-id or --agc-id with the endpoint ID from --list.\n\n";

        PrintMatchCandidates(matches);
        return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
    }

    outDevice = matches[0].device;
    outInfo = matches[0].info;
    return S_OK;
}

static HRESULT GetEndpointVolumePercent(IMMDevice* device,
                                        float& outPercent)
{
    outPercent = 0.0f;

    if (!device)
        return E_POINTER;

    ComPtr<IAudioEndpointVolume> endpointVolume;
    HRESULT hr = device->Activate(
        __uuidof(IAudioEndpointVolume),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(endpointVolume.GetAddressOf()));

    if (FAILED(hr))
        return hr;

    float scalar = 0.0f;
    hr = endpointVolume->GetMasterVolumeLevelScalar(&scalar);

    if (SUCCEEDED(hr))
        outPercent = scalar * 100.0f;

    return hr;
}

static HRESULT SetEndpointVolumePercent(IMMDevice* device,
                                        float percent)
{
    if (!device)
        return E_POINTER;

    if (percent < 0.0f)
        percent = 0.0f;

    if (percent > 100.0f)
        percent = 100.0f;

    const float scalar = percent / 100.0f;

    ComPtr<IAudioEndpointVolume> endpointVolume;
    HRESULT hr = device->Activate(
        __uuidof(IAudioEndpointVolume),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(endpointVolume.GetAddressOf()));

    if (FAILED(hr))
        return hr;

    return endpointVolume->SetMasterVolumeLevelScalar(scalar, nullptr);
}

static std::wstring GetPartName(IPart* part)
{
    if (!part)
        return L"";

    LPWSTR name = nullptr;
    HRESULT hr = part->GetName(&name);

    std::wstring result;

    if (SUCCEEDED(hr) && name)
    {
        result = name;
        CoTaskMemFree(name);
    }

    return result;
}

static std::wstring GetPartGlobalId(IPart* part)
{
    if (!part)
        return L"";

    LPWSTR id = nullptr;
    HRESULT hr = part->GetGlobalId(&id);

    std::wstring result;

    if (SUCCEEDED(hr) && id)
    {
        result = id;
        CoTaskMemFree(id);
    }

    return result;
}

static UINT GetPartLocalId(IPart* part)
{
    if (!part)
        return 0;

    UINT id = 0;
    part->GetLocalId(&id);
    return id;
}

static std::wstring PartTypeToString(PartType type)
{
    switch (type)
    {
    case Connector: return L"Connector";
    case Subunit:   return L"Subunit";
    default:        return L"Unknown";
    }
}

static std::wstring GetPartTypeText(IPart* part)
{
    if (!part)
        return L"Unknown";

    PartType type = {};
    HRESULT hr = part->GetPartType(&type);

    if (FAILED(hr))
        return L"Unknown";

    return PartTypeToString(type);
}

static std::wstring MakePartVisitedKey(IPart* part)
{
    if (!part)
        return L"";

    std::wstring globalId = GetPartGlobalId(part);

    if (!globalId.empty())
        return globalId;

    std::wstringstream ss;
    ss << L"local:" << GetPartLocalId(part)
       << L":name:" << GetPartName(part);

    return ss.str();
}



// Compatibility wrappers for IAudioAutoGainControl.
//
// Microsoft Windows SDK declares:
//   HRESULT GetEnabled(BOOL* pbEnabled);
//   HRESULT SetEnabled(BOOL bEnable, LPCGUID pguidEventContext);
//
// Some MinGW/Wine-derived devicetopology.h headers have IAudioAutoGainControl
// declarations that are misnamed or have the wrong signatures. Calling those
// methods through the header can compile incorrectly or call the wrong vtable
// slot.
//
// To avoid that, MinGW builds call the real COM vtable slots directly:
//
//   IUnknown::QueryInterface
//   IUnknown::AddRef
//   IUnknown::Release
//   IAudioAutoGainControl::GetEnabled
//   IAudioAutoGainControl::SetEnabled
//
// MSVC builds use the normal Windows SDK method names.

struct RawIAudioAutoGainControlVtbl
{
    HRESULT (STDMETHODCALLTYPE* QueryInterface)(void* self,
                                               REFIID riid,
                                               void** ppv);

    ULONG (STDMETHODCALLTYPE* AddRef)(void* self);

    ULONG (STDMETHODCALLTYPE* Release)(void* self);

    HRESULT (STDMETHODCALLTYPE* GetEnabled)(void* self,
                                           BOOL* pbEnabled);

    HRESULT (STDMETHODCALLTYPE* SetEnabled)(void* self,
                                           BOOL bEnable,
                                           LPCGUID pguidEventContext);
};

struct RawIAudioAutoGainControl
{
    RawIAudioAutoGainControlVtbl* lpVtbl;
};

static HRESULT AgcGetEnabled(IAudioAutoGainControl* agc,
                             BOOL* enabled)
{
    if (!agc || !enabled)
        return E_POINTER;

#if defined(__MINGW32__) || defined(__MINGW64__)
    auto* raw = reinterpret_cast<RawIAudioAutoGainControl*>(agc);

    if (!raw || !raw->lpVtbl || !raw->lpVtbl->GetEnabled)
        return E_POINTER;

    return raw->lpVtbl->GetEnabled(raw, enabled);
#else
    return agc->GetEnabled(enabled);
#endif
}

static HRESULT AgcSetEnabled(IAudioAutoGainControl* agc,
                             BOOL enabled,
                             LPCGUID eventContext)
{
    if (!agc)
        return E_POINTER;

#if defined(__MINGW32__) || defined(__MINGW64__)
    auto* raw = reinterpret_cast<RawIAudioAutoGainControl*>(agc);

    if (!raw || !raw->lpVtbl || !raw->lpVtbl->SetEnabled)
        return E_POINTER;

    return raw->lpVtbl->SetEnabled(raw, enabled, eventContext);
#else
    return agc->SetEnabled(enabled, eventContext);
#endif
}

static void VisitPartForAgc(IPart* part,
                            bool changeAgc,
                            bool enableAgc,
                            std::set<std::wstring>& visited,
                            AgcStats& stats,
                            int depth);

static void VisitPartListForAgc(IPart* part,
                                bool outgoing,
                                bool changeAgc,
                                bool enableAgc,
                                std::set<std::wstring>& visited,
                                AgcStats& stats,
                                int depth)
{
    if (!part)
        return;

    ComPtr<IPartsList> partsList;

    HRESULT hr = outgoing
                     ? part->EnumPartsOutgoing(&partsList)
                     : part->EnumPartsIncoming(&partsList);

    if (FAILED(hr) || !partsList)
        return;

    UINT count = 0;
    hr = partsList->GetCount(&count);

    if (FAILED(hr))
        return;

    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IPart> nextPart;
        hr = partsList->GetPart(i, &nextPart);

        if (SUCCEEDED(hr) && nextPart)
        {
            VisitPartForAgc(
                nextPart.Get(),
                changeAgc,
                enableAgc,
                visited,
                stats,
                depth + 1);
        }
    }
}

static void VisitPartForAgc(IPart* part,
                            bool changeAgc,
                            bool enableAgc,
                            std::set<std::wstring>& visited,
                            AgcStats& stats,
                            int depth)
{
    if (!part)
        return;

    // Avoid runaway traversal in unusual driver topologies.
    if (depth > 64)
        return;

    std::wstring visitedKey = MakePartVisitedKey(part);
    if (!visitedKey.empty())
    {
        if (visited.find(visitedKey) != visited.end())
            return;

        visited.insert(visitedKey);
    }

    ComPtr<IAudioAutoGainControl> agc;

    HRESULT hr = part->Activate(
        CLSCTX_ALL,
        __uuidof(IAudioAutoGainControl),
        reinterpret_cast<void**>(agc.GetAddressOf()));

    if (SUCCEEDED(hr) && agc)
    {
        ++stats.foundCount;

        BOOL oldEnabled = FALSE;
        HRESULT getHr = AgcGetEnabled(agc.Get(), &oldEnabled);

        std::wstring partName = GetPartName(part);
        std::wstring globalId = GetPartGlobalId(part);
        UINT localId = GetPartLocalId(part);
        std::wstring partType = GetPartTypeText(part);

        if (partName.empty())
            partName = L"<unnamed AGC part>";

        std::wcout << L"    AGC name      : " << partName << L"\n";
        std::wcout << L"    AGC part type : " << partType << L"\n";
        std::wcout << L"    AGC local ID  : " << localId << L"\n";

        if (!globalId.empty())
            std::wcout << L"    AGC global ID : " << globalId << L"\n";

        if (SUCCEEDED(getHr))
        {
            std::wcout << L"    AGC state     : "
                       << (oldEnabled ? L"enabled" : L"disabled")
                       << L"\n";
        }
        else
        {
            std::wcout << L"    AGC state     : unknown, GetEnabled failed 0x"
                       << std::hex << getHr << std::dec << L"\n";
        }

        if (changeAgc)
        {
            HRESULT setHr = AgcSetEnabled(agc.Get(), enableAgc ? TRUE : FALSE, nullptr);

            if (SUCCEEDED(setHr))
            {
                ++stats.changedCount;

                std::wcout << L"    AGC new state : "
                           << (enableAgc ? L"enabled" : L"disabled")
                           << L"\n";
            }
            else
            {
                std::wcout << L"    AGC set failed: 0x"
                           << std::hex << setHr << std::dec << L"\n";
            }
        }

        std::wcout << L"\n";
    }

    // Drivers can expose topology differently, so walk both directions.
    VisitPartListForAgc(
        part,
        true,
        changeAgc,
        enableAgc,
        visited,
        stats,
        depth);

    VisitPartListForAgc(
        part,
        false,
        changeAgc,
        enableAgc,
        visited,
        stats,
        depth);
}

static HRESULT PrintOrSetAgcForDevice(IMMDevice* device,
                                      bool changeAgc,
                                      bool enableAgc,
                                      AgcStats& stats)
{
    stats = AgcStats{};

    if (!device)
        return E_POINTER;

    ComPtr<IDeviceTopology> topology;
    HRESULT hr = device->Activate(
        __uuidof(IDeviceTopology),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(topology.GetAddressOf()));

    if (FAILED(hr))
        return hr;

    UINT connectorCount = 0;
    hr = topology->GetConnectorCount(&connectorCount);

    if (FAILED(hr))
        return hr;

    std::set<std::wstring> visited;

    for (UINT i = 0; i < connectorCount; ++i)
    {
        ComPtr<IConnector> endpointConnector;
        hr = topology->GetConnector(i, &endpointConnector);

        if (FAILED(hr) || !endpointConnector)
            continue;

        // Try the endpoint connector itself as an IPart.
        ComPtr<IPart> endpointPart;
        if (SUCCEEDED(endpointConnector.As(&endpointPart)) && endpointPart)
        {
            VisitPartForAgc(
                endpointPart.Get(),
                changeAgc,
                enableAgc,
                visited,
                stats,
                0);
        }

        // Try the connected adapter/hardware side too.
        ComPtr<IConnector> connectedConnector;
        hr = endpointConnector->GetConnectedTo(&connectedConnector);

        if (SUCCEEDED(hr) && connectedConnector)
        {
            ComPtr<IPart> connectedPart;
            if (SUCCEEDED(connectedConnector.As(&connectedPart)) && connectedPart)
            {
                VisitPartForAgc(
                    connectedPart.Get(),
                    changeAgc,
                    enableAgc,
                    visited,
                    stats,
                    0);
            }
        }
    }

    return S_OK;
}

static HRESULT ListEndpoints(EDataFlow flow)
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CreateDeviceEnumerator(enumerator);

    if (FAILED(hr))
        return hr;

    ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints(
        flow,
        DEVICE_STATE_ACTIVE,
        &collection);

    if (FAILED(hr))
        return hr;

    UINT count = 0;
    hr = collection->GetCount(&count);

    if (FAILED(hr))
        return hr;

    std::wcout << L"\n==== " << DataFlowToString(flow)
               << L" devices: " << count << L" active ====\n\n";

    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> device;
        hr = collection->Item(i, &device);

        if (FAILED(hr) || !device)
            continue;

        AudioDeviceInfo info = GetAudioDeviceInfo(device.Get(), flow);

        std::wcout << L"[" << i << L"]\n";
        PrintAudioDeviceInfo(info);

        float volumePercent = 0.0f;
        if (SUCCEEDED(GetEndpointVolumePercent(device.Get(), volumePercent)))
        {
            std::wcout << L"  Endpoint volume        : "
                       << std::fixed << std::setprecision(1)
                       << volumePercent << L"%\n";
        }

        if (flow == eCapture)
        {
            std::wcout << L"  Exposed AGC controls   :\n";

            AgcStats stats;
            HRESULT agcHr = PrintOrSetAgcForDevice(
                device.Get(),
                false,
                false,
                stats);

            if (FAILED(agcHr))
            {
                std::wcout << L"    Could not inspect AGC topology. HRESULT=0x"
                           << std::hex << agcHr << std::dec << L"\n";
            }
            else if (stats.foundCount == 0)
            {
                std::wcout << L"    None found\n";
            }
        }

        std::wcout << L"\n";
    }

    return S_OK;
}

static HRESULT RunAgcForMatchedDevice(IMMDevice* micDevice,
                                      const AudioDeviceInfo& micInfo,
                                      bool changeAgc,
                                      bool enableAgc)
{
    std::wcout << L"Matched microphone:\n";
    PrintAudioDeviceInfo(micInfo);

    std::wcout << L"\nAGC controls:\n";

    AgcStats stats;
    HRESULT hr = PrintOrSetAgcForDevice(
        micDevice,
        changeAgc,
        enableAgc,
        stats);

    if (FAILED(hr))
    {
        std::wcerr << L"Could not inspect/set AGC topology. HRESULT=0x"
                   << std::hex << hr << std::dec << L"\n";
        return hr;
    }

    if (stats.foundCount == 0)
    {
        std::wcout << L"  No hardware AGC controls exposed by this driver.\n";
    }
    else if (changeAgc)
    {
        std::wcout << L"AGC controls found   : " << stats.foundCount << L"\n";
        std::wcout << L"AGC controls changed : " << stats.changedCount << L"\n";
    }

    return S_OK;
}

static HRESULT RunSetMicForMatchedDevice(IMMDevice* micDevice,
                                         const AudioDeviceInfo& micInfo,
                                         float volumePercent)
{
    std::wcout << L"Matched microphone:\n";
    PrintAudioDeviceInfo(micInfo);

    float oldVolume = 0.0f;
    if (SUCCEEDED(GetEndpointVolumePercent(micDevice, oldVolume)))
    {
        std::wcout << L"\nOld microphone volume: "
                   << std::fixed << std::setprecision(1)
                   << oldVolume << L"%\n";
    }

    HRESULT hr = SetEndpointVolumePercent(micDevice, volumePercent);

    if (FAILED(hr))
    {
        std::wcerr << L"Failed to set microphone volume. HRESULT=0x"
                   << std::hex << hr << std::dec << L"\n";
        return hr;
    }

    std::wcout << L"New microphone volume: "
               << std::fixed << std::setprecision(1)
               << volumePercent << L"%\n";

    std::wcout << L"\nDisabling exposed AGC controls:\n";

    AgcStats stats;
    hr = PrintOrSetAgcForDevice(
        micDevice,
        true,
        false,
        stats);

    if (FAILED(hr))
    {
        std::wcerr << L"Could not inspect/set AGC topology. HRESULT=0x"
                   << std::hex << hr << std::dec << L"\n";
        return hr;
    }

    if (stats.foundCount == 0)
    {
        std::wcout << L"  No hardware AGC controls exposed by this driver.\n";
    }
    else
    {
        std::wcout << L"AGC controls found   : " << stats.foundCount << L"\n";
        std::wcout << L"AGC controls changed : " << stats.changedCount << L"\n";
    }

    return S_OK;
}


#endif