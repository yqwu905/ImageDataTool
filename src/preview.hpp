#pragma once
#include <wincodec.h>
#include "image_io.hpp"

namespace platform {
template <class T> struct Com {
    T *p = nullptr;
    Com() = default;
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;
    ~Com() {
        if (p)
            p->Release();
    }
    T **put() { return &p; }
    T *operator->() const { return p; }
};
struct ComApartment {
    HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComApartment() {
        if (SUCCEEDED(status))
            CoUninitialize();
    }
};
struct Preview {
    HBITMAP bitmap = nullptr;
    int width = 0, height = 0;
    void *pixels = nullptr;
    Preview(int w, int h) : width(w), height(h) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap || !pixels)
            throw std::runtime_error("Cannot allocate preview");
    }
    ~Preview() {
        if (bitmap)
            DeleteObject(bitmap);
    }
    Preview(const Preview &) = delete;
    Preview &operator=(const Preview &) = delete;
    size_t bytes() const { return size_t(width) * height * 4; }
};
class PreviewDecoder {
    Com<IWICImagingFactory> factory;
    static void check(HRESULT status) {
        if (FAILED(status))
            throw std::runtime_error("Cannot decode preview");
    }

  public:
    std::shared_ptr<Preview> load(const std::string &path, int edge) {
        if (!factory.p)
            check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(factory.put())));
        Com<IWICBitmapDecoder> decoder;
        check(factory->CreateDecoderFromFilename(wide(path).c_str(), nullptr, GENERIC_READ,
                                                 WICDecodeMetadataCacheOnDemand, decoder.put()));
        Com<IWICBitmapFrameDecode> frame;
        check(decoder->GetFrame(0, frame.put()));
        UINT w = 0, h = 0;
        check(frame->GetSize(&w, &h));
        if (!w || !h || uint64_t(w) * h > 120000000)
            throw std::runtime_error("Image exceeds 120 MP limit");
        unsigned orientation = 1;
        Com<IWICMetadataQueryReader> metadata;
        if (SUCCEEDED(frame->GetMetadataQueryReader(metadata.put()))) {
            for (auto key : {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}"}) {
                PROPVARIANT value{};
                if (SUCCEEDED(metadata->GetMetadataByName(key, &value))) {
                    if (value.vt == VT_UI2)
                        orientation = value.uiVal;
                    else if (value.vt == VT_UI4)
                        orientation = value.ulVal;
                    PropVariantClear(&value);
                    break;
                }
                PropVariantClear(&value);
            }
        }
        double scale = std::min(1., double(edge) / std::max(w, h));
        UINT dw = std::max(1u, UINT(std::round(w * scale))), dh = std::max(1u, UINT(std::round(h * scale)));
        IWICBitmapSource *source = frame.p;
        Com<IWICBitmapScaler> scaler;
        if (dw != w || dh != h) {
            check(factory->CreateBitmapScaler(scaler.put()));
            check(scaler->Initialize(source, dw, dh, WICBitmapInterpolationModeLinear));
            source = scaler.p;
        }
        static const WICBitmapTransformOptions transforms[] = {
            WICBitmapTransformRotate0,   WICBitmapTransformRotate0,      WICBitmapTransformFlipHorizontal,
            WICBitmapTransformRotate180, WICBitmapTransformFlipVertical, WICBitmapTransformRotate90,
            WICBitmapTransformRotate90,  WICBitmapTransformRotate270,    WICBitmapTransformRotate270};
        Com<IWICBitmapFlipRotator> rotated;
        if (orientation >= 2 && orientation <= 8) {
            check(factory->CreateBitmapFlipRotator(rotated.put()));
            check(rotated->Initialize(source, transforms[orientation]));
            source = rotated.p;
        }
        // Keep rotation and the following horizontal mirror in separate stages. Combined
        // WIC flags use a different composition order from GDI+ on native Windows.
        Com<IWICBitmapFlipRotator> mirrored;
        if (orientation == 5 || orientation == 7) {
            check(factory->CreateBitmapFlipRotator(mirrored.put()));
            check(mirrored->Initialize(source, WICBitmapTransformFlipHorizontal));
            source = mirrored.p;
        }
        check(source->GetSize(&dw, &dh));
        Com<IWICFormatConverter> converted;
        check(factory->CreateFormatConverter(converted.put()));
        check(converted->Initialize(source, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                    0, WICBitmapPaletteTypeCustom));
        auto result = std::make_shared<Preview>(int(dw), int(dh));
        check(converted->CopyPixels(nullptr, dw * 4, UINT(result->bytes()),
                                    static_cast<BYTE *>(result->pixels)));
        // Flatten only the disposable preview onto the viewer background. Exports keep original alpha.
        auto *pixels = static_cast<BYTE *>(result->pixels);
        for (size_t i = 0; i < size_t(dw) * dh; ++i, pixels += 4) {
            if (pixels[3] == 255)
                continue;
            unsigned inverse = 255 - pixels[3];
            pixels[0] = BYTE(pixels[0] + (21 * inverse + 127) / 255);
            pixels[1] = BYTE(pixels[1] + (14 * inverse + 127) / 255);
            pixels[2] = BYTE(pixels[2] + (10 * inverse + 127) / 255);
            pixels[3] = 255;
        }
        return result;
    }
};
} // namespace platform
