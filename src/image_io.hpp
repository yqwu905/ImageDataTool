#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <atomic>
#include "core.hpp"
namespace fs = std::filesystem;
namespace platform {
inline std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), w.data(), n);
    return w;
}
inline std::string utf8(const std::wstring &w) {
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), int(w.size()), nullptr, 0, nullptr,
                                nullptr);
    if (!n)
        throw std::runtime_error("Invalid Unicode");
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w.data(), int(w.size()), s.data(), n, nullptr,
                        nullptr);
    return s;
}
inline std::wstring folded(std::wstring w) {
    if (!w.empty())
        LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, w.data(), int(w.size()), w.data(),
                      int(w.size()), nullptr, nullptr, 0);
    return w;
}
inline std::string pathKey(const fs::path &p) {
    return utf8(folded(fs::absolute(p).lexically_normal().wstring()));
}
inline std::vector<ic::File> scan(const std::string &folder, const std::atomic<bool> *cancelled = nullptr) {
    std::vector<ic::File> files;
    auto base = fs::absolute(fs::path(wide(folder))).lexically_normal();
    auto prefix = utf8(base.wstring());
    if (!prefix.empty() && prefix.back() != '\\')
        prefix += '\\';
    auto pattern = (base / L"*").wstring();
    WIN32_FIND_DATAW data{};
    HANDLE search = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                     FIND_FIRST_EX_LARGE_FETCH);
    if (search == INVALID_HANDLE_VALUE && GetLastError() == ERROR_INVALID_PARAMETER)
        search = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (search == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND)
            return files;
        throw std::runtime_error("Cannot read folder");
    }
    DWORD error = ERROR_NO_MORE_FILES;
    try {
        do {
            if (cancelled && cancelled->load())
                break;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            std::wstring name = data.cFileName;
            auto dot = name.find_last_of(L'.');
            if (dot == std::wstring::npos)
                continue;
            auto ext = folded(name.substr(dot));
            if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp" || ext == L".gif" ||
                ext == L".tif" || ext == L".tiff") {
                auto filename = utf8(name);
                files.push_back({prefix + filename, filename, utf8(folded(name))});
            }
        } while (FindNextFileW(search, &data));
        error = GetLastError();
    } catch (...) {
        FindClose(search);
        throw;
    }
    FindClose(search);
    if (!(cancelled && cancelled->load()) && error != ERROR_NO_MORE_FILES)
        throw std::runtime_error("Folder enumeration failed");
    return files;
}
inline void orient(Gdiplus::Image &image) {
    const PROPID id = 0x0112;
    UINT n = image.GetPropertyItemSize(id);
    if (n < sizeof(Gdiplus::PropertyItem) || n > 65536)
        return;
    std::vector<unsigned char> bytes(n);
    auto *prop = reinterpret_cast<Gdiplus::PropertyItem *>(bytes.data());
    if (image.GetPropertyItem(id, n, prop) != Gdiplus::Ok || prop->length < 2 || !prop->value)
        return;
    auto v = *static_cast<unsigned short *>(prop->value);
    using namespace Gdiplus;
    switch (v) {
    case 2:
        image.RotateFlip(RotateNoneFlipX);
        break;
    case 3:
        image.RotateFlip(Rotate180FlipNone);
        break;
    case 4:
        image.RotateFlip(Rotate180FlipX);
        break;
    case 5:
        image.RotateFlip(Rotate90FlipX);
        break;
    case 6:
        image.RotateFlip(Rotate90FlipNone);
        break;
    case 7:
        image.RotateFlip(Rotate270FlipX);
        break;
    case 8:
        image.RotateFlip(Rotate270FlipNone);
        break;
    default:
        break;
    }
}
inline std::shared_ptr<Gdiplus::Bitmap> load(const std::string &path, int maxEdge = 2560) {
    using namespace Gdiplus;
    std::unique_ptr<Bitmap> original(Bitmap::FromFile(wide(path).c_str()));
    if (!original || original->GetLastStatus() != Ok)
        throw std::runtime_error("Cannot decode image");
    UINT w = original->GetWidth(), h = original->GetHeight();
    if (!w || !h || uint64_t(w) * h > 120000000)
        throw std::runtime_error("Image exceeds 120 MP limit");
    orient(*original);
    w = original->GetWidth();
    h = original->GetHeight();
    double scale = maxEdge ? std::min(1., double(maxEdge) / std::max(w, h)) : 1.;
    int dw = std::max(1, int(std::round(w * scale))), dh = std::max(1, int(std::round(h * scale)));
    auto result = std::make_shared<Bitmap>(dw, dh, PixelFormat32bppARGB);
    if (result->GetLastStatus() != Ok)
        throw std::runtime_error("Cannot allocate image");
    if (scale == 1.) {
        // DrawImage can round semi-transparent colors on native Windows, even at 1:1.
        // Copy straight ARGB scanlines instead; the new bitmap also drops source metadata.
        Gdiplus::Rect bounds(0, 0, dw, dh);
        BitmapData source{}, target{};
        if (original->LockBits(&bounds, ImageLockModeRead, PixelFormat32bppARGB, &source) != Ok)
            throw std::runtime_error("Cannot read image pixels");
        if (result->LockBits(&bounds, ImageLockModeWrite, PixelFormat32bppARGB, &target) != Ok) {
            original->UnlockBits(&source);
            throw std::runtime_error("Cannot allocate image pixels");
        }
        for (int y = 0; y < dh; ++y) {
            auto *from = static_cast<const BYTE *>(source.Scan0) + ptrdiff_t(y) * source.Stride;
            auto *to = static_cast<BYTE *>(target.Scan0) + ptrdiff_t(y) * target.Stride;
            std::memcpy(to, from, size_t(dw) * 4);
        }
        auto readStatus = original->UnlockBits(&source);
        auto writeStatus = result->UnlockBits(&target);
        if (readStatus != Ok || writeStatus != Ok)
            throw std::runtime_error("Cannot finish image pixel copy");
        return result;
    }
    Graphics g(result.get());
    g.Clear(Color(0, 0, 0, 0));
    g.SetCompositingMode(CompositingModeSourceCopy);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    if (g.DrawImage(original.get(), Gdiplus::Rect(0, 0, dw, dh), 0, 0, w, h, UnitPixel) != Ok)
        throw std::runtime_error("Cannot render image");
    return result;
}
inline CLSID pngEncoder() {
    UINT count = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    std::vector<unsigned char> data(size);
    auto *list = reinterpret_cast<Gdiplus::ImageCodecInfo *>(data.data());
    if (Gdiplus::GetImageEncoders(count, size, list) != Gdiplus::Ok)
        throw std::runtime_error("PNG encoder unavailable");
    for (UINT i = 0; i < count; ++i)
        if (std::wstring(list[i].MimeType) == L"image/png")
            return list[i].Clsid;
    throw std::runtime_error("PNG encoder unavailable");
}
inline void exportPng(const std::string &source, const fs::path &dest, const ic::Edit &edit) {
    auto image = load(source, 0);
    if (!edit.rects.empty()) {
        for (auto &r : edit.rects)
            if (!ic::valid(r))
                throw std::runtime_error("Invalid mask rectangle");
        int width = int(image->GetWidth()), height = int(image->GetHeight());
        Gdiplus::Rect bounds(0, 0, width, height);
        Gdiplus::BitmapData pixels{};
        if (image->LockBits(&bounds, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
                            PixelFormat32bppARGB, &pixels) != Gdiplus::Ok)
            throw std::runtime_error("Cannot edit image pixels");
        for (auto &r : edit.rects) {
            int x = std::clamp(int(std::floor(r.x * width)), 0, width);
            int y = std::clamp(int(std::floor(r.y * height)), 0, height);
            int right = std::clamp(int(std::ceil((r.x + r.w) * width)), 0, width);
            int bottom = std::clamp(int(std::ceil((r.y + r.h) * height)), 0, height);
            for (int row = y; row < bottom; ++row) {
                auto *scan = static_cast<BYTE *>(pixels.Scan0) + ptrdiff_t(row) * pixels.Stride;
                std::fill_n(reinterpret_cast<uint32_t *>(scan) + x, right - x, uint32_t(0xff000000));
            }
        }
        if (image->UnlockBits(&pixels) != Gdiplus::Ok)
            throw std::runtime_error("Cannot finish image pixel edit");
    }
    auto clsid = pngEncoder();
    if (image->Save(dest.c_str(), &clsid, nullptr) != Gdiplus::Ok)
        throw std::runtime_error("PNG export failed");
}
inline void atomicWrite(const fs::path &dest, const std::string &bytes) {
    auto temp = dest;
    temp += L".tmp-" + std::to_wstring(GetCurrentProcessId());
    HANDLE h =
        CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create temporary file");
    DWORD written = 0;
    bool ok = WriteFile(h, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size() &&
              FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(temp.c_str());
        throw std::runtime_error("Cannot write project");
    }
    if (!MoveFileExW(temp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        throw std::runtime_error("Cannot replace project");
    }
}
inline std::string readProject(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot open project");
    in.seekg(0, std::ios::end);
    auto n = in.tellg();
    if (n < 0 || n > 64 * 1024 * 1024)
        throw std::runtime_error("Project too large");
    in.seekg(0);
    std::string s(size_t(n), '\0');
    if (!in.read(s.data(), n))
        throw std::runtime_error("Cannot read project");
    return s;
}
} // namespace platform
