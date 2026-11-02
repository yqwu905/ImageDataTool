// Exercises the production Win32 UI, GDI+ decoder/exporter and atomic project I/O.
#define wWinMain ProductionEntry
#include "../src/main.cpp"
#undef wWinMain
#include <iostream>
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c))                                                                                            \
            throw std::runtime_error(std::string("Check failed: ") + #c + " at " +                           \
                                     std::to_string(__LINE__));                                              \
    } while (0)
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
        Sleep(10);
    } while (GetTickCount64() < end);
}
static void solid(const fs::path &p, int w, int h, Color c) {
    Bitmap b(w, h, PixelFormat32bppARGB);
    {
        Graphics g(&b);
        g.Clear(c);
    }
    auto enc = platform::pngEncoder();
    CHECK(b.Save(p.c_str(), &enc, nullptr) == Ok);
}
int wmain(int argc, wchar_t **argv) {
    if (argc != 2 && argc != 3)
        return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ULONG_PTR token;
    GdiplusStartupInput input;
    CHECK(GdiplusStartup(&token, &input, nullptr) == Ok);
    int result = 0;
    try {
        fs::path root = fs::absolute(argv[1]);
        fs::create_directories(root / L"参考图");
        fs::create_directories(root / L"处理图");
        solid(root / L"参考图/图1.png", 100, 80, Color(255, 240, 120, 60));
        solid(root / L"处理图/图1.PNG", 200, 160, Color(255, 80, 160, 240));
        solid(root / L"参考图/图2.png", 80, 100, Color(255, 80, 240, 160));
        solid(root / L"处理图/图10.png", 160, 200, Color(255, 160, 80, 240));
        std::ofstream(root / L"参考图/broken.png") << "invalid image";
        CHECK(platform::scan(utf8((root / L"参考图").wstring())).size() == 3);
        bool rejected = false;
        try {
            platform::load(utf8((root / L"参考图/broken.png").wstring()));
        } catch (...) {
            rejected = true;
        }
        CHECK(rejected);
        auto path = utf8((root / L"参考图/图1.png").wstring());
        ic::Edit e;
        e.rects.push_back({.2, .25, .3, .25});
        platform::exportPng(path, root / L"export.png", e);
        Bitmap exported((root / L"export.png").c_str());
        CHECK(exported.GetWidth() == 100 && exported.GetHeight() == 80);
        Color pixel;
        CHECK(exported.GetPixel(22, 22, &pixel) == Ok);
        CHECK(pixel.GetValue() == Color(255, 0, 0, 0).GetValue());
        exported.GetPixel(19, 22, &pixel);
        CHECK(pixel.GetR() == 240 && pixel.GetG() == 120 && pixel.GetB() == 60);
        exported.GetPixel(50, 22, &pixel);
        CHECK(pixel.GetR() == 240);
        exported.GetPixel(22, 40, &pixel);
        CHECK(pixel.GetR() == 240);
        platform::exportPng(utf8((root / L"处理图/图1.PNG").wstring()), root / L"export2.png", e);
        Bitmap second((root / L"export2.png").c_str());
        second.GetPixel(45, 45, &pixel);
        CHECK(pixel.GetValue() == Color(255, 0, 0, 0).GetValue());
        second.GetPixel(39, 45, &pixel);
        CHECK(pixel.GetB() == 240);
        CHECK(second.GetWidth() == 200 && second.GetHeight() == 160);
        solid(root / L"alpha.png", 30, 20, Color(0, 0, 0, 0));
        platform::exportPng(utf8((root / L"alpha.png").wstring()), root / L"alpha-masked.png", e);
        Bitmap alpha((root / L"alpha-masked.png").c_str());
        alpha.GetPixel(8, 6, &pixel);
        CHECK(pixel.GetA() == 255 && pixel.GetR() == 0);
        alpha.GetPixel(0, 0, &pixel);
        CHECK(pixel.GetA() == 0);
        {
            Bitmap pattern(17, 13, PixelFormat32bppARGB);
            for (int y = 0; y < 13; ++y)
                for (int x = 0; x < 17; ++x)
                    CHECK(pattern.SetPixel(
                              x, y,
                              Color(BYTE(20 + x * 13), BYTE(x * 11), BYTE(y * 17), BYTE((x + y) * 7))) == Ok);
            auto enc = platform::pngEncoder();
            CHECK(pattern.Save((root / L"pattern.png").c_str(), &enc, nullptr) == Ok);
            platform::exportPng(utf8((root / L"pattern.png").wstring()), root / L"pattern-export.png", {});
            Bitmap source((root / L"pattern.png").c_str()), copy((root / L"pattern-export.png").c_str());
            for (int y = 0; y < 13; ++y)
                for (int x = 0; x < 17; ++x) {
                    Color a, b;
                    source.GetPixel(x, y, &a);
                    copy.GetPixel(x, y, &b);
                    CHECK(a.GetValue() == b.GetValue());
                }
        }
        {
            platform::exportPng(utf8((root / L"pattern.png").wstring()), root / L"pattern-masked.png", e);
            Bitmap source((root / L"pattern.png").c_str()), masked((root / L"pattern-masked.png").c_str());
            for (int y = 0; y < 13; ++y)
                for (int x = 0; x < 17; ++x) {
                    Color before, after;
                    source.GetPixel(x, y, &before);
                    masked.GetPixel(x, y, &after);
                    const bool covered = x >= 3 && x < 9 && y >= 3 && y < 7;
                    CHECK(after.GetValue() == (covered ? Color(255, 0, 0, 0).GetValue() : before.GetValue()));
                }
        }
        std::cout << "PASS: native image export and pixel preservation" << std::endl;
        if (argc == 3) {
            platform::ComApartment apartment;
            platform::PreviewDecoder decoder;
            for (int orientation = 1; orientation <= 8; ++orientation) {
                auto file = fs::path(argv[2]) / (L"orientation-" + std::to_wstring(orientation) + L".jpg");
                auto original = platform::load(utf8(file.wstring()), 0);
                auto preview = decoder.load(utf8(file.wstring()), 2048);
                CHECK(preview->width == int(original->GetWidth()) &&
                      preview->height == int(original->GetHeight()));
                for (int y = 2; y < preview->height - 2; y += 7)
                    for (int x = 2; x < preview->width - 2; x += 7) {
                        Color c;
                        original->GetPixel(x, y, &c);
                        auto pixel = static_cast<BYTE *>(preview->pixels) + (y * preview->width + x) * 4;
                        CHECK(std::abs(int(pixel[2]) - c.GetR()) <= 2 &&
                              std::abs(int(pixel[1]) - c.GetG()) <= 2 &&
                              std::abs(int(pixel[0]) - c.GetB()) <= 2);
                    }
            }
        }
        {
            App app;
            app.settingsPath = (root / L"settings.icmp").wstring();
            app.model.categories.push_back("复核");
            app.persistSettings();
            {
                App restoredSettings;
                restoredSettings.settingsPath = app.settingsPath;
                restoredSettings.loadSettings();
                CHECK(restoredSettings.model.categories == app.model.categories);
            }
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = WndProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"ImageCompareTest";
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            CHECK(RegisterClassExW(&wc));
            HWND w =
                CreateWindowExW(0, wc.lpszClassName, L"ImageCompare integration test", WS_OVERLAPPEDWINDOW, 0,
                                0, 1400, 850, nullptr, nullptr, wc.hInstance, &app);
            CHECK(w);
            ShowWindow(w, SW_SHOW);
            app.addFolders({(root / L"参考图").wstring(), (root / L"处理图").wstring()});
            const auto scanDeadline = GetTickCount64() + 5000;
            while (app.scanning && GetTickCount64() < scanDeadline)
                pump(10);
            CHECK(!app.scanning && app.model.folders.size() == 2);
            app.command(C_MODE);
            while (app.scanning && GetTickCount64() < scanDeadline)
                pump(10);
            CHECK(!app.scanning);
            app.model.position = 1; // broken.png sorts before Chinese names; 图1 is row 1.
            app.invalidate();
            const auto loadDeadline = GetTickCount64() + 5000;
            do {
                pump(20);
                if (app.panes.size() == 2 && app.panes[0].image.w > 0 && app.panes[1].image.w > 0)
                    break;
            } while (GetTickCount64() < loadDeadline);
            CHECK(app.panes.size() == 2);
            CHECK(app.panes[0].image.w > 0 && app.panes[1].image.w > 0);
            auto image = app.panes[0].image;
            app.command(C_RECT);
            auto pack = [&](float x, float y) { return MAKELPARAM(int(x * app.scale), int(y * app.scale)); };
            SendMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON,
                         pack(image.x + image.w * .2f, image.y + image.h * .25f));
            SendMessageW(w, WM_MOUSEMOVE, MK_LBUTTON, pack(image.x + image.w * .5f, image.y + image.h * .5f));
            SendMessageW(w, WM_LBUTTONUP, 0, pack(image.x + image.w * .5f, image.y + image.h * .5f));
            auto targets = app.currentPaths();
            CHECK(targets.size() == 2 && !targets[0].empty() && !targets[1].empty());
            CHECK(app.model.edit(targets[0]).rects.size() == 1 &&
                  app.model.edit(targets[1]).rects.size() == 1);
            CHECK(app.model.edit(targets[0]).rects == app.model.edit(targets[1]).rects);
            SendMessageW(w, WM_COMMAND, C_UNDO, 0);
            CHECK(app.model.edit(targets[0]).rects.empty() && app.model.edit(targets[1]).rects.empty());
            SendMessageW(w, WM_COMMAND, C_REDO, 0);
            CHECK(app.model.edit(targets[1]).rects.size() == 1);
            app.selected = 0;
            app.command(C_CATEGORY_BASE);
            CHECK(app.model.edit(targets[0]).tags == std::vector<std::string>({"A"}));
            CHECK(app.model.edit(targets[1]).tags.empty());
            app.command(C_SYNC_TAGS);
            app.command(C_CATEGORY_BASE + 1);
            CHECK(app.model.edit(targets[1]).tags == std::vector<std::string>({"B"}));
            app.projectPath = (root / L"test.icmp").wstring();
            CHECK(app.save());
            auto saved = ic::deserialize(platform::readProject(app.projectPath));
            CHECK(saved.edits == app.model.edits);
            app.command(C_UNDO);
            CHECK(app.save()); // Atomic replacement of existing project.
            app.open(app.projectPath);
            CHECK(app.model.edits == ic::deserialize(platform::readProject(app.projectPath)).edits);
            const auto openDeadline = GetTickCount64() + 5000;
            while (app.scanning && GetTickCount64() < openDeadline)
                pump(10);
            CHECK(!app.scanning && app.model.folders[0].files.size() == 3);
            app.command(C_FOCUS);
            pump(100);
            CHECK(app.panes.size() == 1);
            app.command(C_CYCLE);
            pump(100);
            CHECK(app.selected == 1);
            app.command(C_FOCUS);
            pump(100);
            CHECK(app.panes.size() == 2);
            // Browse mode left drag pans the shared view, without adding edits or undo steps.
            app.command(C_RECT);
            app.command(C_FIT);
            pump(50);
            auto view = app.panes[0].view;
            PointF center{view.x + view.w / 2, view.y + view.h / 2};
            app.zoomAt(center, 4.);
            pump(50);
            float beforePanX = app.panes[0].image.x;
            auto undoCount = app.model.undoStack.size();
            auto rectCount = app.model.edit(targets[0]).rects.size();
            SendMessageW(w, WM_LBUTTONDOWN, MK_LBUTTON, pack(center.X, center.Y));
            CHECK(app.drag == 2);
            SendMessageW(w, WM_MOUSEMOVE, MK_LBUTTON, pack(center.X + 40, center.Y + 30));
            SendMessageW(w, WM_LBUTTONUP, 0, pack(center.X + 40, center.Y + 30));
            CHECK(app.drag == 0 && app.panX > 0 && app.panY > 0);
            CHECK(app.model.undoStack.size() == undoCount &&
                  app.model.edit(targets[0]).rects.size() == rectCount);
            pump(50);
            CHECK(app.panes[0].image.x > beforePanX);
            app.command(C_FIT);
            pump(180);
            CHECK(app.panX == 0 && app.panY == 0 && app.zoom == 1);
            // Repeated zoom/pan draws reuse the back buffer and decoded images.
            const auto idleDeadline = GetTickCount64() + 5000;
            while (app.loader->queued() && GetTickCount64() < idleDeadline)
                pump(10);
            CHECK(app.loader->queued() == 0);
            auto rendered = app.panes[0].image;
            COLORREF outside = GetPixel(app.backDC, int((rendered.x + rendered.w * .9f) * app.scale),
                                        int((rendered.y + rendered.h * .9f) * app.scale));
            COLORREF inside = GetPixel(app.backDC, int((rendered.x + rendered.w * .35f) * app.scale),
                                       int((rendered.y + rendered.h * .35f) * app.scale));
            CHECK(outside == RGB(240, 120, 60) && inside == RGB(0, 0, 0));
            auto allocations = app.bufferAllocations;
            auto decoded = app.loader->decoded.load();
            HDC dc = GetDC(w);
            for (int i = 0; i < 10; ++i) {
                app.zoom = 2 + i * .1;
                app.paint(dc);
            }
            ReleaseDC(w, dc);
            CHECK(app.bufferAllocations == allocations && app.loader->decoded.load() == decoded);
            auto nextPath = app.model.rows[app.model.position + 1].paths[0];
            CHECK(app.loader->contains(nextPath));
            HDC nextDC = GetDC(w);
            app.nav(1);
            app.paint(nextDC);
            ReleaseDC(w, nextDC);
            CHECK(app.panes[0].path == nextPath && app.panes[0].image.w > 0);
            app.nav(-1);
            pump(50);
            // Repeated add requests merge; replacing a project cancels stale scan completions.
            fs::create_directories(root / L"third");
            fs::create_directories(root / L"fourth");
            app.addFolders({(root / L"third").wstring()});
            app.addFolders({(root / L"fourth").wstring()});
            const auto mergedDeadline = GetTickCount64() + 5000;
            while (app.scanning && GetTickCount64() < mergedDeadline)
                pump(10);
            CHECK(!app.scanning && app.model.folders.size() == 4);
            app.dirty = false;
            app.rescan();
            app.command(C_NEW);
            pump(100);
            CHECK(!app.scanning && app.model.folders.empty() && app.model.rows.empty());
            // Close the test window without prompting to save synthetic edits.
            app.dirty = false;
            DestroyWindow(w);
            pump(10);
        }
        std::cout << "PASS: Unicode scan, bad image, opaque black pixels, proportional masks, full-size "
                     "export, alpha, real Win32 mouse events, synchronized undo/redo, tags, project "
                     "replace/reopen, single/grid switch"
                  << std::endl;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        result = 1;
    }
    GdiplusShutdown(token);
    if (SUCCEEDED(com))
        CoUninitialize();
    return result;
}
