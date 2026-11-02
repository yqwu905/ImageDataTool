#include "../src/core.hpp"
#include <iostream>
#include <random>
#define CHECK(c)                                                                                             \
    do {                                                                                                     \
        if (!(c))                                                                                            \
            throw std::runtime_error(std::string("Check failed: ") + #c + " at " +                           \
                                     std::to_string(__LINE__));                                              \
    } while (0)
int main() {
    try {
        ic::Model m;
        m.folders = {
            {"C:/参考",
             {{"a10", "图10.png", "图10.png"}, {"a2", "图2.png", "图2.png"}, {"a1", "图1.png", "图1.png"}}},
            {"D:/结果", {{"b2", "图2.PNG", "图2.png"}, {"b10", "图10.png", "图10.png"}}}};
        m.rebuild();
        CHECK(m.rows.size() == 3);
        CHECK(m.rows[0].paths == std::vector<std::string>({"a1", "b2"}));
        CHECK(m.rows[1].paths[0] == "a2");
        CHECK(m.rows[2].paths[1].empty());
        m.byName = true;
        m.rebuild();
        CHECK(m.rows.size() == 3);
        CHECK(m.rows[0].paths[1].empty());
        CHECK(m.rows[1].paths == std::vector<std::string>({"a2", "b2"}));
        CHECK(m.rows[2].paths == std::vector<std::string>({"a10", "b10"}));
        auto paths = m.rows[1].paths;
        auto r = ic::rectangle(.8, .9, .2, .3);
        CHECK(ic::valid(r));
        m.addRect(paths, r);
        CHECK(m.edit("a2").rects.size() == 1 && m.edit("b2").rects.size() == 1);
        CHECK(m.edit("a1").rects.empty());
        CHECK(m.undo());
        CHECK(m.edit("a2").rects.empty() && m.edit("b2").rects.empty());
        CHECK(m.redo());
        CHECK(m.edit("a2").rects[0] == r);
        m.toggleTag({"a2"}, "A");
        CHECK(m.filtered(0) == std::vector<size_t>({1}));
        m.toggleTag(paths, "A");
        CHECK(m.edit("a2").tags.size() == 1 && m.edit("b2").tags.size() == 1);
        m.toggleTag(paths, "A");
        CHECK(m.edit("a2").tags.empty() && m.edit("b2").tags.empty());
        CHECK(m.undo());
        CHECK(m.edit("b2").tags.size() == 1);
        CHECK(m.filtered(-2) == std::vector<size_t>({0, 2}));
        CHECK(m.filtered(-3) == std::vector<size_t>({1}));
        m.categories.push_back("需要复查");
        m.toggleTag(paths, "需要复查");
        m.position = 2;
        auto data = ic::serialize(m);
        auto restored = ic::deserialize(data);
        CHECK(restored.categories == m.categories);
        CHECK(restored.position == 2 && restored.byName);
        CHECK(restored.edits == m.edits);
        CHECK(restored.folders[0].path == "C:/参考");
        CHECK(restored.undoStack.empty());
        CHECK(ic::serialize(restored) == data);
        m.clearRects(paths);
        CHECK(m.edit("a2").rects.empty() && !m.edit("a2").tags.empty());
        CHECK(m.undo());
        CHECK(m.edit("a2").rects.size() == 1);
        m.toggleTag({"a1", "a1", ""}, "B");
        CHECK(m.redoStack.empty());
        CHECK(m.edit("a1").tags.size() == 1);
        auto n = m.undoStack.size();
        m.addRect(paths, {0, 0, -.1, .2});
        CHECK(m.undoStack.size() == n);
        m.addRect({"a1", "a1", ""}, ic::rectangle(-5, -4, 3, 2));
        CHECK(m.edit("a1").rects.size() == 1 && m.edit("a1").rects[0].w == 1);
        for (size_t i = 0; i < data.size(); ++i) {
            bool rejected = false;
            try {
                ic::deserialize(data.substr(0, i));
            } catch (...) {
                rejected = true;
            }
            CHECK(rejected);
        }
        for (auto bad : {std::string("garbage"), data + "extra"}) {
            bool rejected = false;
            try {
                ic::deserialize(bad);
            } catch (...) {
                rejected = true;
            }
            CHECK(rejected);
        }
        CHECK(ic::validUtf8("测试🙂"));
        CHECK(!ic::validUtf8("\xc0\xaf"));
        CHECK(!ic::validUtf8("\xed\xa0\x80"));
        CHECK(!ic::validUtf8("\xf4\x90\x80\x80"));
        CHECK(!ic::validUtf8("\xe4\xb8"));
        auto bad = data;
        bad[8] = 2;
        bool rejected = false;
        try {
            ic::deserialize(bad);
        } catch (...) {
            rejected = true;
        }
        CHECK(rejected);
        CHECK(ic::csvCell("a,\"b\n") == "\"a,\"\"b\n\"");
        std::vector<std::string> names = {"img10",
                                          "img2",
                                          "img01",
                                          "img1",
                                          "img000",
                                          "img0",
                                          "img100000000000000000001",
                                          "img99999999999999999999"};
        std::sort(names.begin(), names.end(), ic::naturalLess);
        CHECK(names == std::vector<std::string>({"img0", "img000", "img1", "img01", "img2", "img10",
                                                 "img99999999999999999999", "img100000000000000000001"}));
        // Large folder matching must remain O(n log n), preserving gaps.
        ic::Model large;
        large.byName = true;
        large.folders.resize(3);
        for (int f = 0; f < 3; ++f)
            for (int i = f; i < 10000; i += 2)
                large.folders[f].files.push_back({std::to_string(f) + "/" + std::to_string(i),
                                                  std::to_string(i) + ".png", std::to_string(i) + ".png"});
        large.rebuild();
        CHECK(large.rows.size() == 10000);
        CHECK(large.rows[2].label == "2.png");
        std::mt19937 rng(42);
        for (int run = 0; run < 2000; ++run) {
            std::string bytes(rng() % 256, ' ');
            for (auto &c : bytes)
                c = char(rng());
            try {
                ic::deserialize(bytes);
            } catch (...) {
            }
        }
        std::cout << "PASS: pairing, natural sorting, gaps, synchronized rectangles, undo/redo, "
                     "per-image/group tags, filters, Unicode projects, corrupt input, CSV, 10k files\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
