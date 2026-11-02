#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ic {
struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
};
inline bool operator==(const Rect &a, const Rect &b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}
inline Rect rectangle(double x1, double y1, double x2, double y2) {
    x1 = std::clamp(x1, 0., 1.);
    x2 = std::clamp(x2, 0., 1.);
    y1 = std::clamp(y1, 0., 1.);
    y2 = std::clamp(y2, 0., 1.);
    return {std::min(x1, x2), std::min(y1, y2), std::abs(x2 - x1), std::abs(y2 - y1)};
}
inline bool valid(const Rect &r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.w) && std::isfinite(r.h) && r.x >= 0 &&
           r.y >= 0 && r.w > 0 && r.h > 0 && r.x + r.w <= 1.00000001 && r.y + r.h <= 1.00000001;
}
inline bool naturalLess(const std::string &a, const std::string &b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] >= '0' && a[i] <= '9' && b[j] >= '0' && b[j] <= '9') {
            size_t ae = i, be = j;
            while (ae < a.size() && a[ae] >= '0' && a[ae] <= '9')
                ++ae;
            while (be < b.size() && b[be] >= '0' && b[be] <= '9')
                ++be;
            size_t ai = i, bi = j;
            while (ai < ae && a[ai] == '0')
                ++ai;
            while (bi < be && b[bi] == '0')
                ++bi;
            if (ae - ai != be - bi)
                return ae - ai < be - bi;
            int c = a.compare(ai, ae - ai, b, bi, be - bi);
            if (c)
                return c < 0;
            if (ae - i != be - j)
                return ae - i < be - j;
            i = ae;
            j = be;
        } else {
            if (a[i] != b[j])
                return (unsigned char)a[i] < (unsigned char)b[j];
            ++i;
            ++j;
        }
    }
    return a.size() < b.size();
}
struct File {
    std::string path, name, key;
};
struct Folder {
    std::string path;
    std::vector<File> files;
};
struct Row {
    std::string label;
    std::vector<std::string> paths;
};
struct Edit {
    std::vector<Rect> rects;
    std::vector<std::string> tags;
    bool operator==(const Edit &o) const { return rects == o.rects && tags == o.tags; }
};
struct Change {
    std::string path;
    Edit before, after;
};
struct Action {
    std::vector<Change> changes;
};
struct Model {
    std::vector<Folder> folders;
    std::vector<Row> rows;
    std::vector<std::string> categories{"A", "B", "C", "D"};
    std::map<std::string, Edit> edits;
    std::vector<Action> undoStack, redoStack;
    bool byName = false;
    size_t position = 0;
    const Edit &edit(const std::string &path) const {
        static const Edit empty;
        auto it = edits.find(path);
        return it == edits.end() ? empty : it->second;
    }
    void rebuild() {
        rows.clear();
        for (auto &f : folders)
            std::sort(f.files.begin(), f.files.end(),
                      [](const File &a, const File &b) { return naturalLess(a.key, b.key); });
        if (byName) {
            std::map<std::string, Row> keyed;
            for (size_t i = 0; i < folders.size(); ++i)
                for (auto &f : folders[i].files) {
                    auto &row = keyed[f.key];
                    if (row.paths.empty()) {
                        row.label = f.name;
                        row.paths.resize(folders.size());
                    }
                    row.paths[i] = f.path;
                }
            std::vector<std::string> keys;
            for (auto &kv : keyed)
                keys.push_back(kv.first);
            std::sort(keys.begin(), keys.end(), naturalLess);
            for (auto &key : keys)
                rows.push_back(std::move(keyed[key]));
        } else {
            size_t count = 0;
            for (auto &f : folders)
                count = std::max(count, f.files.size());
            for (size_t i = 0; i < count; ++i) {
                Row r;
                r.label = std::to_string(i + 1);
                for (auto &f : folders)
                    r.paths.push_back(i < f.files.size() ? f.files[i].path : "");
                rows.push_back(std::move(r));
            }
        }
        if (rows.empty())
            position = 0;
        else
            position = std::min(position, rows.size() - 1);
    }
    // -1 = all; -2 = unmarked; -3 = any mark; otherwise category index.
    std::vector<size_t> filtered(int filter) const {
        std::vector<size_t> result;
        for (size_t i = 0; i < rows.size(); ++i) {
            bool match = filter == -1, any = false;
            for (auto &path : rows[i].paths)
                if (!path.empty()) {
                    auto &e = edit(path);
                    any |= !e.tags.empty();
                    if (filter >= 0 && size_t(filter) < categories.size())
                        match |= std::find(e.tags.begin(), e.tags.end(), categories[filter]) != e.tags.end();
                }
            if (filter == -2)
                match = !any;
            if (filter == -3)
                match = any;
            if (match)
                result.push_back(i);
        }
        return result;
    }
    void apply(Action action) {
        if (action.changes.empty())
            return;
        for (auto &c : action.changes)
            edits[c.path] = c.after;
        undoStack.push_back(std::move(action));
        redoStack.clear();
    }
    void addRect(const std::vector<std::string> &paths, Rect r) {
        if (!valid(r))
            return;
        Action a;
        std::set<std::string> seen;
        for (auto &p : paths)
            if (!p.empty() && seen.insert(p).second) {
                Edit e = edit(p), next = e;
                next.rects.push_back(r);
                a.changes.push_back({p, e, next});
            }
        apply(std::move(a));
    }
    void clearRects(const std::vector<std::string> &paths) {
        Action a;
        std::set<std::string> seen;
        for (auto &p : paths)
            if (!p.empty() && seen.insert(p).second && !edit(p).rects.empty()) {
                Edit e = edit(p), next = e;
                next.rects.clear();
                a.changes.push_back({p, e, next});
            }
        apply(std::move(a));
    }
    void toggleTag(const std::vector<std::string> &paths, const std::string &tag) {
        std::set<std::string> seen;
        for (auto &p : paths)
            if (!p.empty())
                seen.insert(p);
        if (seen.empty())
            return;
        bool all = true;
        for (auto &p : seen) {
            auto &t = edit(p).tags;
            all &= std::find(t.begin(), t.end(), tag) != t.end();
        }
        Action a;
        for (auto &p : seen) {
            Edit e = edit(p), next = e;
            auto it = std::find(next.tags.begin(), next.tags.end(), tag);
            if (all && it != next.tags.end())
                next.tags.erase(it);
            else if (!all && it == next.tags.end())
                next.tags.push_back(tag);
            if (!(e == next))
                a.changes.push_back({p, e, next});
        }
        apply(std::move(a));
    }
    bool undo() {
        if (undoStack.empty())
            return false;
        auto a = std::move(undoStack.back());
        undoStack.pop_back();
        for (auto &c : a.changes)
            edits[c.path] = c.before;
        redoStack.push_back(std::move(a));
        return true;
    }
    bool redo() {
        if (redoStack.empty())
            return false;
        auto a = std::move(redoStack.back());
        redoStack.pop_back();
        for (auto &c : a.changes)
            edits[c.path] = c.after;
        undoStack.push_back(std::move(a));
        return true;
    }
};
// Versioned, bounded binary project format. All paths and labels are UTF-8.
struct Writer {
    std::string data = "ICMP\r\n\x1a\n";
    void u32(uint32_t n) {
        for (int i = 0; i < 4; ++i)
            data.push_back(char(n >> (i * 8)));
    }
    void str(const std::string &s) {
        if (s.size() > 1048576)
            throw std::runtime_error("String too long");
        u32(uint32_t(s.size()));
        data += s;
    }
    void real(double x) {
        uint64_t v;
        std::memcpy(&v, &x, 8);
        for (int i = 0; i < 8; ++i)
            data.push_back(char(v >> (i * 8)));
    }
};
inline bool validUtf8(const std::string &s) {
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp = static_cast<unsigned char>(s[i++]);
        if (cp < 128)
            continue;
        int n = 0;
        uint32_t minimum = 0;
        if (cp >= 0xc2 && cp <= 0xdf) {
            n = 1;
            cp &= 31;
            minimum = 0x80;
        } else if (cp >= 0xe0 && cp <= 0xef) {
            n = 2;
            cp &= 15;
            minimum = 0x800;
        } else if (cp >= 0xf0 && cp <= 0xf4) {
            n = 3;
            cp &= 7;
            minimum = 0x10000;
        } else
            return false;
        while (n--) {
            if (i >= s.size())
                return false;
            auto b = static_cast<unsigned char>(s[i++]);
            if ((b & 0xc0) != 0x80)
                return false;
            cp = (cp << 6) | (b & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            return false;
    }
    return true;
}
struct Reader {
    const std::string &data;
    size_t pos = 8;
    explicit Reader(const std::string &d) : data(d) {
        if (d.size() < 8 || d.substr(0, 8) != "ICMP\r\n\x1a\n" || d.size() > 64 * 1024 * 1024)
            throw std::runtime_error("Invalid project");
    }
    uint32_t u32() {
        if (pos + 4 > data.size())
            throw std::runtime_error("Truncated project");
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= uint32_t((unsigned char)data[pos++]) << (i * 8);
        return v;
    }
    uint32_t count(uint32_t max) {
        auto n = u32();
        if (n > max)
            throw std::runtime_error("Project limit exceeded");
        return n;
    }
    std::string str() {
        auto n = count(1048576);
        if (n > data.size() - pos)
            throw std::runtime_error("Truncated string");
        auto s = data.substr(pos, n);
        pos += n;
        if (!validUtf8(s) || s.find('\0') != std::string::npos)
            throw std::runtime_error("NUL in string");
        return s;
    }
    double real() {
        if (pos + 8 > data.size())
            throw std::runtime_error("Truncated number");
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v |= uint64_t((unsigned char)data[pos++]) << (i * 8);
        double x;
        std::memcpy(&x, &v, 8);
        return x;
    }
};
inline std::string serialize(const Model &m) {
    Writer w;
    w.u32(1);
    w.u32(m.byName ? 1 : 0);
    w.u32(uint32_t(m.position));
    w.u32(uint32_t(m.folders.size()));
    for (auto &f : m.folders)
        w.str(f.path);
    w.u32(uint32_t(m.categories.size()));
    for (auto &c : m.categories)
        w.str(c);
    w.u32(uint32_t(m.edits.size()));
    for (auto &kv : m.edits) {
        w.str(kv.first);
        w.u32(uint32_t(kv.second.rects.size()));
        for (auto &r : kv.second.rects) {
            w.real(r.x);
            w.real(r.y);
            w.real(r.w);
            w.real(r.h);
        }
        w.u32(uint32_t(kv.second.tags.size()));
        for (auto &t : kv.second.tags)
            w.str(t);
    }
    if (w.data.size() > 64 * 1024 * 1024)
        throw std::runtime_error("Project too large");
    return w.data;
}
inline Model deserialize(const std::string &data) {
    Reader r(data);
    if (r.u32() != 1)
        throw std::runtime_error("Unsupported project version");
    Model m;
    auto mode = r.u32();
    if (mode > 1)
        throw std::runtime_error("Invalid match mode");
    m.byName = mode != 0;
    m.position = r.u32();
    auto nf = r.count(256);
    for (uint32_t i = 0; i < nf; ++i) {
        auto p = r.str();
        if (p.empty())
            throw std::runtime_error("Empty folder");
        m.folders.push_back({p, {}});
    }
    auto nc = r.count(100);
    if (!nc)
        throw std::runtime_error("No categories");
    m.categories.clear();
    std::set<std::string> categories;
    for (uint32_t i = 0; i < nc; ++i) {
        auto c = r.str();
        if (c.empty() || c.size() > 256 || !categories.insert(c).second)
            throw std::runtime_error("Invalid category");
        m.categories.push_back(c);
    }
    auto ne = r.count(500000);
    for (uint32_t i = 0; i < ne; ++i) {
        auto p = r.str();
        if (p.empty() || m.edits.count(p))
            throw std::runtime_error("Invalid image key");
        Edit e;
        auto nr = r.count(100000);
        for (uint32_t j = 0; j < nr; ++j) {
            Rect q{r.real(), r.real(), r.real(), r.real()};
            if (!valid(q))
                throw std::runtime_error("Invalid rectangle");
            e.rects.push_back(q);
        }
        auto nt = r.count(100);
        std::set<std::string> tags;
        for (uint32_t j = 0; j < nt; ++j) {
            auto t = r.str();
            if (!categories.count(t) || !tags.insert(t).second)
                throw std::runtime_error("Invalid tag");
            e.tags.push_back(t);
        }
        m.edits.emplace(p, std::move(e));
    }
    if (r.pos != data.size())
        throw std::runtime_error("Trailing project data");
    return m;
}
inline std::string csvCell(const std::string &s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '\"')
            out += '\"';
        out += c;
    }
    return out + '\"';
}
} // namespace ic
