/*
 * MIT License
 *
 * Copyright (C) 2024 by wangwenx190 (Yuhang Zhao)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* Most code is based on https://github.com/LizardByte/Sunshine/blob/master/tools/dxgi.cpp */

#include <windows.h>
#include <versionhelpers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <io.h>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <unordered_map>

using namespace Microsoft::WRL;

struct LibraryDeleter final {
    void operator()(const HMODULE dll) const {
        if (dll) {
            ::FreeLibrary(dll);
        }
    }
};
using ScopedLibrary = std::unique_ptr<std::remove_pointer_t<HMODULE>, LibraryDeleter>;

#define LOAD_DLL(DLL, VAR) VAR = ScopedLibrary{ ::LoadLibraryExW(L## #DLL, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) }
#define DECL_API(SYM, VAR) decltype(&::SYM) p##SYM{ nullptr }
#define LOAD_API(DLL, SYM) p##SYM = reinterpret_cast<decltype(&::SYM)>(::GetProcAddress(DLL, #SYM))

static constexpr const float kDefaultSDRWhiteLevel{ 200.f };
static constexpr const DXGI_FORMAT kDefaultPixelFormat{ DXGI_FORMAT_R8G8B8A8_UNORM };
static constexpr const std::wstring_view kColorDefault{ L"\x1b[0m" };
static constexpr const std::wstring_view kColorRed{ L"\x1b[1;31m" };
static constexpr const std::wstring_view kColorGreen{ L"\x1b[1;32m" };
static constexpr const std::wstring_view kColorYellow{ L"\x1b[1;33m" };
static constexpr const std::wstring_view kColorBlue{ L"\x1b[1;34m" };
static constexpr const std::wstring_view kColorMagenta{ L"\x1b[1;35m" };
static constexpr const std::wstring_view kColorCyan{ L"\x1b[1;36m" };

enum class Vendor : std::int8_t {
    Unknown = -1,
    // PCI-SIG-registered vendors
    AMD,
    Apple,
    ARM,
    Google,
    ImgTec,
    Intel,
    Microsoft,
    Nvidia,
    Qualcomm,
    Samsung,
    Broadcom,
    VMWare,
    VirtIO,
    // Khronos-registered vendors
    Vivante,
    VeriSilicon,
    Kazan,
    CodePlay,
    Mesa,
    PoCL,
};

// Based on //third_party/angle/src/gpu_info_util/SystemInfo.h
static const std::unordered_map<std::uint64_t, Vendor> vendorIdMap = {
    { 0x0,     Vendor::Unknown },
    { 0x1002,  Vendor::AMD },
    { 0x106B,  Vendor::Apple },
    { 0x13B5,  Vendor::ARM },
    { 0x1AE0,  Vendor::Google },
    { 0x1010,  Vendor::ImgTec },
    { 0x8086,  Vendor::Intel },
    { 0x1414,  Vendor::Microsoft },
    { 0x10DE,  Vendor::Nvidia },
    { 0x5143,  Vendor::Qualcomm },
    { 0x144D,  Vendor::Samsung },
    { 0x14E4,  Vendor::Broadcom },
    { 0x15AD,  Vendor::VMWare },
    { 0x1AF4,  Vendor::VirtIO },
    { 0x10001, Vendor::Vivante },
    { 0x10002, Vendor::VeriSilicon },
    { 0x10003, Vendor::Kazan },
    { 0x10004, Vendor::CodePlay },
    { 0x10005, Vendor::Mesa },
    { 0x10006, Vendor::PoCL },
};

static const std::unordered_map<Vendor, std::wstring_view> vendorNameMap = {
    { Vendor::Unknown,     L"Unknown" },
    { Vendor::AMD,         L"AMD" },
    { Vendor::Apple,       L"Apple" },
    { Vendor::ARM,         L"ARM" },
    { Vendor::Google,      L"Google" },
    { Vendor::ImgTec,      L"Img Tec" },
    { Vendor::Intel,       L"Intel" },
    { Vendor::Microsoft,   L"Microsoft" },
    { Vendor::Nvidia,      L"Nvidia" },
    { Vendor::Qualcomm,    L"Qualcomm" },
    { Vendor::Samsung,     L"Samsung" },
    { Vendor::Broadcom,    L"Broadcom" },
    { Vendor::VMWare,      L"VMWare" },
    { Vendor::VirtIO,      L"VirtIO" },
    { Vendor::Vivante,     L"Vivante" },
    { Vendor::VeriSilicon, L"VeriSilicon" },
    { Vendor::Kazan,       L"Kazan" },
    { Vendor::CodePlay,    L"CodePlay" },
    { Vendor::Mesa,        L"Mesa" },
    { Vendor::PoCL,        L"PoCL" },
};

