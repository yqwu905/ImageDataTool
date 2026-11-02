#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include "background.hpp"

using namespace Gdiplus;
using platform::utf8;
using platform::wide;
constexpr UINT WM_IMAGE_READY = WM_APP + 1, WM_EXPORT_DONE = WM_APP + 2, WM_EXPORT_PROGRESS = WM_APP + 3,
               WM_SCAN_READY = WM_APP + 4;
struct Box {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    RectF rect() const { return {x, y, w, h}; }
};
struct Hit {
    Box box;
    int id;
};
struct InputState {
    HWND edit = nullptr;
    bool done = false, ok = false;
    std::wstring value;
};
LRESULT CALLBACK InputProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto *s = reinterpret_cast<InputState *>(GetWindowLongPtrW(w, GWLP_USERDATA));
    if (msg == WM_CREATE) {
        s = static_cast<InputState *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        SetWindowLongPtrW(w, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    if (msg == WM_COMMAND && s) {
        if (LOWORD(wp) == IDOK) {
            int n = GetWindowTextLengthW(s->edit);
            std::wstring v(n + 1, L'\0');
            GetWindowTextW(s->edit, v.data(), n + 1);
            v.resize(n);
            s->value = v;
            s->ok = true;
            s->done = true;
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL) {
            s->done = true;
            return 0;
        }
    }
    if (msg == WM_CLOSE && s) {
        s->done = true;
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}
std::wstring inputBox(HWND parent, const std::wstring &title, const std::wstring &prompt,
                      const std::wstring &initial = L"") {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW c{};
        c.lpfnWndProc = InputProc;
        c.hInstance = GetModuleHandleW(nullptr);
        c.lpszClassName = L"ICInput";
        c.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&c);
        registered = true;
    }
    RECT r;
    GetWindowRect(parent, &r);
    float scale = GetDpiForWindow(parent) / 96.f;
    auto d = [&](int n) { return int(n * scale); };
    InputState s;
    HWND w =
        CreateWindowExW(WS_EX_DLGMODALFRAME, L"ICInput", title.c_str(), WS_POPUP | WS_CAPTION | WS_SYSMENU,
                        (r.left + r.right - d(460)) / 2, (r.top + r.bottom - d(180)) / 2, d(460), d(180),
                        parent, nullptr, GetModuleHandleW(nullptr), &s);
    HFONT font = CreateFontW(-d(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                             CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    HWND label = CreateWindowW(L"STATIC", prompt.c_str(), WS_CHILD | WS_VISIBLE, d(18), d(14), d(420), d(24),
                               w, nullptr, nullptr, nullptr);
    s.edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initial.c_str(),
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, d(18), d(45), d(420), d(27),
                             w, reinterpret_cast<HMENU>(100), nullptr, nullptr);
    HWND ok = CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, d(248),
                            d(92), d(90), d(29), w, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
    HWND cancel = CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP, d(348), d(92), d(90),
                                d(29), w, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
    for (HWND c : {label, s.edit, ok, cancel})
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    EnableWindow(parent, FALSE);
    ShowWindow(w, SW_SHOW);
    SetFocus(s.edit);
    SendMessageW(s.edit, EM_SETSEL, 0, -1);
    MSG msg{};
    while (!s.done) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (!got)
                PostQuitMessage(int(msg.wParam));
            break;
        }
        if (!IsDialogMessageW(w, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(parent, TRUE);
    DestroyWindow(w);
    DeleteObject(font);
    SetForegroundWindow(parent);
    return s.ok ? s.value : L"";
}
std::vector<std::wstring> choose(HWND owner, bool folders, bool save = false, bool singleFolder = false) {
    IFileDialog *dialog = nullptr;
    HRESULT hr =
        save ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))
             : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr))
        return {};
    DWORD opts;
    dialog->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (folders)
        opts |= FOS_PICKFOLDERS | (singleFolder ? 0u : DWORD(FOS_ALLOWMULTISELECT));
    else if (!save)
        opts |= FOS_FILEMUSTEXIST;
    dialog->SetOptions(opts);
    dialog->SetTitle(folders ? (singleFolder ? L"选择导出目录" : L"选择图片文件夹（可多选）")
                     : save  ? L"保存筛选项目"
                             : L"打开筛选项目");
    COMDLG_FILTERSPEC filter[] = {{L"图片对比项目 (*.icmp)", L"*.icmp"}};
    if (!folders) {
        dialog->SetFileTypes(1, filter);
        dialog->SetDefaultExtension(L"icmp");
    }
    std::vector<std::wstring> paths;
    if (SUCCEEDED(dialog->Show(owner))) {
        if (folders) {
            IFileOpenDialog *open = nullptr;
            if (SUCCEEDED(dialog->QueryInterface(IID_PPV_ARGS(&open)))) {
                IShellItemArray *items = nullptr;
                if (SUCCEEDED(open->GetResults(&items))) {
                    DWORD count = 0;
                    items->GetCount(&count);
                    for (DWORD i = 0; i < count; ++i) {
                        IShellItem *item = nullptr;
                        if (SUCCEEDED(items->GetItemAt(i, &item))) {
                            PWSTR p = nullptr;
                            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                                paths.emplace_back(p);
                                CoTaskMemFree(p);
                            }
                            item->Release();
                        }
                    }
                    items->Release();
                }
                open->Release();
            }
        } else {
            IShellItem *item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR p = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                    paths.emplace_back(p);
                    CoTaskMemFree(p);
                }
                item->Release();
            }
        }
    }
    dialog->Release();
    return paths;
}
enum Commands {
    C_ADD = 10,
    C_OPEN,
    C_SAVE,
    C_SAVEAS,
    C_NEW,
    C_REFRESH,
    C_MODE,
    C_FILTER,
    C_RECT,
    C_UNDO,
    C_REDO,
    C_FIT,
    C_FOCUS,
    C_NEXT,
    C_PREV,
    C_GOTO,
    C_SETTINGS,
    C_HELP,
    C_EXPORT,
    C_EXPORT_CURRENT,
    C_EXPORT_ALL,
    C_SYNC_TAGS,
    C_CLEAR_RECTS,
    C_CYCLE,
    C_EXIT,
    C_FOLDER_UP,
    C_FOLDER_DOWN,
    C_FOLDER_REMOVE,
    C_CATEGORY_BASE = 500,
    C_PANE_BASE = 1000,
    C_FOLDER_BASE = 2000
};
struct Pane {
    int folder = 0;
    std::string path;
    Box outer, view, image;
};
struct ExportTask {
    std::string path, folder;
    ic::Edit edit;
};
class App {
  public:
    HWND hwnd = nullptr;
    ic::Model model;
    std::unique_ptr<Loader> loader;
    std::unique_ptr<FolderScanner> scanner;
    bool scanning = false, scanRefresh = false, scanMarksDirty = false;
    uint64_t scanGeneration = 0;
    std::vector<std::wstring> scanPaths;
    // GDI+ resources must be destroyed before GdiplusShutdown, not at process static teardown.
    std::unique_ptr<FontFamily> preferredFont;
    bool fontInitialized = false;
    std::map<std::pair<int, bool>, std::unique_ptr<Font>> fonts;
    HDC backDC = nullptr, imageDC = nullptr;
    HBITMAP backBitmap = nullptr;
    HGDIOBJ oldBackBitmap = nullptr;
    int backWidth = 0, backHeight = 0;
    uint64_t bufferAllocations = 0, renderedFrames = 0, interactiveUntil = 0;
    float scale = 1, width = 1280, height = 800;
    std::vector<Hit> hits;
    std::vector<Pane> panes;
    std::vector<size_t> visible;
    int filter = -1, selected = 0, folderScroll = 0;
    bool rectTool = false, focus = false, syncTags = false, dirty = false;
    std::wstring projectPath, status = L"添加文件夹，开始对比";
    std::wstring settingsPath;
    std::vector<std::string> defaultCategories{"A", "B", "C", "D"};
    double zoom = 1, panX = 0, panY = 0;
    int drag = 0, dragPane = -1;
    PointF start{}, last{};
    ic::Rect preview{};
    bool previewValid = false;
    std::thread exportWorker;
    std::atomic<bool> exporting{false}, cancelExport{false};
    std::atomic<int> exportDone{0}, exportTotal{0};
    std::mutex exportMu;
    std::wstring exportSummary;
    ~App() {
        cancelExport = true;
        if (exportWorker.joinable())
            exportWorker.join();
        scanner.reset();
        loader.reset();
        if (backDC) {
            if (oldBackBitmap)
                SelectObject(backDC, oldBackBitmap);
            if (backBitmap)
                DeleteObject(backBitmap);
            DeleteDC(backDC);
        }
        if (imageDC)
            DeleteDC(imageDC);
    }
    void invalidate() { InvalidateRect(hwnd, nullptr, FALSE); }
    void title() {
        std::wstring t = L"图片对比 / ImageCompare";
        if (!projectPath.empty())
            t += L" — " + fs::path(projectPath).filename().wstring();
        if (dirty)
            t += L" *";
        SetWindowTextW(hwnd, t.c_str());
    }
    void changed(const std::wstring &s) {
        dirty = true;
        status = s;
        title();
        ensurePosition();
        invalidate();
    }
    void refreshFilter() { visible = model.filtered(filter); }
    bool currentValid() const {
        return model.position < model.rows.size() &&
               std::binary_search(visible.begin(), visible.end(), model.position);
    }
    std::vector<std::string> currentPaths() const {
        return currentValid() ? model.rows[model.position].paths : std::vector<std::string>{};
    }
    void ensurePosition() {
        refreshFilter();
        if (!visible.empty() && !currentValid())
            model.position = *([&] {
                auto it = std::lower_bound(visible.begin(), visible.end(), model.position);
                return it == visible.end() ? visible.end() - 1 : it;
            })();
    }
    void resetView() {
        zoom = 1;
        panX = panY = 0;
    }
    std::vector<std::string> tagTargets() {
        auto paths = currentPaths();
        if (syncTags)
            return paths;
        if (selected >= 0 && size_t(selected) < paths.size() && !paths[selected].empty())
            return {paths[selected]};
        return {};
    }
    void nav(int delta) {
        if (visible.empty())
            return;
        auto it = std::find(visible.begin(), visible.end(), model.position);
        int index = it == visible.end() ? 0 : int(it - visible.begin());
        index = std::clamp(index + delta, 0, int(visible.size()) - 1);
        model.position = visible[index];
        status = L"";
        interact();
        invalidate();
    }
    bool save(bool as = false) {
        if (scanning) {
            status = L"文件夹扫描中，完成后即可保存项目";
            invalidate();
            return false;
        }
        if (as || projectPath.empty()) {
            auto paths = choose(hwnd, false, true);
            if (paths.empty())
                return false;
            try {
                platform::atomicWrite(paths[0], ic::serialize(model));
                projectPath = paths[0];
            } catch (const std::exception &e) {
                error(L"保存失败", e);
                return false;
            }
        } else {
            try {
                platform::atomicWrite(projectPath, ic::serialize(model));
            } catch (const std::exception &e) {
                error(L"保存失败", e);
                return false;
            }
        }
        dirty = false;
        status = L"项目已保存";
        title();
        invalidate();
        return true;
    }
    bool canReplace() {
        if (!dirty)
            return true;
        int answer = MessageBoxW(hwnd, L"当前项目有未保存的修改。是否保存？", L"保存项目",
                                 MB_YESNOCANCEL | MB_ICONQUESTION);
        return answer == IDNO || (answer == IDYES && save());
    }
    void error(const std::wstring &prefix, const std::exception &e) {
        auto s = prefix + L"\n" + wide(e.what());
        MessageBoxW(hwnd, s.c_str(), L"图片对比", MB_OK | MB_ICONERROR);
    }
    void cancelScan() {
        if (scanner)
            scanner->cancel();
        ++scanGeneration;
        scanning = false;
        scanPaths.clear();
    }
    void startScan(std::vector<std::wstring> paths, bool refresh, bool marksDirty) {
        scanPaths = std::move(paths);
        scanRefresh = refresh;
        scanMarksDirty = marksDirty;
        scanning = true;
        status = L"正在后台扫描 " + std::to_wstring(scanPaths.size()) + L" 个文件夹，可继续浏览…";
        scanner->submit(++scanGeneration, model.folders, scanPaths, model.byName, refresh);
        invalidate();
    }
    std::vector<std::wstring> folderPaths() const {
        std::vector<std::wstring> paths;
        for (auto &f : model.folders)
            paths.push_back(wide(f.path));
        return paths;
    }
    void addFolders(const std::vector<std::wstring> &paths) {
        if (paths.empty())
            return;
        auto desired = scanning ? scanPaths : folderPaths();
        std::set<std::string> keys;
        for (auto &path : desired)
            keys.insert(platform::pathKey(fs::path(path)));
        for (auto &path : paths) {
            if (desired.size() >= 256) {
                status = L"文件夹上限为 256";
                invalidate();
                break;
            }
            auto absolute = fs::absolute(fs::path(path)).lexically_normal();
            if (keys.insert(platform::pathKey(absolute)).second)
                desired.push_back(absolute.wstring());
        }
        if (desired.size() != (scanning ? scanPaths.size() : model.folders.size()))
            startScan(std::move(desired), scanning && scanRefresh, true);
    }
    void scanReady() {
        auto result = scanner->take();
        if (!result || result->generation != scanGeneration)
            return;
        scanning = false;
        scanPaths.clear();
        if (!result->error.empty()) {
            status = L"扫描失败：" + result->error;
            invalidate();
            return;
        }
        model.folders = std::move(result->folders);
        model.rows = std::move(result->rows);
        if (model.rows.empty())
            model.position = 0;
        else
            model.position = std::min(model.position, model.rows.size() - 1);
        selected = std::max(0, std::min(selected, int(model.folders.size()) - 1));
        if (scanRefresh)
            loader->clear(model.folders.size());
        else
            loader->configure(model.folders.size());
        ensurePosition();
        status =
            result->added ? L"已添加 " + std::to_wstring(result->added) + L" 个文件夹" : L"文件夹扫描完成";
        if (scanMarksDirty)
            changed(status);
        else
            invalidate();
        if (!result->warning.empty())
            MessageBoxW(hwnd, (L"以下文件夹读取失败，已有编辑和列表已保留：\n" + result->warning).c_str(),
                        L"扫描结果", MB_OK | MB_ICONWARNING);
    }
    void open(const std::wstring &path) {
        if (!canReplace())
            return;
        try {
            auto next = ic::deserialize(platform::readProject(path));
            cancelScan();
            model = std::move(next);
            projectPath = path;
            filter = -1;
            selected = 0;
            folderScroll = 0;
            focus = false;
            dirty = false;
            loader->clear(model.folders.size());
            ensurePosition();
            resetView();
            title();
            startScan(folderPaths(), true, false);
        } catch (const std::exception &e) {
            error(L"打开失败，当前项目未改变", e);
        }
    }
    void rescan() { startScan(scanning ? scanPaths : folderPaths(), true, scanning && scanMarksDirty); }
    void loadSettings() {
        try {
            if (settingsPath.empty()) {
                PWSTR p = nullptr;
                if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)))
                    return;
                fs::path dir(p);
                CoTaskMemFree(p);
                settingsPath = (dir / L"ImageCompare" / L"settings.icmp").wstring();
            }
            if (fs::exists(settingsPath)) {
                auto settings = ic::deserialize(platform::readProject(settingsPath));
                defaultCategories = settings.categories;
                model.categories = defaultCategories;
            }
        } catch (...) { /* An unavailable preferences file does not prevent startup. */
        }
    }
    void persistSettings() {
        if (settingsPath.empty())
            return;
        fs::create_directories(fs::path(settingsPath).parent_path());
        ic::Model settings;
        settings.categories = model.categories;
        platform::atomicWrite(settingsPath, ic::serialize(settings));
        defaultCategories = model.categories;
    }
    void addCategory() {
        auto name = inputBox(hwnd, L"设置 · 标记类别", L"新增类别名称（最多 32 个字符）");
        while (!name.empty() && iswspace(name.front()))
            name.erase(name.begin());
        while (!name.empty() && iswspace(name.back()))
            name.pop_back();
        if (name.empty())
            return;
        if (name.size() > 32 || model.categories.size() >= 100) {
            MessageBoxW(hwnd, L"名称最多 32 个字符，总类别最多 100 个。", L"标记类别", MB_OK);
            return;
        }
        auto key = platform::folded(name);
        for (auto &c : model.categories)
            if (platform::folded(wide(c)) == key) {
                MessageBoxW(hwnd, L"类别名称已存在。", L"标记类别", MB_OK);
                return;
            }
        model.categories.push_back(utf8(name));
        changed(L"类别已添加");
        try {
            persistSettings();
        } catch (const std::exception &e) {
            error(L"类别已加入当前项目，但默认设置未能保存。可通过保存项目保留类别。", e);
        }
    }
    void popup(const std::vector<std::pair<int, std::wstring>> &items) {
        HMENU menu = CreatePopupMenu();
        for (auto &it : items) {
            if (!it.first)
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            else
                AppendMenuW(menu, MF_STRING, it.first, it.second.c_str());
        }
        POINT p;
        GetCursorPos(&p);
        int id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
        if (id)
            command(id);
    }
    void filterMenu() {
        HMENU m = CreatePopupMenu();
        auto append = [&](int v, const std::wstring &label) {
            AppendMenuW(m, MF_STRING | (filter == v ? MF_CHECKED : 0), UINT_PTR(v + 20004), label.c_str());
        };
        append(-1, L"全部对比组");
        append(-2, L"整组未标记");
        append(-3, L"组内有任意标记");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        for (size_t i = 0; i < model.categories.size(); ++i)
            append(int(i), L"组内包含 " + wide(model.categories[i]));
        POINT p;
        GetCursorPos(&p);
        int v = TrackPopupMenu(m, TPM_RETURNCMD, p.x, p.y, 0, hwnd, nullptr);
        DestroyMenu(m);
        if (v) {
            filter = v - 20004;
            ensurePosition();
            invalidate();
        }
    }
    void cycle() {
        if (model.folders.empty())
            return;
        selected = (selected + 1) % int(model.folders.size());
        invalidate();
    }
    void help() {
        MessageBoxW(hwnd,
                    L"1. 添加多个文件夹，或直接拖入文件夹。仅扫描文件夹第一层。\n"
                    L"2. 顺序配对按自然排序（2 在 10 前）；文件名匹配包含扩展名，忽略大小写，缺图保留空位。\n"
                    L"3. 点击图片选中；黑框工具 R "
                    L"在任意图片上拖动，同步应用到本组所有有效图片。不同尺寸按比例定位。\n"
                    L"4. 滚轮同步缩放，浏览模式左键拖动平移；黑框模式可用右键或空格+左键平移，F 适应窗口。\n"
                    L"5. 左右方向键 / PageUp、PageDown 切换对比组；Tab 轮流选图；V "
                    L"切换并排/单图。单图模式下黑框仍同步整组。\n"
                    L"6. A/B/C/D 切换选中图片的标记；可勾选“标记整组”；1–9 对应前九类。设置中可新增类别。\n"
                    L"7. Ctrl+Z 撤销，Ctrl+Y 或 Ctrl+Shift+Z 重做，含矩形和标记。Esc 取消正在绘制的矩形。\n"
                    L"8. Ctrl+S 保存项目，Ctrl+O 打开项目，Ctrl+Shift+S 另存为。导出不会覆盖原图。\n"
                    L"9. 筛选以对比组为单位；批量导出包含筛选组内所有有效图片。\n\n"
                    L"支持 PNG/JPEG/BMP/GIF/TIFF；GIF/TIFF 读取首帧。预览最长边 "
                    L"2560，导出保持原始像素尺寸（EXIF 方向纠正后），上限 1.2 亿像素。\n"
                    L"项目保存路径、类别、标记、黑框；撤销历史在本次会话内有效。",
                    L"图片对比 · 使用说明", MB_OK | MB_ICONINFORMATION);
    }
    void exportImages(bool all) {
        if (exporting) {
            MessageBoxW(hwnd, L"正在导出，请等待完成。", L"导出", MB_OK);
            return;
        }
        auto rows = all              ? visible
                    : currentValid() ? std::vector<size_t>{model.position}
                                     : std::vector<size_t>{};
        if (rows.empty()) {
            MessageBoxW(hwnd, L"没有可以导出的图片。", L"导出", MB_OK);
            return;
        }
        std::vector<ExportTask> tasks;
        std::set<std::string> seen;
        for (auto row : rows)
            for (size_t i = 0; i < model.rows[row].paths.size(); ++i) {
                auto &path = model.rows[row].paths[i];
                if (!path.empty() && seen.insert(path).second) {
                    auto folder = std::to_wstring(i + 1) + L"_" +
                                  fs::path(wide(model.folders[i].path)).filename().wstring();
                    if (folder.size() > 90)
                        folder.resize(90);
                    tasks.push_back({path, utf8(folder), model.edit(path)});
                }
            }
        if (tasks.empty())
            return;
        auto picked = choose(hwnd, true, false, true);
        if (picked.empty())
            return;
        fs::path root;
        try {
            SYSTEMTIME t;
            GetLocalTime(&t);
            wchar_t stamp[80];
            swprintf(stamp, 80, L"ImageCompare_%04u%02u%02u_%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour,
                     t.wMinute, t.wSecond);
            for (int i = 0; i < 10000; ++i) {
                root = fs::path(picked[0]) / (std::wstring(stamp) + (i ? L"_" + std::to_wstring(i) : L""));
                std::error_code ec;
                if (fs::create_directory(root, ec))
                    break;
                if (ec && ec != std::errc::file_exists)
                    throw std::runtime_error("Cannot create export folder");
                if (i == 9999)
                    throw std::runtime_error("Cannot choose unique export folder");
            }
        } catch (const std::exception &e) {
            error(L"创建导出目录失败", e);
            return;
        }
        if (exportWorker.joinable())
            exportWorker.join();
        exporting = true;
        cancelExport = false;
        exportDone = 0;
        exportTotal = int(tasks.size());
        status = L"正在导出…";
        invalidate();
        exportWorker = std::thread([this, tasks = std::move(tasks), root] {
            int success = 0, failed = 0;
            std::string csv = "\xef\xbb\xbfsource,output,tags,black_rectangles,result\r\n";
            std::string errors;
            for (auto &task : tasks) {
                if (cancelExport)
                    break;
                std::string result = "OK", output;
                try {
                    auto dir = root / fs::path(wide(task.folder));
                    fs::create_directories(dir);
                    auto dest = dir / fs::path(fs::path(wide(task.path)).filename().wstring() + L".png");
                    platform::exportPng(task.path, dest, task.edit);
                    output = utf8(dest.wstring());
                    ++success;
                } catch (const std::exception &e) {
                    ++failed;
                    result = e.what();
                    errors += task.path + " : " + e.what() + "\r\n";
                }
                std::string tags;
                for (auto &tag : task.edit.tags) {
                    if (!tags.empty())
                        tags += ';';
                    tags += tag;
                }
                csv += ic::csvCell(task.path) + "," + ic::csvCell(output) + "," + ic::csvCell(tags) + "," +
                       std::to_string(task.edit.rects.size()) + "," + ic::csvCell(result) + "\r\n";
                exportDone++;
                PostMessageW(hwnd, WM_EXPORT_PROGRESS, 0, 0);
            }
            try {
                platform::atomicWrite(root / L"labels.csv", csv);
                if (!errors.empty())
                    platform::atomicWrite(root / L"errors.txt", errors);
            } catch (const std::exception &e) {
                errors += "Report: " + std::string(e.what());
            }
            std::wstring summary = L"导出完成：成功 " + std::to_wstring(success) + L" 张，失败 " +
                                   std::to_wstring(failed) + L" 张。\n" + root.wstring();
            if (cancelExport)
                summary = L"导出已取消。\n" + summary;
            if (!errors.empty())
                summary += L"\n部分图片或报告写入失败，请查看导出目录。";
            {
                std::lock_guard<std::mutex> lock(exportMu);
                exportSummary = summary;
            }
            exporting = false;
            PostMessageW(hwnd, WM_EXPORT_DONE, 0, 0);
        });
    }
    void command(int id) {
        if (scanning && (id == C_FOLDER_REMOVE || id == C_FOLDER_UP || id == C_FOLDER_DOWN)) {
            status = L"扫描完成后可调整文件夹顺序";
            invalidate();
            return;
        }
        if (id >= C_CATEGORY_BASE && id < C_CATEGORY_BASE + 100) {
            size_t i = id - C_CATEGORY_BASE;
            if (i < model.categories.size()) {
                auto targets = tagTargets();
                if (targets.empty()) {
                    status = L"请先选中有图片的面板";
                    invalidate();
                    return;
                }
                model.toggleTag(targets, model.categories[i]);
                changed(L"已更新标记（可撤销）");
            }
            return;
        }
        if (id >= C_PANE_BASE && id < C_PANE_BASE + 256) {
            selected = id - C_PANE_BASE;
            invalidate();
            return;
        }
        if (id >= C_FOLDER_BASE && id < C_FOLDER_BASE + 256) {
            selected = id - C_FOLDER_BASE;
            invalidate();
            return;
        }
        switch (id) {
        case C_ADD:
            addFolders(choose(hwnd, true));
            break;
        case C_OPEN: {
            auto p = choose(hwnd, false);
            if (!p.empty())
                open(p[0]);
            break;
        }
        case C_SAVE:
            save();
            break;
        case C_SAVEAS:
            save(true);
            break;
        case C_NEW:
            if (canReplace()) {
                cancelScan();
                model = ic::Model{};
                model.categories = defaultCategories;
                projectPath.clear();
                dirty = false;
                filter = -1;
                selected = 0;
                folderScroll = 0;
                focus = false;
                loader->clear(model.folders.size());
                ensurePosition();
                resetView();
                status = L"添加文件夹，开始对比";
                title();
                invalidate();
            }
            break;
        case C_REFRESH:
            rescan();
            break;
        case C_MODE:
            model.byName = !model.byName;
            startScan(scanning ? scanPaths : folderPaths(), scanning && scanRefresh, true);
            break;
        case C_FILTER:
            filterMenu();
            break;
        case C_RECT:
            rectTool = !rectTool;
            status = rectTool ? L"拖动绘制黑色矩形，同步应用于当前整组" : L"浏览模式：左键拖动平移，滚轮缩放";
            invalidate();
            break;
        case C_UNDO:
            if (model.undo())
                changed(L"已撤销整次操作");
            break;
        case C_REDO:
            if (model.redo())
                changed(L"已重做整次操作");
            break;
        case C_FIT:
            resetView();
            invalidate();
            break;
        case C_FOCUS:
            focus = !focus;
            invalidate();
            break;
        case C_CYCLE:
            cycle();
            break;
        case C_NEXT:
            nav(1);
            break;
        case C_PREV:
            nav(-1);
            break;
        case C_GOTO: {
            if (visible.empty())
                break;
            auto s = inputBox(hwnd, L"跳转",
                              L"输入筛选列表中的组序号（1–" + std::to_wstring(visible.size()) + L"）");
            if (s.empty())
                break;
            try {
                size_t n = 0;
                auto v = std::stoull(s, &n);
                if (n != s.size() || v < 1 || v > visible.size())
                    throw std::runtime_error("Invalid position");
                model.position = visible[size_t(v - 1)];
                invalidate();
            } catch (...) {
                MessageBoxW(hwnd, L"请输入有效序号。", L"跳转", MB_OK);
            }
            break;
        }
        case C_SETTINGS:
            addCategory();
            break;
        case C_HELP:
            help();
            break;
        case C_EXPORT:
            popup({{C_EXPORT_CURRENT, L"导出当前对比组…"}, {C_EXPORT_ALL, L"导出当前筛选的所有组…"}});
            break;
        case C_EXPORT_CURRENT:
            exportImages(false);
            break;
        case C_EXPORT_ALL:
            exportImages(true);
            break;
        case C_SYNC_TAGS:
            syncTags = !syncTags;
            invalidate();
            break;
        case C_CLEAR_RECTS:
            if (currentValid()) {
                model.clearRects(currentPaths());
                changed(L"已清除整组黑框（可撤销）");
            }
            break;
        case C_FOLDER_REMOVE:
            if (selected >= 0 && size_t(selected) < model.folders.size()) {
                model.folders.erase(model.folders.begin() + selected);
                selected = std::max(0, std::min(selected, int(model.folders.size()) - 1));
                model.rebuild();
                ensurePosition();
                loader->clear(model.folders.size());
                changed(L"文件夹已移出项目，编辑仍保留");
            }
            break;
        case C_FOLDER_UP:
        case C_FOLDER_DOWN: {
            int other = selected + (id == C_FOLDER_UP ? -1 : 1);
            if (other >= 0 && size_t(other) < model.folders.size()) {
                std::swap(model.folders[selected], model.folders[other]);
                selected = other;
                model.rebuild();
                ensurePosition();
                changed(L"已调整文件夹顺序");
            }
            break;
        }
        case C_EXIT:
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
        default:
            break;
        }
    }
    static Color color(unsigned r, unsigned g, unsigned b) { return Color(255, BYTE(r), BYTE(g), BYTE(b)); }
    void fill(Graphics &g, Box b, Color c, float radius = 0) {
        SolidBrush brush(c);
        if (radius <= 0)
            g.FillRectangle(&brush, b.rect());
        else {
            GraphicsPath p;
            float r = std::min(radius, std::min(b.w, b.h) / 2) * 2;
            p.AddArc(b.x, b.y, r, r, 180, 90);
            p.AddArc(b.x + b.w - r, b.y, r, r, 270, 90);
            p.AddArc(b.x + b.w - r, b.y + b.h - r, r, r, 0, 90);
            p.AddArc(b.x, b.y + b.h - r, r, r, 90, 90);
            p.CloseFigure();
            g.FillPath(&brush, &p);
        }
    }
    void text(Graphics &g, const std::wstring &s, Box b, float size = 13, Color c = Color(255, 222, 227, 237),
              bool bold = false, StringAlignment align = StringAlignmentNear) {
        if (!fontInitialized) {
            fontInitialized = true;
            for (auto name : {L"Microsoft YaHei UI", L"Segoe UI", L"WenQuanYi Zen Hei", L"Arial"}) {
                auto family = std::make_unique<FontFamily>(name);
                if (family->IsAvailable()) {
                    preferredFont = std::move(family);
                    break;
                }
            }
        }
        auto key = std::make_pair(int(std::round(size * 100)), bold);
        auto &cached = fonts[key];
        if (!cached)
            cached =
                std::make_unique<Font>(preferredFont ? preferredFont.get() : FontFamily::GenericSansSerif(),
                                       size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
        SolidBrush brush(c);
        StringFormat f;
        f.SetAlignment(align);
        f.SetLineAlignment(StringAlignmentCenter);
        f.SetTrimming(StringTrimmingEllipsisCharacter);
        f.SetFormatFlags(StringFormatFlagsNoWrap);
        g.DrawString(s.c_str(), int(s.size()), cached.get(), b.rect(), &f, &brush);
    }
    void button(Graphics &g, Box b, const std::wstring &label, int id, bool active = false,
                bool enabled = true) {
        auto bg = active ? color(54, 87, 205) : color(37, 45, 60);
        fill(g, b, enabled ? bg : color(27, 33, 44), 6);
        text(g, label, b, 12, enabled ? color(232, 237, 248) : color(88, 99, 119), active,
             StringAlignmentCenter);
        if (enabled)
            hits.push_back({b, id});
    }
    std::wstring folderName(int i) {
        auto p = fs::path(wide(model.folders[i].path));
        auto name = p.filename().wstring();
        return name.empty() ? p.wstring() : name;
    }
    std::wstring tagsFor(const std::string &path) {
        std::wstring s;
        for (auto &t : model.edit(path).tags) {
            if (!s.empty())
                s += L"  ";
            s += wide(t);
        }
        return s;
    }
    void interact() {
        interactiveUntil = GetTickCount64() + 120;
        SetTimer(hwnd, 1, 120, nullptr);
    }
    void clampPan(const Pane &pane) {
        double mx = std::max(0., double(pane.image.w - pane.view.w) / std::max(1.f, pane.view.w) / 2);
        double my = std::max(0., double(pane.image.h - pane.view.h) / std::max(1.f, pane.view.h) / 2);
        panX = std::clamp(panX, -mx, mx);
        panY = std::clamp(panY, -my, my);
    }
    void zoomAt(PointF p, double factor) {
        double before = zoom;
        zoom = std::clamp(zoom * factor, .1, 32.);
        for (auto &pane : panes)
            if (pane.view.contains(p.X, p.Y) && pane.image.w > 0) {
                double ratio = zoom / before;
                double u = (p.X - pane.image.x) / pane.image.w, v = (p.Y - pane.image.y) / pane.image.h;
                panX =
                    (p.X - (pane.view.x + pane.view.w / 2) - (u - .5) * pane.image.w * ratio) / pane.view.w;
                panY =
                    (p.Y - (pane.view.y + pane.view.h / 2) - (v - .5) * pane.image.h * ratio) / pane.view.h;
                Pane next = pane;
                next.image.w *= float(ratio);
                next.image.h *= float(ratio);
                clampPan(next);
                break;
            }
        interact();
        invalidate();
    }
    bool prepareBuffer(HDC dc, int w, int h) {
        if (w <= 0 || h <= 0)
            return false;
        if (!backDC) {
            backDC = CreateCompatibleDC(dc);
            imageDC = CreateCompatibleDC(dc);
        }
        if (!backDC || !imageDC)
            return false;
        if (w != backWidth || h != backHeight) {
            if (backBitmap) {
                SelectObject(backDC, oldBackBitmap);
                DeleteObject(backBitmap);
                backBitmap = nullptr;
            }
            backBitmap = CreateCompatibleBitmap(dc, w, h);
            if (!backBitmap)
                return false;
            auto previous = SelectObject(backDC, backBitmap);
            if (!oldBackBitmap)
                oldBackBitmap = previous;
            backWidth = w;
            backHeight = h;
            ++bufferAllocations;
        }
        return true;
    }
    void requestImages() {
        if (!currentValid()) {
            loader->want({}, {});
            return;
        }
        auto current = currentPaths();
        std::vector<std::string> adjacent;
        if (focus && selected >= 0 && size_t(selected) < current.size()) {
            for (size_t i = 0; i < current.size(); ++i)
                if (int(i) != selected)
                    adjacent.push_back(current[i]);
            current = {current[selected]};
        }
        auto it = std::lower_bound(visible.begin(), visible.end(), model.position);
        for (int direction : {1, -1}) {
            auto index = int64_t(it - visible.begin()) + direction;
            if (index >= 0 && size_t(index) < visible.size()) {
                auto &paths = model.rows[visible[size_t(index)]].paths;
                if (focus && selected >= 0 && size_t(selected) < paths.size())
                    adjacent.push_back(paths[selected]);
                else
                    adjacent.insert(adjacent.end(), paths.begin(), paths.end());
            }
        }
        loader->want(current, adjacent);
    }
    void paint(HDC dc) {
        RECT client;
        GetClientRect(hwnd, &client);
        width = (client.right - client.left) / scale;
        height = (client.bottom - client.top) / scale;
        if (!prepareBuffer(dc, client.right, client.bottom))
            return;
        HDC buffer = backDC;
        struct Draw {
            std::shared_ptr<platform::Preview> bitmap;
            Box image, view;
            const ic::Edit *edit;
        };
        std::vector<Draw> draws;
        draws.reserve(model.folders.size());
        requestImages();
        {
            Graphics g(buffer);
            g.ScaleTransform(scale, scale);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
            g.Clear(color(17, 22, 31));
            hits.clear();
            panes.clear();
            fill(g, {0, 0, width, 68}, color(22, 28, 40));
            fill(g, {20, 22, 25, 25}, color(102, 135, 255), 7);
            text(g, L"Ⅱ", {20, 20, 25, 27}, 17, color(255, 255, 255), true, StringAlignmentCenter);
            text(g, L"图片对比", {58, 10, 230, 29}, 21, color(240, 243, 252), true);
            text(g, L"IMAGECOMPARE   /   本地图片筛选与同步处理", {60, 38, 360, 19}, 10,
                 color(131, 145, 168));
            button(g, {width - 394, 19, 74, 32}, L"项目 ▾", 30000);
            button(g, {width - 310, 19, 74, 32}, L"设置 +", C_SETTINGS);
            button(g, {width - 226, 19, 64, 32}, L"帮助", C_HELP);
            button(g, {width - 150, 19, 128, 32}, exporting ? L"导出中…" : L"导出图片 ▾", C_EXPORT, true,
                   !exporting);
            const float side = 218, right = 196, mainX = side + 20, mainW = width - side - right - 48;
            fill(g, {0, 68, side, height - 68}, color(22, 28, 40));
            text(g, L"对比文件夹", {18, 84, 128, 26}, 14, color(231, 236, 247), true);
            text(g, std::to_wstring(model.folders.size()), {160, 84, 40, 26}, 12, color(135, 151, 179), false,
                 StringAlignmentFar);
            button(g, {16, 122, side - 32, 35}, L"+  添加文件夹", C_ADD, true);
            text(g, L"支持拖入文件夹 · 右键管理", {17, 163, side - 30, 22}, 10, color(117, 133, 159));
            int folderSlots = std::max(1, int((height - 314) / 65));
            folderScroll = std::clamp(folderScroll, 0, std::max(0, int(model.folders.size()) - folderSlots));
            for (int k = 0; k < folderSlots && k + folderScroll < int(model.folders.size()); ++k) {
                int i = k + folderScroll;
                float y = 196 + k * 65.f;
                Box b{12, y, side - 24, 57};
                if (i == selected)
                    fill(g, b, color(40, 53, 84), 6);
                text(g, std::to_wstring(i + 1), {21, y + 12, 24, 28}, 12, color(128, 156, 255), true);
                text(g, folderName(i), {51, y + 6, side - 65, 25}, 12, color(225, 231, 244), i == selected);
                text(g, std::to_wstring(model.folders[i].files.size()) + L" 张图片",
                     {51, y + 30, side - 65, 18}, 10, color(137, 153, 179));
                hits.push_back({b, C_FOLDER_BASE + i});
            }
            text(g, L"滚轮可滚动文件夹列表", {17, height - 108, side - 30, 19}, 10, color(99, 115, 142));
            button(g, {16, height - 77, 88, 30}, L"打开项目", C_OPEN);
            button(g, {113, height - 77, 88, 30}, L"保存项目", C_SAVE);
            text(g, L"PNG · JPG · BMP · GIF · TIFF", {17, height - 40, side - 30, 22}, 9,
                 color(108, 123, 147));
            button(g, {mainX, 84, 156, 32}, model.byName ? L"文件名匹配 ▾" : L"按顺序配对 ▾", C_MODE);
            std::wstring filterLabel = filter == -1   ? L"全部对比组"
                                       : filter == -2 ? L"整组未标记"
                                       : filter == -3 ? L"有标记的组"
                                                      : L"标记 " + wide(model.categories[filter]);
            button(g, {mainX + 166, 84, 156, 32}, filterLabel + L" ▾", C_FILTER);
            button(g, {mainX + 332, 84, 64, 32}, L"刷新", C_REFRESH);
            float y = 130;
            button(g, {mainX, y, 100, 30}, rectTool ? L"黑框工具 · R" : L"浏览 / 黑框 R", C_RECT, rectTool);
            button(g, {mainX + 108, y, 58, 30}, L"撤销", C_UNDO, false, !model.undoStack.empty());
            button(g, {mainX + 174, y, 58, 30}, L"重做", C_REDO, false, !model.redoStack.empty());
            button(g, {mainX + 240, y, 80, 30}, L"适应 · F", C_FIT);
            button(g, {mainX + 328, y, 104, 30}, focus ? L"返回并排 · V" : L"单图轮看 · V", C_FOCUS, focus);
            // The right rail keeps tagging independent from navigation.
            float railX = width - right;
            text(g, L"标记类别", {railX, 85, right - 20, 25}, 14, color(232, 237, 247), true);
            button(g, {railX, 124, right - 20, 30}, syncTags ? L"☑  标记整组" : L"☐  仅标记选中图片",
                   C_SYNC_TAGS, syncTags);
            auto targets = tagTargets();
            int shown = std::max(0, int((height - 395) / 36));
            shown = std::min(shown, int(model.categories.size()));
            for (int i = 0; i < shown; ++i) {
                bool all = !targets.empty(), any = false;
                for (auto &p : targets) {
                    auto &tags = model.edit(p).tags;
                    bool has = std::find(tags.begin(), tags.end(), model.categories[i]) != tags.end();
                    all &= has;
                    any |= has;
                }
                auto name = wide(model.categories[i]);
                std::wstring label = (all ? L"●  " : any ? L"◐  " : L"○  ") + name;
                if (i < 9)
                    label += L"    " + std::to_wstring(i + 1);
                button(g, {railX, 166 + i * 36.f, right - 20, 29}, label, C_CATEGORY_BASE + i, all);
            }
            if (shown < int(model.categories.size()))
                button(g, {railX, 169 + shown * 36.f, right - 20, 29}, L"全部类别…", 30001);
            text(g, L"编辑联动", {railX, height - 223, right - 20, 24}, 12, color(214, 223, 240), true);
            text(g, L"黑框同步应用于当前整组", {railX, height - 196, right - 20, 21}, 10,
                 color(134, 151, 180));
            text(g, L"不同尺寸按相对比例定位", {railX, height - 175, right - 20, 21}, 10,
                 color(134, 151, 180));
            button(g, {railX, height - 142, right - 20, 29}, L"清除整组黑框", C_CLEAR_RECTS, false,
                   currentValid());
            text(g, L"← → 切组    Tab 切图", {railX, height - 96, right - 20, 20}, 10, color(116, 134, 164));
            text(g, L"左键拖动平移 · 滚轮缩放", {railX, height - 74, right - 20, 20}, 10,
                 color(116, 134, 164));
            text(g, L"Ctrl+Z 撤销 · Ctrl+S 保存", {railX, height - 52, right - 20, 20}, 10,
                 color(116, 134, 164));
            Box area{mainX, 178, mainW, height - 287};
            auto paths = currentPaths();
            if (model.folders.empty()) {
                fill(g, area, color(20, 27, 39), 9);
                text(g, scanning ? L"正在后台整理图片列表…" : L"把需要对比的文件夹放在一起",
                     {area.x + 20, area.y + area.h / 2 - 53, area.w - 40, 42}, 22, color(218, 229, 252), true,
                     StringAlignmentCenter);
                text(g, L"添加多个文件夹，按顺序或文件名对齐图片",
                     {area.x + 20, area.y + area.h / 2 - 4, area.w - 40, 28}, 12, color(133, 152, 184), false,
                     StringAlignmentCenter);
                button(g, {area.x + area.w / 2 - 85, area.y + area.h / 2 + 44, 170, 39}, L"+  选择图片文件夹",
                       C_ADD, true);
            } else if (!currentValid()) {
                fill(g, area, color(20, 27, 39), 9);
                text(g, model.rows.empty() ? L"文件夹内没有支持的图片" : L"没有符合当前筛选的对比组", area,
                     17, color(141, 159, 190), false, StringAlignmentCenter);
                if (!model.rows.empty())
                    button(g, {area.x + area.w / 2 - 75, area.y + area.h / 2 + 34, 150, 32}, L"更改筛选",
                           C_FILTER);
            } else {
                int total = focus ? 1 : int(model.folders.size());
                int cols = focus ? 1
                           : total <= 3
                               ? total
                               : int(std::ceil(std::sqrt(double(total) * area.w / std::max(1.f, area.h))));
                cols = std::clamp(cols, 1, total);
                int rows = (total + cols - 1) / cols;
                float gap = 12, pw = (area.w - gap * (cols - 1)) / cols,
                      ph = (area.h - gap * (rows - 1)) / rows;
                for (int k = 0; k < total; ++k) {
                    int i = focus ? std::clamp(selected, 0, int(paths.size()) - 1) : k;
                    Pane p;
                    p.folder = i;
                    p.path = paths[i];
                    p.outer = {area.x + (k % cols) * (pw + gap), area.y + (k / cols) * (ph + gap), pw, ph};
                    p.view = {p.outer.x + 2, p.outer.y + 40, pw - 4, ph - 68};
                    fill(g, p.outer, i == selected ? color(49, 77, 151) : color(40, 50, 68), 7);
                    fill(g, {p.outer.x + 1, p.outer.y + 1, pw - 2, ph - 2}, color(23, 30, 43), 6);
                    if (i == selected)
                        fill(g, {p.outer.x + 7, p.outer.y + 13, 3, 15}, color(115, 148, 255), 1);
                    text(g, std::to_wstring(i + 1) + L" / " + folderName(i),
                         {p.outer.x + 17, p.outer.y + 6, pw - 30, 28}, 12, color(216, 226, 245), true);
                    fill(g, p.view, color(10, 14, 21));
                    if (p.path.empty())
                        text(g, L"此文件夹无匹配图片", p.view, 13, color(104, 121, 151), false,
                             StringAlignmentCenter);
                    else {
                        auto entry = loader->get(p.path);
                        if (entry.image) {
                            auto &image = *entry.image;
                            float iw = float(image.width), ih = float(image.height);
                            double fit = std::min(p.view.w / iw, p.view.h / ih);
                            float dw = float(iw * fit * zoom), dh = float(ih * fit * zoom);
                            p.image = {p.view.x + (p.view.w - dw) / 2 + float(panX * p.view.w),
                                       p.view.y + (p.view.h - dh) / 2 + float(panY * p.view.h), dw, dh};
                            draws.push_back({entry.image, p.image, p.view, &model.edit(p.path)});
                        } else {
                            text(g, entry.error.empty() ? L"正在加载…" : L"读取失败 · 请检查文件或刷新",
                                 p.view, 12, color(142, 155, 177), false, StringAlignmentCenter);
                        }
                    }
                    std::wstring footer = p.path.empty() ? L"—" : fs::path(wide(p.path)).filename().wstring();
                    auto tags = p.path.empty() ? L"" : tagsFor(p.path);
                    if (!tags.empty())
                        footer += L"   [ " + tags + L" ]";
                    text(g, footer, {p.outer.x + 9, p.outer.y + ph - 26, pw - 18, 22}, 10,
                         color(137, 154, 184));
                    hits.push_back({p.outer, C_PANE_BASE + i});
                    panes.push_back(p);
                }
            }
            float bottom = height - 89;
            button(g, {mainX, bottom, 66, 32}, L"← 上组", C_PREV, false, !visible.empty());
            button(g, {mainX + 74, bottom, 66, 32}, L"下组 →", C_NEXT, false, !visible.empty());
            auto it = std::find(visible.begin(), visible.end(), model.position);
            std::wstring index = it != visible.end() ? std::to_wstring(it - visible.begin() + 1) : L"0";
            button(g, {mainX + 151, bottom, 144, 32},
                   index + L" / " + std::to_wstring(visible.size()) + L"  跳转", C_GOTO, false,
                   !visible.empty());
            text(g, L"缩放 " + std::to_wstring(int(std::round(zoom * 100))) + L"%",
                 {mainX + 306, bottom, 100, 32}, 11, color(136, 152, 180));
            std::wstring message = status;
            if (exporting)
                message = L"正在导出 " + std::to_wstring(exportDone.load()) + L" / " +
                          std::to_wstring(exportTotal.load()) + L"（可继续浏览）";
            else if (message.empty() && currentValid())
                message = model.byName ? L"文件名：" + wide(model.rows[model.position].label)
                                       : L"同步对比组 " + std::to_wstring(model.position + 1);
            text(g, message, {mainX, height - 45, mainW, 26}, 11, color(142, 160, 190));
            g.Flush(FlushIntentionSync);
        }
        bool quick = drag != 0 || GetTickCount64() < interactiveUntil;
        for (auto &draw : draws) {
            int state = SaveDC(buffer);
            IntersectClipRect(buffer, int(std::ceil(draw.view.x * scale)),
                              int(std::ceil(draw.view.y * scale)),
                              int(std::floor((draw.view.x + draw.view.w) * scale)),
                              int(std::floor((draw.view.y + draw.view.h) * scale)));
            float left = std::max(draw.view.x, draw.image.x), top = std::max(draw.view.y, draw.image.y);
            float right = std::min(draw.view.x + draw.view.w, draw.image.x + draw.image.w);
            float bottom = std::min(draw.view.y + draw.view.h, draw.image.y + draw.image.h);
            if (right > left && bottom > top) {
                auto &bitmap = *draw.bitmap;
                int sx = std::clamp(int(std::floor((left - draw.image.x) / draw.image.w * bitmap.width)), 0,
                                    bitmap.width - 1);
                int sy = std::clamp(int(std::floor((top - draw.image.y) / draw.image.h * bitmap.height)), 0,
                                    bitmap.height - 1);
                int sr = std::clamp(int(std::ceil((right - draw.image.x) / draw.image.w * bitmap.width)),
                                    sx + 1, bitmap.width);
                int sb = std::clamp(int(std::ceil((bottom - draw.image.y) / draw.image.h * bitmap.height)),
                                    sy + 1, bitmap.height);
                int dx = int(std::round((draw.image.x + draw.image.w * sx / bitmap.width) * scale));
                int dy = int(std::round((draw.image.y + draw.image.h * sy / bitmap.height) * scale));
                int dr = int(std::round((draw.image.x + draw.image.w * sr / bitmap.width) * scale));
                int db = int(std::round((draw.image.y + draw.image.h * sb / bitmap.height) * scale));
                auto previous = SelectObject(imageDC, bitmap.bitmap);
                SetStretchBltMode(buffer, quick ? COLORONCOLOR : HALFTONE);
                SetBrushOrgEx(buffer, 0, 0, nullptr);
                StretchBlt(buffer, dx, dy, dr - dx, db - dy, imageDC, sx, sy, sr - sx, sb - sy, SRCCOPY);
                SelectObject(imageDC, previous);
                auto mask = [&](const ic::Rect &r) {
                    RECT rect{LONG(std::floor((draw.image.x + r.x * draw.image.w) * scale)),
                              LONG(std::floor((draw.image.y + r.y * draw.image.h) * scale)),
                              LONG(std::ceil((draw.image.x + (r.x + r.w) * draw.image.w) * scale)),
                              LONG(std::ceil((draw.image.y + (r.y + r.h) * draw.image.h) * scale))};
                    FillRect(buffer, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                };
                for (auto &r : draw.edit->rects)
                    mask(r);
                if (previewValid)
                    mask(preview);
            }
            RestoreDC(buffer, state);
        }
        BitBlt(dc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
        ++renderedFrames;
    }
    PointF point(LPARAM lp) { return {GET_X_LPARAM(lp) / scale, GET_Y_LPARAM(lp) / scale}; }
    void mouseDown(PointF p, bool right = false) {
        SetFocus(hwnd);
        for (size_t k = 0; k < panes.size(); ++k) {
            auto &pane = panes[k];
            if (pane.view.contains(p.X, p.Y)) {
                selected = pane.folder;
                if (right || !rectTool || (GetKeyState(VK_SPACE) & 0x8000)) {
                    drag = 2;
                    last = p;
                    dragPane = int(k);
                    SetCapture(hwnd);
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                    interact();
                    invalidate();
                    return;
                }
                if (rectTool && !pane.path.empty() && pane.image.w > 0 && pane.image.contains(p.X, p.Y)) {
                    drag = 1;
                    dragPane = int(k);
                    start = {(p.X - pane.image.x) / pane.image.w, (p.Y - pane.image.y) / pane.image.h};
                    previewValid = false;
                    SetCapture(hwnd);
                    invalidate();
                    return;
                }
            }
        }
        if (right) {
            for (auto &h : hits)
                if (h.id >= C_FOLDER_BASE && h.id < C_FOLDER_BASE + 256 && h.box.contains(p.X, p.Y)) {
                    selected = h.id - C_FOLDER_BASE;
                    popup({{C_FOLDER_UP, L"上移文件夹"},
                           {C_FOLDER_DOWN, L"下移文件夹"},
                           {0, L""},
                           {C_FOLDER_REMOVE, L"移出项目（保留原图和编辑）"}});
                    invalidate();
                    return;
                }
            return;
        }
        for (auto it = hits.rbegin(); it != hits.rend(); ++it)
            if (it->box.contains(p.X, p.Y)) {
                if (it->id == 30000) {
                    popup({{C_NEW, L"新建项目"},
                           {C_OPEN, L"打开项目…   Ctrl+O"},
                           {C_SAVE, L"保存项目   Ctrl+S"},
                           {C_SAVEAS, L"项目另存为…"},
                           {0, L""},
                           {C_EXIT, L"退出"}});
                } else if (it->id == 30001) {
                    std::vector<std::pair<int, std::wstring>> items;
                    for (size_t i = 0; i < model.categories.size(); ++i)
                        items.push_back({C_CATEGORY_BASE + int(i), wide(model.categories[i])});
                    popup(items);
                } else
                    command(it->id);
                return;
            }
    }
    void mouseMove(PointF p) {
        if (!drag)
            return;
        if (drag == 2) {
            if (dragPane >= 0 && size_t(dragPane) < panes.size()) {
                panX += (p.X - last.X) / std::max(1.f, panes[dragPane].view.w);
                panY += (p.Y - last.Y) / std::max(1.f, panes[dragPane].view.h);
                clampPan(panes[dragPane]);
            }
            last = p;
            interact();
            invalidate();
            return;
        }
        if (dragPane < 0 || size_t(dragPane) >= panes.size())
            return;
        auto &image = panes[dragPane].image;
        preview = ic::rectangle(start.X, start.Y, (p.X - image.x) / image.w, (p.Y - image.y) / image.h);
        previewValid = preview.w * image.w >= 2 && preview.h * image.h >= 2;
        interact();
        invalidate();
    }
    void mouseUp(PointF p) {
        if (!drag)
            return;
        mouseMove(p);
        bool commit = drag == 1 && previewValid;
        auto r = preview;
        drag = 0;
        previewValid = false;
        ReleaseCapture();
        if (commit) {
            model.addRect(currentPaths(), r);
            changed(L"已为整组添加黑框（Ctrl+Z 撤销）");
        } else
            invalidate();
    }
    void cancelDrag() {
        drag = 0;
        previewValid = false;
        if (GetCapture() == hwnd)
            ReleaseCapture();
        invalidate();
    }
    void key(WPARAM vk) {
        if (drag) {
            if (vk == VK_ESCAPE)
                cancelDrag();
            return;
        }
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (ctrl) {
            if (vk == 'S')
                command(shift ? C_SAVEAS : C_SAVE);
            else if (vk == 'O')
                command(C_OPEN);
            else if (vk == 'Z')
                command(shift ? C_REDO : C_UNDO);
            else if (vk == 'Y')
                command(C_REDO);
            else if (vk == 'N')
                command(C_NEW);
            return;
        }
        if (vk == VK_RIGHT || vk == VK_NEXT)
            nav(1);
        else if (vk == VK_LEFT || vk == VK_PRIOR)
            nav(-1);
        else if (vk == VK_HOME) {
            if (!visible.empty()) {
                model.position = visible.front();
                invalidate();
            }
        } else if (vk == VK_END) {
            if (!visible.empty()) {
                model.position = visible.back();
                invalidate();
            }
        } else if (vk == VK_TAB)
            cycle();
        else if (vk == 'R')
            command(C_RECT);
        else if (vk == 'F')
            command(C_FIT);
        else if (vk == 'V')
            command(C_FOCUS);
        else if (vk == VK_F1)
            help();
        else if (vk >= '1' && vk <= '9')
            command(C_CATEGORY_BASE + int(vk - '1'));
        else if (vk >= 'A' && vk <= 'D')
            command(C_CATEGORY_BASE + int(vk - 'A'));
    }
};
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto *app = reinterpret_cast<App *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        app = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        app->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app)
        return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE:
        app->scale = GetDpiForWindow(hwnd) / 96.f;
        app->loader = std::make_unique<Loader>(hwnd);
        app->scanner = std::make_unique<FolderScanner>(hwnd);
        DragAcceptFiles(hwnd, TRUE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        auto dc = BeginPaint(hwnd, &ps);
        app->paint(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        app->cancelDrag();
        return 0;
    case WM_DPICHANGED: {
        app->scale = HIWORD(wp) / 96.f;
        auto *r = reinterpret_cast<RECT *>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        app->invalidate();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto *info = reinterpret_cast<MINMAXINFO *>(lp);
        float s = app->scale;
        info->ptMinTrackSize = {LONG(1080 * s), LONG(650 * s)};
        return 0;
    }
    case WM_COMMAND:
        app->command(LOWORD(wp));
        return 0;
    case WM_LBUTTONDOWN:
        app->mouseDown(app->point(lp));
        return 0;
    case WM_RBUTTONDOWN:
        app->mouseDown(app->point(lp), true);
        return 0;
    case WM_MOUSEMOVE:
        app->mouseMove(app->point(lp));
        return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        app->mouseUp(app->point(lp));
        return 0;
    case WM_CAPTURECHANGED:
        if (app->drag) {
            app->drag = 0;
            app->previewValid = false;
            app->invalidate();
        }
        return 0;
    case WM_MOUSEWHEEL: {
        if (app->drag)
            return 0;
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &p);
        if (p.x / app->scale < 218) {
            app->folderScroll += GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1;
            app->invalidate();
            return 0;
        }
        double factor = std::pow(1.16, GET_WHEEL_DELTA_WPARAM(wp) / 120.);
        app->zoomAt({p.x / app->scale, p.y / app->scale}, factor);
        return 0;
    }
    case WM_KEYDOWN:
        app->key(wp);
        return 0;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wp);
        UINT count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
        std::vector<std::wstring> paths;
        for (UINT i = 0; i < count; ++i) {
            UINT n = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring p(n + 1, 0);
            DragQueryFileW(drop, i, p.data(), n + 1);
            p.resize(n);
            if (platform::folded(fs::path(p).extension().wstring()) == L".icmp")
                app->open(p);
            else
                paths.push_back(p);
        }
        DragFinish(drop);
        if (!paths.empty())
            app->addFolders(paths);
        return 0;
    }
    case WM_SCAN_READY:
        app->scanReady();
        return 0;
    case WM_TIMER:
        if (wp == 1 && app->drag == 0 && GetTickCount64() >= app->interactiveUntil) {
            KillTimer(hwnd, 1);
            app->invalidate();
        }
        return 0;
    case WM_SETCURSOR:
        if (app->drag == 2) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return TRUE;
        }
        break;
    case WM_IMAGE_READY:
    case WM_EXPORT_PROGRESS:
        app->invalidate();
        return 0;
    case WM_EXPORT_DONE: {
        std::wstring summary;
        {
            std::lock_guard<std::mutex> lock(app->exportMu);
            summary = app->exportSummary;
        }
        app->status = L"导出已完成";
        app->invalidate();
        MessageBoxW(hwnd, summary.c_str(), L"导出结果", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    case WM_CLOSE:
        if (app->exporting) {
            if (MessageBoxW(hwnd, L"正在导出。是否取消剩余导出并退出？", L"退出",
                            MB_YESNO | MB_ICONQUESTION) != IDYES)
                return 0;
        }
        if (app->canReplace()) {
            app->cancelExport = true;
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ULONG_PTR token = 0;
    GdiplusStartupInput input;
    if (GdiplusStartup(&token, &input, nullptr) != Ok) {
        MessageBoxW(nullptr, L"无法初始化 Windows GDI+。", L"图片对比", MB_OK | MB_ICONERROR);
        return 1;
    }
    int result = 0;
    try {
        App app;
        app.loadSettings();
        WNDCLASSEXW c{};
        c.cbSize = sizeof(c);
        c.lpfnWndProc = WndProc;
        c.hInstance = instance;
        c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        c.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
        if (!c.hIcon)
            c.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        c.lpszClassName = L"ImageCompareMain";
        RegisterClassExW(&c);
        int w = std::min(1500, GetSystemMetrics(SM_CXSCREEN) - 60),
            h = std::min(900, GetSystemMetrics(SM_CYSCREEN) - 80);
        HWND hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, c.lpszClassName, L"图片对比 / ImageCompare",
                                    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr,
                                    instance, &app);
        if (!hwnd)
            throw std::runtime_error("Cannot create window");
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
        ShowWindow(hwnd, show);
        UpdateWindow(hwnd);
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::vector<std::wstring> folders;
        for (int i = 1; i < argc; ++i) {
            std::wstring p = argv[i];
            if (platform::folded(fs::path(p).extension().wstring()) == L".icmp")
                app.open(p);
            else
                folders.push_back(p);
        }
        if (argv)
            LocalFree(argv);
        if (!folders.empty())
            app.addFolders(folders);
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        result = int(msg.wParam);
    } catch (const std::exception &e) {
        MessageBoxW(nullptr, wide(e.what()).c_str(), L"图片对比 · 错误", MB_OK | MB_ICONERROR);
        result = 1;
    }
    GdiplusShutdown(token);
    if (SUCCEEDED(com))
        CoUninitialize();
    return result;
}
