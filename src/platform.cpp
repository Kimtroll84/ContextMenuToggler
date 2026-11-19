#include "platform.h"

#include <shellapi.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <cwchar>
#include <string>

namespace {

template<typename T>
struct ComPtr {
    T* value = nullptr;
    ~ComPtr() { if (value) value->Release(); }
};

struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
};

DWORD ShellPid() {
    DWORD pid = 0;
    const HWND shell = GetShellWindow();
    if (shell) GetWindowThreadProcessId(shell, &pid);
    return pid;
}

bool ReadTokenUser(HANDLE process, BYTE* buffer, DWORD size) {
    Handle token;
    DWORD required = 0;
    return OpenProcessToken(process, TOKEN_QUERY, &token.value) &&
           GetTokenInformation(token.value, TokenUser, buffer, size, &required);
}

bool SameUser(HANDLE target) {
    alignas(TOKEN_USER) BYTE current[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE]{};
    alignas(TOKEN_USER) BYTE other[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE]{};
    if (!ReadTokenUser(GetCurrentProcess(), current, sizeof(current)) ||
        !ReadTokenUser(target, other, sizeof(other))) return false;
    return EqualSid(reinterpret_cast<TOKEN_USER*>(current)->User.Sid,
                    reinterpret_cast<TOKEN_USER*>(other)->User.Sid) != FALSE;
}

bool WaitForShell(DWORD oldPid, DWORD timeout) {
    const ULONGLONG start = GetTickCount64();
    do {
        const DWORD pid = ShellPid();
        if (pid != 0 && pid != oldPid) return true;
        Sleep(100);
    } while (GetTickCount64() - start < timeout);
    return false;
}

}  // namespace

HBITMAP LoadPortrait(HINSTANCE instance, int resourceId, UINT targetSize) {
    const HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource) return nullptr;
    const HGLOBAL image = LoadResource(instance, resource);
    const auto* bytes = static_cast<const BYTE*>(LockResource(image));
    const DWORD byteCount = SizeofResource(instance, resource);
    if (!bytes || !byteCount) return nullptr;

    ComPtr<IStream> stream{SHCreateMemStream(bytes, byteCount)};
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (!stream.value ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory.value))) ||
        FAILED(factory.value->CreateDecoderFromStream(stream.value, nullptr,
                   WICDecodeMetadataCacheOnLoad, &decoder.value)) ||
        FAILED(decoder.value->GetFrame(0, &frame.value)) ||
        FAILED(factory.value->CreateFormatConverter(&converter.value)) ||
        FAILED(converter.value->Initialize(frame.value, GUID_WICPixelFormat32bppPBGRA,
                   WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        return nullptr;
    }

    UINT width = 0;
    UINT height = 0;
    if (FAILED(converter.value->GetSize(&width, &height)) ||
        !width || !height || width > 512 || height > 512 ||
        !targetSize || targetSize > 1024) return nullptr;

    ComPtr<IWICBitmapScaler> scaler;
    IWICBitmapSource* pixelsSource = converter.value;
    if (width != targetSize || height != targetSize) {
        // Scale premultiplied pixels before GDI compositing, including transparent edges.
        const auto filter = targetSize < width ? WICBitmapInterpolationModeFant
                                                : WICBitmapInterpolationModeCubic;
        if (FAILED(factory.value->CreateBitmapScaler(&scaler.value)) ||
            FAILED(scaler.value->Initialize(converter.value, targetSize, targetSize, filter))) {
            return nullptr;
        }
        pixelsSource = scaler.value;
        width = height = targetSize;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    const HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap) return nullptr;
    if (FAILED(pixelsSource->CopyPixels(nullptr, width * 4, width * height * 4,
                                           static_cast<BYTE*>(pixels)))) {
        DeleteObject(bitmap);
        return nullptr;
    }
    return bitmap;
}

DWORD RestartExplorer() {
    const DWORD pid = ShellPid();
    if (!pid) return ERROR_NOT_FOUND;
    DWORD shellSession = 0;
    DWORD appSession = 0;
    if (!ProcessIdToSessionId(pid, &shellSession) ||
        !ProcessIdToSessionId(GetCurrentProcessId(), &appSession)) return GetLastError();
    if (shellSession != appSession) return ERROR_ACCESS_DENIED;

    Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE,
                               FALSE, pid)};
    if (!process.value) return GetLastError();

    wchar_t windows[MAX_PATH]{};
    const UINT length = GetWindowsDirectoryW(windows, ARRAYSIZE(windows));
    if (!length || length >= ARRAYSIZE(windows)) return ERROR_PATH_NOT_FOUND;
    const std::wstring executable = std::wstring(windows) + L"\\explorer.exe";
    wchar_t actual[MAX_PATH]{};
    DWORD actualSize = ARRAYSIZE(actual);
    if (!QueryFullProcessImageNameW(process.value, 0, actual, &actualSize)) return GetLastError();
    if (_wcsicmp(actual, executable.c_str()) != 0 || !SameUser(process.value) || ShellPid() != pid) {
        return ERROR_ACCESS_DENIED;
    }

    if (!TerminateProcess(process.value, 0)) return GetLastError();
    const DWORD stopped = WaitForSingleObject(process.value, 4000);
    if (stopped != WAIT_OBJECT_0) return stopped == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT;
    if (WaitForShell(pid, 3000)) return ERROR_SUCCESS;

    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_FLAG_NO_UI;
    launch.lpFile = executable.c_str();
    launch.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&launch)) return GetLastError();
    return WaitForShell(pid, 8000) ? ERROR_SUCCESS : ERROR_TIMEOUT;
}