[[nodiscard]] static inline std::wstring getWin32ErrorMessage(const DWORD dwError) {
    LPWSTR buf{ nullptr };
    ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, dwError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                     reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring str{ buf };
    ::LocalFree(buf);
    return str;
}

[[nodiscard]] static inline std::wstring getLastWin32ErrorMessage() {
    return getWin32ErrorMessage(::GetLastError());
}

[[nodiscard]] static inline std::wstring getComErrorMessage(const HRESULT hr) {
    return getWin32ErrorMessage(HRESULT_CODE(hr));
}

struct DLLBase {
    DLLBase() = default;
    ~DLLBase() = default;

    [[nodiscard]] inline bool isAvailable() const {
        return m_dll != nullptr;
    }

    [[nodiscard]] inline operator bool() const {
        return isAvailable();
    }

    [[nodiscard]] inline HMODULE get() const {
        return m_dll ? m_dll.get() : nullptr;
    }

private:
    DLLBase(const DLLBase&) = delete;
    DLLBase& operator=(const DLLBase&) = delete;

protected:
    ScopedLibrary m_dll{ nullptr };
};
#define DLLBASE_DECL_INSTANCE(Class) \
    [[nodiscard]] static inline const Class& instance() { \
        static const Class inst; \
        return inst; \
    }

struct User32DLL final : public DLLBase {
    DLLBASE_DECL_INSTANCE(User32DLL)

    // Windows 2000
    DECL_API(GetMonitorInfoW);
    // Windows Vista
    DECL_API(GetDisplayConfigBufferSizes);
    DECL_API(DisplayConfigGetDeviceInfo);
    // Windows 7
    DECL_API(QueryDisplayConfig);
    // Windows 10, version 1703
    DECL_API(SetProcessDpiAwarenessContext);

private:
    User32DLL() : DLLBase() {
        LOAD_DLL(user32, m_dll);
        if (m_dll) {
            LOAD_API(m_dll.get(), GetMonitorInfoW);
            if (!pGetMonitorInfoW) {
                std::wcerr << L"Failed to resolve \"GetMonitorInfoW\" from \"user32.dll\": " << getLastWin32ErrorMessage() << std::endl;
            }
            if (::IsWindowsVistaOrGreater()) {
                LOAD_API(m_dll.get(), GetDisplayConfigBufferSizes);
                if (!pGetDisplayConfigBufferSizes) {
                    std::wcerr << L"Failed to resolve \"GetDisplayConfigBufferSizes\" from \"user32.dll\": " << getLastWin32ErrorMessage() << std::endl;
                }
                LOAD_API(m_dll.get(), DisplayConfigGetDeviceInfo);
                if (!pDisplayConfigGetDeviceInfo) {
                    std::wcerr << L"Failed to resolve \"DisplayConfigGetDeviceInfo\" from \"user32.dll\": " << getLastWin32ErrorMessage() << std::endl;
                }
                if (::IsWindows7OrGreater()) {
                    LOAD_API(m_dll.get(), QueryDisplayConfig);
                    if (!pQueryDisplayConfig) {
                        std::wcerr << L"Failed to resolve \"QueryDisplayConfig\" from \"user32.dll\": " << getLastWin32ErrorMessage() << std::endl;
                    }
                    if (::IsWindows10OrGreater()) {
                        LOAD_API(m_dll.get(), SetProcessDpiAwarenessContext);
                        if (!pSetProcessDpiAwarenessContext) {
                            std::wcerr << L"Failed to resolve \"SetProcessDpiAwarenessContext\" from \"user32.dll\": " << getLastWin32ErrorMessage() << std::endl;
                        }
                    }
                }
            }
        } else {
            std::wcerr << L"Failed to load \"user32.dll\": " << getLastWin32ErrorMessage() << std::endl;
        }
    }

