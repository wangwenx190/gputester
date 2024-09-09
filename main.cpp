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

#include "registry.hpp"
#include <windows.h>
#include <versionhelpers.h>
#include <shellscalingapi.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <io.h>
#include <fcntl.h>
#include <cassert>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <string>

// Code copied from Unreal Engine 5.4.4:
// Engine/Source/Runtime/Core/Private/Windows/WindowsPlatformMisc.cpp
#define USE_SP_ALTPLATFORM_INFO_V1 0
#define USE_SP_ALTPLATFORM_INFO_V3 1
#define USE_SP_DRVINFO_DATA_V1 0
#define USE_SP_BACKUP_QUEUE_PARAMS_V1 0
#define USE_SP_INF_SIGNER_INFO_V1 0
#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>
#include <devpkey.h>
#undef USE_SP_ALTPLATFORM_INFO_V1
#undef USE_SP_ALTPLATFORM_INFO_V3
#undef USE_SP_DRVINFO_DATA_V1
#undef USE_SP_BACKUP_QUEUE_PARAMS_V1
#undef USE_SP_INF_SIGNER_INFO_V1
// UE 5 source code ends here.

using namespace Microsoft::WRL;
using namespace m4x1m1l14n;

using path_info_t = std::vector<DISPLAYCONFIG_PATH_INFO>;
using mode_info_t = std::vector<DISPLAYCONFIG_MODE_INFO>;

struct library_deleter_t final {
    void operator()(const HMODULE dll) const {
        if (dll) {
            ::FreeLibrary(dll);
        }
    }
};
using scoped_library_t = std::unique_ptr<std::remove_pointer_t<HMODULE>, library_deleter_t>;

#define LOAD_DLL(DLL, VAR) VAR = scoped_library_t{ ::LoadLibraryExW(L## #DLL, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) };
#define DECL_API(SYM, VAR) \
    using PFN_##SYM = decltype(&::SYM); \
    PFN_##SYM p##SYM{ nullptr };
#define LOAD_API(DLL, SYM) \
    p##SYM = reinterpret_cast<PFN_##SYM>(::GetProcAddress(DLL, #SYM)); \
    if (!p##SYM) { \
        std::wcerr << L"Failed to resolve \""## #SYM ##"\": " << getLastWin32ErrorMessage() << std::endl; \
    }

static constexpr const float kDefaultSDRWhiteLevel{ 200.f };
static constexpr const float kDefaultRefreshRate{ 60.f };
static constexpr const DXGI_FORMAT kDefaultPixelFormat{ DXGI_FORMAT_R8G8B8A8_UNORM };
static constexpr const std::wstring_view kColorDefault{ L"\x1b[0m" };
static constexpr const std::wstring_view kColorRed{ L"\x1b[1;31m" };
static constexpr const std::wstring_view kColorGreen{ L"\x1b[1;32m" };
static constexpr const std::wstring_view kColorYellow{ L"\x1b[1;33m" };
static constexpr const std::wstring_view kColorBlue{ L"\x1b[1;34m" };
static constexpr const std::wstring_view kColorMagenta{ L"\x1b[1;35m" };
static constexpr const std::wstring_view kColorCyan{ L"\x1b[1;36m" };

