// Reproducible viewer timings. Build this same harness against current and v1.0.0 sources.
#ifndef IC_APP_SOURCE
#define IC_APP_SOURCE "../src/main.cpp"
#endif
#define wWinMain ProductionEntry
#include IC_APP_SOURCE
#undef wWinMain
#include <iostream>
#include <chrono>
using Clock = std::chrono::steady_clock;
static double milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
static void pump(int ms) {
    auto end = GetTickCount64() + ms;
    do {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message != WM_QUIT) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        Sleep(1);
    } while (GetTickCount64() < end);
}
static void ready(App &app) {
    auto deadline = GetTickCount64() + 30000;
    do {
        pump(2);
        bool loaded = app.panes.size() == 3;
        for (auto &pane : app.panes)
            loaded &= pane.image.w > 0 && app.currentValid() &&
                      pane.path == app.model.rows[app.model.position].paths[pane.folder];
        if (loaded)
            return;
    } while (GetTickCount64() < deadline);
    throw std::runtime_error("Preview readiness timeout");
}
static std::vector<std::wstring> fixtures(const fs::path &root) {
    fs::create_directories(root);
    auto source = root / L"source.jpg";
    if (!fs::exists(source)) {
        Bitmap image(4096, 3072, PixelFormat24bppRGB);
        {
            Graphics g(&image);
            LinearGradientBrush brush(Point(0, 0), Point(4096, 3072), Color(255, 22, 84, 170),
                                      Color(255, 240, 150, 60));
            g.FillRectangle(&brush, 0, 0, 4096, 3072);
            Pen pen(Color(255, 240, 240, 240), 2);
            for (int x = 0; x < 4096; x += 17)
                g.DrawLine(&pen, x, 0, 4096 - x, 3071);
        }
        UINT count = 0, size = 0;
        GetImageEncodersSize(&count, &size);
        std::vector<BYTE> data(size);
        auto *enc = reinterpret_cast<ImageCodecInfo *>(data.data());
        GetImageEncoders(count, size, enc);
        bool saved = false;
        for (UINT i = 0; i < count; ++i)
            if (std::wstring(enc[i].MimeType) == L"image/jpeg")
                saved = image.Save(source.c_str(), &enc[i].Clsid, nullptr) == Ok;
        if (!saved)
            throw std::runtime_error("Cannot create JPEG fixture");
    }
    std::vector<std::wstring> paths;
    for (int f = 0; f < 3; ++f) {
        auto dir = root / (L"folder-" + std::to_wstring(f));
        fs::create_directories(dir);
        paths.push_back(dir.wstring());
        for (int i = 0; i < 1000; ++i) {
            wchar_t name[40];
            swprintf(name, 40, L"photo%04d.jpg", i);
            auto path = dir / name;
            if (!fs::exists(path) && !CreateHardLinkW(path.c_str(), source.c_str(), nullptr) &&
                !CopyFileW(source.c_str(), path.c_str(), TRUE))
                throw std::runtime_error("Cannot create directory fixture");
        }
    }
    return paths;
}
int wmain(int argc, wchar_t **argv) {
    if (argc != 3)
        return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ULONG_PTR token = 0;
    GdiplusStartupInput input;
    if (GdiplusStartup(&token, &input, nullptr) != Ok)
        return 2;
    int result = 0;
    try {
        auto paths = fixtures(fs::absolute(argv[1]));
        App app;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"ImageCompareBenchmark";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        HWND w = CreateWindowExW(0, wc.lpszClassName, L"ImageCompare performance benchmark",
                                 WS_OVERLAPPEDWINDOW, 0, 0, 1400, 850, nullptr, nullptr, wc.hInstance, &app);
        if (!w)
            throw std::runtime_error("Cannot create benchmark window");
        ShowWindow(w, SW_SHOW);
        pump(50);
        auto start = Clock::now();
        app.addFolders(paths);
        double dispatch = milliseconds(start);
        ready(app);
        double first = milliseconds(start);
        HDC dc = GetDC(w);
        auto draw = [&](double zoom) {
            app.zoom = zoom;
            app.drag = 2;
            app.paint(dc);
            std::vector<double> times;
            for (int i = 0; i < 5; ++i) {
                app.panX = (i % 2) * .05;
                auto t = Clock::now();
                app.paint(dc);
                GdiFlush();
                times.push_back(milliseconds(t));
            }
            std::sort(times.begin(), times.end());
            return times[times.size() / 2];
        };
        double fit = draw(1), z4 = draw(4), z12 = draw(12);
        app.drag = 0;
        app.resetView();
        ReleaseDC(w, dc);
        pump(1000);
        start = Clock::now();
        app.nav(1);
        ready(app);
        double next = milliseconds(start);
        std::ofstream output(fs::path(argv[2]), std::ios::binary);
        output
            << std::fixed << std::setprecision(3)
            << "{\n  \"files_per_folder\": 1000,\n  \"folder_count\": 3,\n  \"source_pixels\": [4096,3072],\n"
            << "  \"add_dispatch_ms\": " << dispatch << ",\n  \"first_group_ready_ms\": " << first
            << ",\n  \"fit_paint_median_ms\": " << fit << ",\n  \"zoom4_paint_median_ms\": " << z4
            << ",\n  \"zoom12_paint_median_ms\": " << z12
            << ",\n  \"prefetched_next_group_ready_ms\": " << next << "\n}\n";
        output.close();
        std::cout << "BENCHMARK: add dispatch " << dispatch << " ms, first group " << first
                  << " ms, fit/4x/12x paint " << fit << "/" << z4 << "/" << z12 << " ms, next group " << next
                  << " ms" << std::endl;
        app.dirty = false;
        DestroyWindow(w);
        pump(5);
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        result = 1;
    }
    GdiplusShutdown(token);
    if (SUCCEEDED(com))
        CoUninitialize();
    return result;
}