    ~User32DLL() = default;
};
#define USER32_AVAILABLE (User32DLL::instance().isAvailable())
#define USER32_API(Name) (User32DLL::instance().p##Name)

struct DXGIDLL final : public DLLBase {
    DLLBASE_DECL_INSTANCE(DXGIDLL)

    DECL_API(CreateDXGIFactory1);

private:
    DXGIDLL() : DLLBase() {
        LOAD_DLL(dxgi, m_dll);
        if (m_dll) {
            if (::IsWindows7OrGreater()) {
                LOAD_API(m_dll.get(), CreateDXGIFactory1);
                if (!pCreateDXGIFactory1) {
                    std::wcerr << L"Failed to resolve \"CreateDXGIFactory1\" from \"dxgi.dll\": " << getLastWin32ErrorMessage() << std::endl;
                }
            }
        } else {
            std::wcerr << L"Failed to load \"dxgi.dll\": " << getLastWin32ErrorMessage() << std::endl;
        }
    }

    ~DXGIDLL() = default;
};
#define DXGI_AVAILABLE (DXGIDLL::instance().isAvailable())
#define DXGI_API(Name) (DXGIDLL::instance().p##Name)

[[nodiscard]] static inline Vendor vendorIdToVendor(const std::uint64_t vendorId) {
    const auto it = vendorIdMap.find(vendorId);
    if (it != vendorIdMap.end()) {
        return it->second;
    }
    return Vendor::Unknown;
}

[[nodiscard]] static inline bool getSdrWhiteLevelInNit(const DXGI_OUTPUT_DESC1 &outputDesc, float& levelOut)
{
    if (!USER32_API(GetMonitorInfoW) || !USER32_API(GetDisplayConfigBufferSizes) || !USER32_API(DisplayConfigGetDeviceInfo) || !USER32_API(QueryDisplayConfig)) {
        return false;
    }
    std::vector<DISPLAYCONFIG_PATH_INFO> pathInfos{};
    std::uint32_t pathInfoCount{ 0 };
    std::uint32_t modeInfoCount{ 0 };
    LONG result{ ERROR_SUCCESS };
    do {
        if (USER32_API(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, &pathInfoCount, &modeInfoCount) == ERROR_SUCCESS) {
            pathInfos.resize(pathInfoCount);
            std::vector<DISPLAYCONFIG_MODE_INFO> modeInfos(modeInfoCount);
            result = USER32_API(QueryDisplayConfig)(QDC_ONLY_ACTIVE_PATHS, &pathInfoCount, pathInfos.data(), &modeInfoCount, modeInfos.data(), nullptr);
        } else {
            std::wcerr << L"\"GetDisplayConfigBufferSizes\" failed: " << getLastWin32ErrorMessage() << std::endl;
            return false;
        }
    } while (result == ERROR_INSUFFICIENT_BUFFER);
    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!USER32_API(GetMonitorInfoW)(outputDesc.Monitor, &monitorInfo)) {
        std::wcerr << L"\"GetMonitorInfoW\" failed: " << getLastWin32ErrorMessage() << std::endl;
        return false;
    }
    for (auto&& info : std::as_const(pathInfos)) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME deviceName{};
        deviceName.header.size = sizeof(deviceName);
        deviceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        deviceName.header.adapterId = info.sourceInfo.adapterId;
        deviceName.header.id = info.sourceInfo.id;
        if (USER32_API(DisplayConfigGetDeviceInfo)(&deviceName.header) == ERROR_SUCCESS) {
            if (std::wcscmp(monitorInfo.szDevice, deviceName.viewGdiDeviceName) == 0) {
                DISPLAYCONFIG_SDR_WHITE_LEVEL whiteLevel{};
                whiteLevel.header.size = sizeof(whiteLevel);
                whiteLevel.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
                whiteLevel.header.adapterId = info.targetInfo.adapterId;
                whiteLevel.header.id = info.targetInfo.id;
                if (USER32_API(DisplayConfigGetDeviceInfo)(&whiteLevel.header) == ERROR_SUCCESS) {
                    levelOut = static_cast<float>(whiteLevel.SDRWhiteLevel) / 1000.f * 80.f; // MSDN told me this formula ...
                    return true;
                } else {
                    std::wcerr << L"\"DisplayConfigGetDeviceInfo\" failed: " << getLastWin32ErrorMessage() << std::endl;
                }
            }
        } else {
            std::wcerr << L"\"DisplayConfigGetDeviceInfo\" failed: " << getLastWin32ErrorMessage() << std::endl;
        }
    }
    return false;
}