enum class vendor_t : std::int8_t {
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
static const std::unordered_map<std::uint64_t, vendor_t> vendorIdMap = {
    { 0x0000,  vendor_t::Unknown },
    { 0x1002,  vendor_t::AMD },
    { 0x106B,  vendor_t::Apple },
    { 0x13B5,  vendor_t::ARM },
    { 0x1AE0,  vendor_t::Google },
    { 0x1010,  vendor_t::ImgTec },
    { 0x8086,  vendor_t::Intel },
    { 0x1414,  vendor_t::Microsoft },
    { 0x10DE,  vendor_t::Nvidia },
    { 0x5143,  vendor_t::Qualcomm },
    { 0x144D,  vendor_t::Samsung },
    { 0x14E4,  vendor_t::Broadcom },
    { 0x15AD,  vendor_t::VMWare },
    { 0x1AF4,  vendor_t::VirtIO },
    { 0x10001, vendor_t::Vivante },
    { 0x10002, vendor_t::VeriSilicon },
    { 0x10003, vendor_t::Kazan },
    { 0x10004, vendor_t::CodePlay },
    { 0x10005, vendor_t::Mesa },
    { 0x10006, vendor_t::PoCL },
};

static const std::unordered_map<vendor_t, std::wstring_view> vendorNameMap = {
    { vendor_t::Unknown,     L"Unknown" },
    { vendor_t::AMD,         L"AMD" },
    { vendor_t::Apple,       L"Apple" },
    { vendor_t::ARM,         L"ARM" },
    { vendor_t::Google,      L"Google" },
    { vendor_t::ImgTec,      L"Img Tec" },
    { vendor_t::Intel,       L"Intel" },
    { vendor_t::Microsoft,   L"Microsoft" },
    { vendor_t::Nvidia,      L"Nvidia" },
    { vendor_t::Qualcomm,    L"Qualcomm" },
    { vendor_t::Samsung,     L"Samsung" },
    { vendor_t::Broadcom,    L"Broadcom" },
    { vendor_t::VMWare,      L"VMWare" },
    { vendor_t::VirtIO,      L"VirtIO" },
    { vendor_t::Vivante,     L"Vivante" },
    { vendor_t::VeriSilicon, L"VeriSilicon" },
    { vendor_t::Kazan,       L"Kazan" },
    { vendor_t::CodePlay,    L"CodePlay" },
    { vendor_t::Mesa,        L"Mesa" },
    { vendor_t::PoCL,        L"PoCL" },
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
    DLLBase(const DLLBase&) = delete;
    DLLBase& operator=(const DLLBase&) = delete;

    [[nodiscard]] inline bool isAvailable() const {
        return m_dll != nullptr;
    }

    [[nodiscard]] inline explicit operator bool() const {
        return isAvailable();
    }

    [[nodiscard]] inline HMODULE get() const {
        return m_dll ? m_dll.get() : nullptr;
    }

    [[nodiscard]] inline HMODULE operator*() const {
        return get();
    }

    [[nodiscard]] inline HMODULE operator->() const {
        return get();
    }

protected:
    scoped_library_t m_dll{ nullptr };
};
#define DLLBASE_DECL_INSTANCE(Class) \
    [[nodiscard]] static inline const Class& instance() { \
        static const Class inst; \
        return inst; \
    }

struct User32DLL final : public DLLBase {
    DLLBASE_DECL_INSTANCE(User32DLL)