extern "C" int WINAPI wmain(int, wchar_t**) {
    std::setlocale(LC_ALL, "C.UTF-8");
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);
    ::SetConsoleCP(CP_UTF8);
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleTitleW(L"GPU Test Tool");
    if (::IsWindows10OrGreater()) {
        const auto enableVTSequencesForConsole = [](const DWORD dwHandle) -> bool {
            const HANDLE handle = ::GetStdHandle(dwHandle);
            if (!handle || (handle == INVALID_HANDLE_VALUE)) {
                return false;
            }
            DWORD dwMode{ 0 };
            if (!::GetConsoleMode(handle, &dwMode)) {
                return false;
            }
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            return ::SetConsoleMode(handle, dwMode);
        };
        enableVTSequencesForConsole(STD_OUTPUT_HANDLE);
        enableVTSequencesForConsole(STD_ERROR_HANDLE);
    }
    std::ios::sync_with_stdio(false);
    if (!USER32_AVAILABLE) {
        std::wcerr << kColorRed << L"We need an available \"user32.dll\" to be able to use this tool." << kColorDefault << std::endl;
        return EXIT_FAILURE;
    }
    if (!DXGI_AVAILABLE) {
        std::wcerr << kColorRed << L"We need an available \"dxgi.dll\" to be able to use this tool." << kColorDefault << std::endl;
        return EXIT_FAILURE;
    }
    if (USER32_API(SetProcessDpiAwarenessContext)) {
        if (!USER32_API(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            const DWORD dwError = ::GetLastError();
            if (dwError != ERROR_ACCESS_DENIED) { // Setting the DPI awareness level in the manifest file will cause this "Access Denied" error.
                std::wcerr << L"\"SetProcessDpiAwarenessContex\" failed: " << getWin32ErrorMessage(dwError) << std::endl;
                return EXIT_FAILURE;
            }
        }
    }
    if (!DXGI_API(CreateDXGIFactory1)) {
        return EXIT_FAILURE;
    }
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = DXGI_API(CreateDXGIFactory1)(IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) {
        std::wcerr << L"\"CreateDXGIFactory1\" failed: " << getComErrorMessage(hr) << std::endl;
        return EXIT_FAILURE;
    }
    bool variableRefreshRateSupported{ false };
    {
        ComPtr<IDXGIFactory5> factory5;
        hr = factory->QueryInterface(IID_PPV_ARGS(factory5.GetAddressOf()));
        if (SUCCEEDED(hr)) {
            BOOL allowTearing{ FALSE };
            hr = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
            variableRefreshRateSupported = SUCCEEDED(hr) && allowTearing;
        }
    }
    ComPtr<IDXGIAdapter1> adapter;
    for (std::uint32_t adapterIndex = 0; factory->EnumAdapters1(adapterIndex, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++adapterIndex) {
        DXGI_ADAPTER_DESC1 adapterDesc1{};
        hr = adapter->GetDesc1(&adapterDesc1);
        if (FAILED(hr)) {
            std::wcerr << L"\"IDXGIAdapter1::GetDesc1\" failed: " << getComErrorMessage(hr) << std::endl;
            continue;
        }
        std::wcout << kColorBlue << L"##############################" << kColorDefault << std::endl;
        std::wcout << kColorGreen << L"GPU #" << adapterIndex + 1 << L':' << kColorDefault << std::endl;
        std::wcout << L"Device name: " << adapterDesc1.Description << std::endl;
        std::wcout << L"Vendor ID: 0x" << std::hex << adapterDesc1.VendorId << std::dec;
        {
            const Vendor vendor = vendorIdToVendor(adapterDesc1.VendorId);
            if (vendor != Vendor::Unknown) {
                std::wcout << L" (" << vendorNameMap.at(vendor) << L')';
            }
            std::wcout << std::endl;
        }
        std::wcout << L"Device ID: 0x" << std::hex << adapterDesc1.DeviceId << std::dec << std::endl;
        std::wcout << L"Dedicated video memory: " << adapterDesc1.DedicatedVideoMemory / 1048576 << L" MiB" << std::endl;
        std::wcout << L"Dedicated system memory: " << adapterDesc1.DedicatedSystemMemory / 1048576 << L" MiB" << std::endl;
        std::wcout << L"Shared system memory: " << adapterDesc1.SharedSystemMemory / 1048576 << L" MiB" << std::endl;
        std::wcout << L"Software simulation (rendered by CPU): " << ((adapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? L"Yes" : L"No") << std::endl;
        std::wcout << L"Variable refresh rate supported: " << (variableRefreshRateSupported ? L"Yes" : L"No") << std::endl;
        ComPtr<IDXGIOutput> output;
        for (std::uint32_t outputIndex = 0; adapter->EnumOutputs(outputIndex, output.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++outputIndex) {
            DXGI_OUTPUT_DESC outputDesc{};
            hr = output->GetDesc(&outputDesc);
            if (FAILED(hr)) {
                std::wcerr << L"\"IDXGIOutput::GetDesc\" failed: " << getComErrorMessage(hr) << std::endl;
                continue;
            }
            const auto width = std::abs(outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left);
            const auto height = std::abs(outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top);
            const auto rotation = [&outputDesc]() {
                switch (outputDesc.Rotation) {
                    case DXGI_MODE_ROTATION_UNSPECIFIED:
                        return L"Unspecified";
                    case DXGI_MODE_ROTATION_IDENTITY:
                        return L"0";
                    case DXGI_MODE_ROTATION_ROTATE90:
                        return L"90";
                    case DXGI_MODE_ROTATION_ROTATE180:
                        return L"180";
                    case DXGI_MODE_ROTATION_ROTATE270:
                        return L"270";
                    default:
                        return L"Unknown";
                }
            }();
            std::wcout << kColorRed << L"-------------------------------" << kColorDefault << std::endl;
            std::wcout << kColorYellow << L"Output #" << outputIndex + 1 << L':' << kColorDefault << std::endl;
            std::wcout << L"Device name: " << outputDesc.DeviceName << std::endl;
            std::wcout << L"Desktop geometry: x: " << outputDesc.DesktopCoordinates.left << L", y: " << outputDesc.DesktopCoordinates.top << L", width: " << width << L", height: " << height << std::endl;
            std::wcout << L"Attached to desktop: " << (outputDesc.AttachedToDesktop ? L"Yes" : L"No") << std::endl;
            std::wcout << L"Rotation: " << rotation << L" degree" << std::endl;
            {
                ComPtr<IDXGIOutput1> output1;
                hr = output->QueryInterface(IID_PPV_ARGS(output1.GetAddressOf()));
                if (SUCCEEDED(hr)) {
                    std::uint32_t modeCount{ 0 };
                    hr = output1->GetDisplayModeList1(kDefaultPixelFormat, 0, &modeCount, nullptr);
                    if (SUCCEEDED(hr) && modeCount > 0) {
                        const auto modeList = std::make_unique<DXGI_MODE_DESC1[]>(modeCount);
                        hr = output1->GetDisplayModeList1(kDefaultPixelFormat, 0, &modeCount, modeList.get());
                        if (SUCCEEDED(hr)) {
                            float maxRefreshRate{ 60.f };
                            for (std::size_t modeIndex = 0; modeIndex != static_cast<std::size_t>(modeCount); ++modeIndex) {
                                const DXGI_MODE_DESC1& mode = modeList[modeIndex];
                                const auto refreshRate = static_cast<float>(mode.RefreshRate.Numerator) / static_cast<float>(mode.RefreshRate.Denominator);
                                maxRefreshRate = std::max(maxRefreshRate, refreshRate);
                            }
                            std::wcout << L"Maximum refresh rate: " << maxRefreshRate << L" Hz" << std::endl;
                        }
                    }
                }
            }
            {
                // refresh rate
            }
            {
                ComPtr<IDXGIOutput6> output6;
                hr = output->QueryInterface(IID_PPV_ARGS(output6.GetAddressOf()));
                if (SUCCEEDED(hr)) {
                    DXGI_OUTPUT_DESC1 outputDesc1{};
                    hr = output6->GetDesc1(&outputDesc1);
                    if (SUCCEEDED(hr)) {
                        const auto colorSpace = [&outputDesc1]() {
                            switch (outputDesc1.ColorSpace) {
                                case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
                                    return L"[sRGB] RGB (0-255), gamma: 2.2, siting: image, primaries: BT.709";
                                case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
                                    return L"[scRGB] RGB (0-255), gamma: 1.0, siting: image, primaries: BT.709";
                                case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:
                                    return L"[ITU-R] RGB (16-235), gamma: 2.2, siting: image, primaries: BT.709";
                                case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020:
                                    return L"RGB (16-235), gamma: 2.2, siting: image, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_YCBCR_FULL_G22_NONE_P709_X601:
                                    return L"YCbCr (0-255), gamma: 2.2, siting: image, primaries: BT.709, transfer matrix: BT.601";
                                case DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601:
                                    return L"YCbCr (16-235), gamma: 2.2, siting: video, primaries: BT.601";
                                case DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601:
                                    return L"YCbCr (0-255), gamma: 2.2, siting: video, primaries: BT.601";
                                case DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709:
                                    return L"YCbCr (16-235), gamma: 2.2, siting: video, primaries: BT.709";
                                case DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709:
                                    return L"YCbCr (0-255), gamma: 2.2, siting: video, primaries: BT.709";
                                case DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020:
                                    return L"YCbCr (16-235), gamma: 2.2, siting: video, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020:
                                    return L"YCbCr (0-255), gamma: 2.2, siting: video, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
                                    return L"RGB (0-255), gamma: 2084, siting: image, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020:
                                    return L"YCbCr (16-235), gamma: 2084, siting: video, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
                                    return L"RGB (16-235), gamma: 2084, siting: image, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020:
                                    return L"YCbCr (16-235), gamma: 2.2, siting: video, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020:
                                    return L"YCbCr (16-235), gamma: 2084, siting: video, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020:
                                    return L"RGB (0-255), gamma: 2.2, siting: image, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020:
                                    return L"YCbCr (16-235), gamma: HLG, siting: video, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020:
                                    return L"YCbCr (0-255), gamma: HLG, siting: video, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709:
                                    return L"RGB (16-235), gamma: 2.4, siting: image, primaries: BT.709";
                                case DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020:
                                    return L"RGB (16-235), gamma: 2.4, siting: image, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P709:
                                    return L"YCbCr (16-235), gamma: 2.4, siting: video, primaries: BT.709";
                                case DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P2020:
                                    return L"YCbCr (16-235), gamma: 2.4, siting: video, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_TOPLEFT_P2020:
                                    return L"YCbCr (16-235), gamma: 2.4, siting: video, primaries: BT.2020";
                                default:
                                    return L"Unknown";
                            }
                        }();
                        float sdrWhiteLevel{ kDefaultSDRWhiteLevel };
                        std::wcout << L"Bits per color: " << outputDesc1.BitsPerColor << std::endl;
                        std::wcout << L"Color space: " << colorSpace << std::endl;
                        std::wcout << L"Red primary: " << outputDesc1.RedPrimary[0] << L", " << outputDesc1.RedPrimary[1] << std::endl;
                        std::wcout << L"Green primary: " << outputDesc1.GreenPrimary[0] << L", " << outputDesc1.GreenPrimary[1] << std::endl;
                        std::wcout << L"Blue primary: " << outputDesc1.BluePrimary[0] << L", " << outputDesc1.BluePrimary[1] << std::endl;
                        std::wcout << L"White point: " << outputDesc1.WhitePoint[0] << L", " << outputDesc1.WhitePoint[1] << std::endl;
                        std::wcout << L"Minimum luminance: " << outputDesc1.MinLuminance << L" nit" << std::endl;
                        std::wcout << L"Maximum luminance: " << outputDesc1.MaxLuminance << L" nit" << std::endl;
                        std::wcout << L"Maximum average full frame luminance: " << outputDesc1.MaxFullFrameLuminance << L" nit" << std::endl;
                        if (getSdrWhiteLevelInNit(outputDesc1, sdrWhiteLevel)) {
                            std::wcout << L"SDR white level: " << sdrWhiteLevel << L" nit" << std::endl;
                        }
                    }
                }
            }
        }
    }
    std::wcout << kColorBlue << L"##############################" << kColorDefault << std::endl;
    std::wcout << kColorMagenta << L"Press the <ENTER> key to exit ..." << kColorDefault << std::endl;
    std::getchar();
    return EXIT_SUCCESS;
}