    // Windows 2000
    DECL_API(GetMonitorInfoW)
    DECL_API(EnumDisplaySettingsW)
    // Windows Vista
    DECL_API(GetDisplayConfigBufferSizes)
    DECL_API(DisplayConfigGetDeviceInfo)
    // Windows 7
    DECL_API(QueryDisplayConfig)
    // Windows 10, version 1703
    DECL_API(SetProcessDpiAwarenessContext)

private:
    User32DLL() : DLLBase() {
        LOAD_DLL(user32, m_dll)
        if (m_dll) {
            LOAD_API(m_dll.get(), GetMonitorInfoW)
            LOAD_API(m_dll.get(), EnumDisplaySettingsW)
            if (::IsWindowsVistaOrGreater()) {
                LOAD_API(m_dll.get(), GetDisplayConfigBufferSizes)
                LOAD_API(m_dll.get(), DisplayConfigGetDeviceInfo)
                if (::IsWindows7OrGreater()) {
                    LOAD_API(m_dll.get(), QueryDisplayConfig)
                    if (::IsWindows10OrGreater()) {
                        LOAD_API(m_dll.get(), SetProcessDpiAwarenessContext)
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

struct Gdi32DLL final : public DLLBase {
    DLLBASE_DECL_INSTANCE(Gdi32DLL)

    DECL_API(CreateDCW)
    DECL_API(DeleteDC)
    DECL_API(GetDeviceCaps)

private:
    Gdi32DLL() : DLLBase() {
        LOAD_DLL(gdi32, m_dll)
        if (m_dll) {
            LOAD_API(m_dll.get(), CreateDCW)
            LOAD_API(m_dll.get(), DeleteDC)
            LOAD_API(m_dll.get(), GetDeviceCaps)
        } else {
            std::wcerr << L"Failed to load \"gdi32.dll\": " << getLastWin32ErrorMessage() << std::endl;
        }
    }

    ~Gdi32DLL() = default;
};
#define GDI32_AVAILABLE (Gdi32DLL::instance().isAvailable())
#define GDI32_API(Name) (Gdi32DLL::instance().p##Name)

struct DXGIDLL final : public DLLBase {
    DLLBASE_DECL_INSTANCE(DXGIDLL)

    DECL_API(CreateDXGIFactory1)

private:
    DXGIDLL() : DLLBase() {
        LOAD_DLL(dxgi, m_dll)
        if (m_dll) {
            if (::IsWindows7OrGreater()) {
                LOAD_API(m_dll.get(), CreateDXGIFactory1)
            }
        } else {
            std::wcerr << L"Failed to load \"dxgi.dll\": " << getLastWin32ErrorMessage() << std::endl;
        }
    }

    ~DXGIDLL() = default;
};
#define DXGI_AVAILABLE (DXGIDLL::instance().isAvailable())
#define DXGI_API(Name) (DXGIDLL::instance().p##Name)

struct SHCOREDLL final : public DLLBase {
    DLLBASE_DECL_INSTANCE(SHCOREDLL)

    DECL_API(GetDpiForMonitor)

private:
    SHCOREDLL() : DLLBase() {
        LOAD_DLL(shcore, m_dll)
        if (m_dll) {
            if (::IsWindows8Point1OrGreater()) {
                LOAD_API(m_dll.get(), GetDpiForMonitor)
            }
        } else {
            std::wcerr << L"Failed to load \"shcore.dll\": " << getLastWin32ErrorMessage() << std::endl;
        }
    }

    ~SHCOREDLL() = default;
};
#define SHCORE_AVAILABLE (SHCOREDLL::instance().isAvailable())
#define SHCORE_API(Name) (SHCOREDLL::instance().p##Name)

struct SETUPAPIDLL final : public DLLBase {
    DLLBASE_DECL_INSTANCE(SETUPAPIDLL)

    // Windows 2000
    DECL_API(SetupDiDestroyDeviceInfoList)
    DECL_API(SetupDiEnumDeviceInfo)
    // Windows Vista
    DECL_API(SetupDiGetClassDevsW)
    DECL_API(SetupDiGetDevicePropertyW)

private:
    SETUPAPIDLL() : DLLBase() {
        LOAD_DLL(setupapi, m_dll)
        if (m_dll) {
            LOAD_API(m_dll.get(), SetupDiDestroyDeviceInfoList)
            LOAD_API(m_dll.get(), SetupDiEnumDeviceInfo)
            if (::IsWindowsVistaOrGreater()) {
                LOAD_API(m_dll.get(), SetupDiGetClassDevsW)
                LOAD_API(m_dll.get(), SetupDiGetDevicePropertyW)
            }
        } else {
            std::wcerr << L"Failed to load \"setupapi.dll\": " << getLastWin32ErrorMessage() << std::endl;
        }
    }

    ~SETUPAPIDLL() = default;
};
#define SETUPAPI_AVAILABLE (SETUPAPIDLL::instance().isAvailable())
#define SETUPAPI_API(Name) (SETUPAPIDLL::instance().p##Name)

[[nodiscard]] static inline vendor_t vendorIdToVendor(const std::uint64_t vendorId) {
    const auto it = vendorIdMap.find(vendorId);
    if (it != vendorIdMap.end()) {
        return it->second;
    }
    return vendor_t::Unknown;
}

[[nodiscard]] static inline bool getPathInfo(const std::wstring& targetDeviceName, path_info_t& pathInfoOut) {
    if (!USER32_API(GetDisplayConfigBufferSizes) || !USER32_API(DisplayConfigGetDeviceInfo) || !USER32_API(QueryDisplayConfig)) {
        return false;
    }
    if (targetDeviceName.empty()) {
        return false;
    }
    pathInfoOut = {};
    std::uint32_t pathInfoCount{ 0 };
    std::uint32_t modeInfoCount{ 0 };
    LONG result{ ERROR_SUCCESS };
    do {
        if (USER32_API(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, &pathInfoCount, &modeInfoCount) == ERROR_SUCCESS) {
            pathInfoOut.resize(pathInfoCount);
            mode_info_t modeInfos(modeInfoCount);
            result = USER32_API(QueryDisplayConfig)(QDC_ONLY_ACTIVE_PATHS, &pathInfoCount, pathInfoOut.data(), &modeInfoCount, modeInfos.data(), nullptr);
        } else {
            std::wcerr << L"\"GetDisplayConfigBufferSizes\" failed: " << getLastWin32ErrorMessage() << std::endl;
            pathInfoOut = {};
            return false;
        }
    } while (result == ERROR_INSUFFICIENT_BUFFER);
    if (result != ERROR_SUCCESS) {
        std::wcerr << L"\"QueryDisplayConfig\" failed: " << getLastWin32ErrorMessage() << std::endl;
        pathInfoOut = {};
        return false;
    }
    auto discardThese =
            std::remove_if(pathInfoOut.begin(), pathInfoOut.end(), [&](const auto& path) -> bool {
                DISPLAYCONFIG_SOURCE_DEVICE_NAME deviceName{};
                deviceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                deviceName.header.size = sizeof(deviceName);
                deviceName.header.adapterId = path.sourceInfo.adapterId;
                deviceName.header.id = path.sourceInfo.id;
                if (USER32_API(DisplayConfigGetDeviceInfo)(&deviceName.header) == ERROR_SUCCESS) {
                    return std::wcscmp(targetDeviceName.c_str(), deviceName.viewGdiDeviceName) != 0;
                } else {
                    std::wcerr << L"\"DisplayConfigGetDeviceInfo\" failed: " << getLastWin32ErrorMessage() << std::endl;
                    return true;
                }
            });
    pathInfoOut.erase(discardThese, pathInfoOut.end());
    return !pathInfoOut.empty();
}

[[nodiscard]] static inline bool getUserFriendlyName(const path_info_t& pathInfos, std::wstring& nameOut) {
    if (!USER32_API(DisplayConfigGetDeviceInfo)) {
        return false;
    }
    for (auto&& info : std::as_const(pathInfos)) {
        DISPLAYCONFIG_TARGET_DEVICE_NAME deviceName{};
        deviceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        deviceName.header.size = sizeof(deviceName);
        deviceName.header.adapterId = info.targetInfo.adapterId;
        deviceName.header.id = info.targetInfo.id;
        if (USER32_API(DisplayConfigGetDeviceInfo)(&deviceName.header) == ERROR_SUCCESS) {
            nameOut = deviceName.monitorFriendlyDeviceName;
            return true;
        } else {
            std::wcerr << L"\"DisplayConfigGetDeviceInfo\" failed: " << getLastWin32ErrorMessage() << std::endl;
        }
    }
    return false;
}

[[nodiscard]] static inline bool getSdrWhiteLevelInNit(const path_info_t& pathInfos, float& levelOut) {
    if (!USER32_API(DisplayConfigGetDeviceInfo)) {
        return false;
    }
    for (auto&& info : std::as_const(pathInfos)) {
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
    return false;
}

[[nodiscard]] static inline bool getRefreshRate(const std::wstring& targetDeviceName, const path_info_t& pathInfos, float& rateOut) {
    for (auto&& info : std::as_const(pathInfos)) {
        const auto& rawRefreshRate = info.targetInfo.refreshRate;
        if (rawRefreshRate.Numerator > 0 && rawRefreshRate.Denominator > 0) {
            rateOut = static_cast<float>(rawRefreshRate.Numerator) / static_cast<float>(rawRefreshRate.Denominator);
            return true;
        }
    }
    if (targetDeviceName.empty()) { // The following solutions need the device name to be correct.
        return false;
    }
    if (USER32_API(EnumDisplaySettingsW)) {
        DEVMODEW devMode{};
        if (USER32_API(EnumDisplaySettingsW)(targetDeviceName.c_str(), ENUM_CURRENT_SETTINGS, &devMode)) {
            const auto& refreshRate = devMode.dmDisplayFrequency;
            if (refreshRate > 1) { // 0,1 means hardware default.
                rateOut = static_cast<float>(refreshRate);
                return true;
            }
        } else {
            std::wcerr << L"\"EnumDisplaySettingsW\" failed: " << getLastWin32ErrorMessage() << std::endl;
        }
    }
    if (GDI32_API(CreateDCW) && GDI32_API(DeleteDC) && GDI32_API(GetDeviceCaps)) {
        const HDC hdc = GDI32_API(CreateDCW)(targetDeviceName.c_str(), targetDeviceName.c_str(), nullptr, nullptr);
        if (hdc) {
            const auto refreshRate = GDI32_API(GetDeviceCaps)(hdc, VREFRESH);
            if (!GDI32_API(DeleteDC(hdc))) {
                std::wcerr << L"\"DeleteDC\" failed: " << getLastWin32ErrorMessage() << std::endl;
            }
            if (refreshRate > 1) { // 0,1 means hardware default.
                rateOut = static_cast<float>(refreshRate);
                return true;
            }
        } else {
            std::wcerr << L"\"CreateDCW\" failed: " << getLastWin32ErrorMessage() << std::endl;
        }
    }
    return false;
}

[[nodiscard]] static inline bool getDpi(const HMONITOR monitor, std::uint32_t& dpiOut) {
    assert(monitor);
    if (!monitor) {
        dpiOut = USER_DEFAULT_SCREEN_DPI;
        return false;
    }
    if (SHCORE_API(GetDpiForMonitor)) {
        std::uint32_t dpiX{ 0 };
        std::uint32_t dpiY{ 0 };
        const HRESULT hr = SHCORE_API(GetDpiForMonitor)(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        if (SUCCEEDED(hr)) {
            dpiOut = dpiX;
            return true;
        } else {
            std::wcerr << L"\"GetDpiForMonitor\" failed: " << getComErrorMessage(hr) << std::endl;
        }
    }
    dpiOut = USER_DEFAULT_SCREEN_DPI;
    return false;
}

struct DriverInfo final {
    std::wstring version{};
    std::wstring date{};
};

// Code copied and modified from Unreal Engine 5.4.4:
// Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformDriver.h
// Engine/Source/Runtime/Core/Private/Windows/WindowsPlatformMisc.cpp
[[nodiscard]] static inline bool getDriverInfo(const std::wstring& deviceName, DriverInfo& infoOut) {
    assert(!deviceName.empty());
    if (deviceName.empty()) {
        return false;
    }
    if (!SETUPAPI_API(SetupDiGetClassDevsW) || !SETUPAPI_API(SetupDiDestroyDeviceInfoList) || !SETUPAPI_API(SetupDiEnumDeviceInfo) || !SETUPAPI_API(SetupDiGetDevicePropertyW)) {
        return false;
    }
    HDEVINFO hDevInfo = SETUPAPI_API(SetupDiGetClassDevsW)(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (!hDevInfo || hDevInfo == INVALID_HANDLE_VALUE) {
        return false;
    }
    const struct DevInfoListDeleter final {
        explicit DevInfoListDeleter(HDEVINFO devInfo) : m_devInfo(devInfo) {}
        ~DevInfoListDeleter() {
            if (m_devInfo && m_devInfo != INVALID_HANDLE_VALUE) {
                SETUPAPI_API(SetupDiDestroyDeviceInfoList)(m_devInfo);
            }
        }
    private:
        HDEVINFO m_devInfo{ nullptr };
    } devInfoListDeleter{ hDevInfo };
    const auto shrinkToFit = [](std::wstring& str) {
        if (str.empty()) {
            return;
        }
        const std::size_t index = str.find(L'\0');
        if (index == std::wstring::npos) {
            return;
        }
        str.resize(index);
    };
    std::wstring registryKeyName{};
    std::wstring providerName{};
    std::wstring driverVersion{};
    std::wstring driverDate{};
    {
        bool found{ false };
        std::wstring buffer(512, L'\0');
        ULONG dataType{ 0 };
        SP_DEVINFO_DATA deviceInfoData{};
        deviceInfoData.cbSize = sizeof(deviceInfoData);
        for (DWORD index = 0; SETUPAPI_API(SetupDiEnumDeviceInfo)(hDevInfo, index, &deviceInfoData); ++index) {
            if (!SETUPAPI_API(SetupDiGetDevicePropertyW)(hDevInfo, &deviceInfoData, &DEVPKEY_Device_DriverDesc, &dataType, reinterpret_cast<PBYTE>(buffer.data()), buffer.size(), nullptr, 0)) {
                ZeroMemory(buffer.data(), buffer.size());
                continue;
            }
            if (buffer.find(deviceName) == std::wstring::npos) {
                ZeroMemory(buffer.data(), buffer.size());
                continue;
            }
            ZeroMemory(buffer.data(), buffer.size());
            found = true;
            if (SETUPAPI_API(SetupDiGetDevicePropertyW)(hDevInfo, &deviceInfoData, &DEVPKEY_Device_Driver, &dataType, reinterpret_cast<PBYTE>(buffer.data()), buffer.size(), nullptr, 0)) {
                registryKeyName = buffer;
                shrinkToFit(registryKeyName);
                ZeroMemory(buffer.data(), buffer.size());
            }
            break;
        }
        if (!found) {
            return false;
        }
        if (!SETUPAPI_API(SetupDiGetDevicePropertyW)(hDevInfo, &deviceInfoData, &DEVPKEY_Device_DriverProvider, &dataType, reinterpret_cast<PBYTE>(buffer.data()), buffer.size(), nullptr, 0)) {
            return false;
        }
        providerName = buffer;
        shrinkToFit(providerName);
        ZeroMemory(buffer.data(), buffer.size());
        if (!SETUPAPI_API(SetupDiGetDevicePropertyW)(hDevInfo, &deviceInfoData, &DEVPKEY_Device_DriverVersion, &dataType, reinterpret_cast<PBYTE>(buffer.data()), buffer.size(), nullptr, 0)) {
            return false;
        }
        driverVersion = buffer;
        shrinkToFit(driverVersion);
        ZeroMemory(buffer.data(), buffer.size());
        FILETIME fileTime{};
        if (!SETUPAPI_API(SetupDiGetDevicePropertyW)(hDevInfo, &deviceInfoData, &DEVPKEY_Device_DriverDate, &dataType, reinterpret_cast<PBYTE>(&fileTime), sizeof(fileTime), nullptr, 0)) {
            return false;
        }
        SYSTEMTIME systemTime{};
        FileTimeToSystemTime(&fileTime, &systemTime);
        driverDate = std::to_wstring(systemTime.wYear) + L'-' + std::to_wstring(systemTime.wMonth) + L'-' + std::to_wstring(systemTime.wDay);
    }
    if (providerName.find(L"NVIDIA") != std::wstring::npos) {
        // Ignore the Windows/DirectX version by taking the last digits of the internal version
        // and moving the version dot. Coincidentally, that's the user-facing string. For example:
        // 9.18.13.4788 -> 3.4788 -> 347.88
        if (driverVersion.size() >= 6) {
            std::wstring rightPart = driverVersion.substr(driverVersion.size() - 6);
            for (std::size_t index = rightPart.find(L'.'); index != std::wstring::npos; index = rightPart.find(L'.')) {
                rightPart.erase(index, 1);
            }
            rightPart.insert(3, L".");
            driverVersion = rightPart;
        }
    }
    if (providerName.find(L"Advanced Micro Devices") != std::wstring::npos) {
        // Get the AMD specific information directly from the registry.
        // AMD AGS could be used instead, but retrieving the radeon software version cannot occur after a D3D device
        // has been created, and this function could be called at any time.
        if (!registryKeyName.empty()) {
            const std::wstring keyPath = L"SYSTEM\\CurrentControlSet\\Control\\Class\\" + registryKeyName;
            try {
                if (const auto regKey = Registry::LocalMachine->Open(keyPath)) {
                    if (regKey->HasValue(L"Catalyst_Version")) {
                        const std::wstring catalystVersion = regKey->GetString(L"Catalyst_Version");
                        if (!catalystVersion.empty()) {
                            driverVersion = L"Catalyst " + catalystVersion;
                        }
                    }
                    if (regKey->HasValue(L"RadeonSoftwareEdition")) {
                        const std::wstring edition = regKey->GetString(L"RadeonSoftwareEdition");
                        if (!edition.empty()) {
                            if (regKey->HasValue(L"RadeonSoftwareVersion")) {
                                const std::wstring version = regKey->GetString(L"RadeonSoftwareVersion");
                                if (!version.empty()) {
                                    // e.g. "Crimson 15.12" or "Catalyst 14.1".
                                    driverVersion = edition + L' ' + version;
                                }
                            }
                        }
                    }
                } else {
                    std::wcerr << kColorRed << L"Failed to open registry key: HKEY_LOCAL_MACHINE\\" << keyPath << kColorDefault << std::endl;
                }
            } catch (const std::exception& ex) {
                std::wcerr << kColorRed << L"Failed to access the registry: " << ex.what() << kColorDefault << std::endl;
            }
        }
    }
    if (providerName.find(L"Intel") != std::wstring::npos) { // Usually "Intel Corporation".
        // https://www.intel.com/content/www/us/en/support/articles/000005654/graphics.html
        // Drop off the OS and DirectX version. For example:
        // 27.20.100.8935 -> 100.8935
        std::size_t index = driverVersion.find(L'.');
        if (index != std::wstring::npos) {
            index = driverVersion.find(L'.', index + 1);
            if (index != std::wstring::npos) {
                driverVersion = driverVersion.substr(index);
            }
        }
    }
    infoOut.version = driverVersion;
    infoOut.date = driverDate;
    return true;
}
// UE 5 source code ends here.

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
        std::wcerr << kColorRed << L"The critical function \"CreateDXGIFactory1\" is not available for some unknown reason, aborted." << kColorDefault << std::endl;
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
            const vendor_t vendor = vendorIdToVendor(adapterDesc1.VendorId);
            if (vendor != vendor_t::Unknown) {
                std::wcout << L" (" << vendorNameMap.at(vendor) << L')';
            }
            std::wcout << std::endl;
        }
        std::wcout << L"Device ID: 0x" << std::hex << adapterDesc1.DeviceId << std::dec << std::endl;
        std::wcout << L"Dedicated video memory: " << adapterDesc1.DedicatedVideoMemory / 1048576 << L" MiB" << std::endl;
        std::wcout << L"Dedicated system memory: " << adapterDesc1.DedicatedSystemMemory / 1048576 << L" MiB" << std::endl;
        std::wcout << L"Shared system memory: " << adapterDesc1.SharedSystemMemory / 1048576 << L" MiB" << std::endl;
        std::wcout << L"Variable refresh rate supported: " << (variableRefreshRateSupported ? L"Yes" : L"No") << std::endl;
        std::wcout << L"Software simulation (rendered by CPU): " << ((adapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? L"Yes" : L"No") << std::endl;
        {
            ComPtr<IDXGIAdapter3> adapter3;
            hr = adapter->QueryInterface(IID_PPV_ARGS(adapter3.GetAddressOf()));
            if (SUCCEEDED(hr)) {
                // Simple heuristic but without profiling it's hard to do better.
                DXGI_QUERY_VIDEO_MEMORY_INFO nonLocalVideoMemoryInfo{};
                hr = adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocalVideoMemoryInfo);
                if (SUCCEEDED(hr)) {
                    const bool isAdapterIntegrated = nonLocalVideoMemoryInfo.Budget == 0;
                    std::wcout << L"Integrated device: " << (isAdapterIntegrated ? L"Yes" : L"No") << std::endl;
                }
            }
        }
        {
            DriverInfo driverInfo{};
            if (getDriverInfo(adapterDesc1.Description, driverInfo)) {
                std::wcout << L"Driver: " << driverInfo.version << L" (" << driverInfo.date << L')' << std::endl;
            }
        }
        ComPtr<IDXGIOutput> output;
        for (std::uint32_t outputIndex = 0; adapter->EnumOutputs(outputIndex, output.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++outputIndex) {
            DXGI_OUTPUT_DESC outputDesc{};
            hr = output->GetDesc(&outputDesc);
            if (FAILED(hr)) {
                std::wcerr << L"\"IDXGIOutput::GetDesc\" failed: " << getComErrorMessage(hr) << std::endl;
                continue;
            }
            const auto& desktopRect = outputDesc.DesktopCoordinates;
            const auto width = std::abs(desktopRect.right - desktopRect.left);
            const auto height = std::abs(desktopRect.bottom - desktopRect.top);
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
            std::wcout << L"Desktop geometry: x: " << desktopRect.left << L", y: " << desktopRect.top << L", width: " << width << L", height: " << height << std::endl;
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
                            float maxRefreshRate{ kDefaultRefreshRate };
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
                                    return L"[HDR] RGB (16-235), gamma: 2.2, siting: image, primaries: BT.2020";
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
                                    return L"[HDR] YCbCr (16-235), gamma: 2.2, siting: video, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020:
                                    return L"[HDR] YCbCr (0-255), gamma: 2.2, siting: video, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
                                    return L"[HDR] RGB (0-255), gamma: 2084, siting: image, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020:
                                    return L"[HDR] YCbCr (16-235), gamma: 2084, siting: video, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
                                    return L"[HDR] RGB (16-235), gamma: 2084, siting: image, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020:
                                    return L"[HDR] YCbCr (16-235), gamma: 2.2, siting: video, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020:
                                    return L"[HDR] YCbCr (16-235), gamma: 2084, siting: video, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020:
                                    return L"[HDR] RGB (0-255), gamma: 2.2, siting: image, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020:
                                    return L"[HDR] YCbCr (16-235), gamma: HLG, siting: video, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020:
                                    return L"[HDR] YCbCr (0-255), gamma: HLG, siting: video, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709:
                                    return L"RGB (16-235), gamma: 2.4, siting: image, primaries: BT.709";
                                case DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020:
                                    return L"[HDR] RGB (16-235), gamma: 2.4, siting: image, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P709:
                                    return L"YCbCr (16-235), gamma: 2.4, siting: video, primaries: BT.709";
                                case DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P2020:
                                    return L"[HDR] YCbCr (16-235), gamma: 2.4, siting: video, primaries: BT.2020";
                                case DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_TOPLEFT_P2020:
                                    return L"[HDR] YCbCr (16-235), gamma: 2.4, siting: video, primaries: BT.2020";
                                default:
                                    return L"Unknown";
                            }
                        }();
                        std::wcout << L"Bits per color: " << outputDesc1.BitsPerColor << std::endl;
                        std::wcout << L"Color space: " << colorSpace << std::endl;
                        std::wcout << L"Red primary: " << outputDesc1.RedPrimary[0] << L", " << outputDesc1.RedPrimary[1] << std::endl;
                        std::wcout << L"Green primary: " << outputDesc1.GreenPrimary[0] << L", " << outputDesc1.GreenPrimary[1] << std::endl;
                        std::wcout << L"Blue primary: " << outputDesc1.BluePrimary[0] << L", " << outputDesc1.BluePrimary[1] << std::endl;
                        std::wcout << L"White point: " << outputDesc1.WhitePoint[0] << L", " << outputDesc1.WhitePoint[1] << std::endl;
                        std::wcout << L"Minimum luminance: " << outputDesc1.MinLuminance << L" nit" << std::endl;
                        std::wcout << L"Maximum luminance: " << outputDesc1.MaxLuminance << L" nit" << std::endl;
                        std::wcout << L"Maximum average full frame luminance: " << outputDesc1.MaxFullFrameLuminance << L" nit" << std::endl;
                    }
                }
            }
            {
                path_info_t pathInfos{};
                if (getPathInfo(outputDesc.DeviceName, pathInfos)) {
                    float sdrWhiteLevel{ kDefaultSDRWhiteLevel };
                    if (getSdrWhiteLevelInNit(pathInfos, sdrWhiteLevel)) {
                        std::wcout << L"SDR white level: " << sdrWhiteLevel << L" nit" << std::endl;
                    }
                    float refreshRate{ kDefaultRefreshRate };
                    if (getRefreshRate(outputDesc.DeviceName, pathInfos, refreshRate)) {
                        std::wcout << L"Current refresh rate: " << refreshRate << L" Hz" << std::endl;
                    }
                    std::wstring userFriendlyName{};
                    if (getUserFriendlyName(pathInfos, userFriendlyName)) {
                        std::wcout << L"Display name: " << userFriendlyName << std::endl;
                    }
                }
            }
            {
                std::uint32_t dpi{ USER_DEFAULT_SCREEN_DPI };
                if (getDpi(outputDesc.Monitor, dpi)) {
                    const auto scale = std::uint32_t(std::round(static_cast<float>(dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI) * 100.f));
                    std::wcout << L"Dots-per-inch: " << dpi << L" (" << scale << L"%)" << std::endl;
                }
            }
        }
    }
    std::wcout << kColorBlue << L"##############################" << kColorDefault << std::endl;
    std::wcout << kColorMagenta << L"Press the <ENTER> key to exit ..." << kColorDefault << std::endl;
    std::getchar();
    return EXIT_SUCCESS;
}
