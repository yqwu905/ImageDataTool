#pragma once
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <optional>
#include "preview.hpp"

struct CacheEntry {
    std::shared_ptr<platform::Preview> image;
    std::wstring error;
    uint64_t age = 0;
};
class Loader {
    struct Job {
        std::string path;
        uint64_t generation;
        int edge;
    };
    HWND hwnd;
    std::mutex mu;
    std::condition_variable cv;
    bool stop = false;
    int maxEdge = 2560;
    uint64_t generation = 0, tick = 0, inFlightGeneration = 0;
    std::string inFlight;
    std::deque<Job> jobs;
    std::set<std::string> pending, active;
    std::map<std::string, size_t> priority;
    std::vector<std::string> lastWanted, lastActive;
    std::map<std::string, CacheEntry> cache;
    size_t bytes = 0;
    std::thread worker;
    static size_t size(const CacheEntry &e) { return e.image ? e.image->bytes() : 0; }
    void run() {
        platform::ComApartment apartment;
        platform::PreviewDecoder decoder;
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mu);
                cv.wait(lock, [&] { return stop || !jobs.empty(); });
                if (stop)
                    return;
                job = std::move(jobs.front());
                jobs.pop_front();
                inFlight = job.path;
                inFlightGeneration = job.generation;
            }
            CacheEntry entry;
            try {
                entry.image = decoder.load(job.path, job.edge);
            } catch (const std::exception &e) {
                entry.error = platform::wide(e.what());
            }
            ++decoded;
            bool notify = false;
            {
                std::lock_guard<std::mutex> lock(mu);
                inFlight.clear();
                if (job.generation != generation)
                    continue;
                pending.erase(job.path);
                entry.age = ++tick;
                auto old = cache.find(job.path);
                if (old != cache.end())
                    bytes -= size(old->second);
                bytes += size(entry);
                cache[job.path] = std::move(entry);
                notify = active.count(job.path) != 0;
                while (bytes > 192 * 1024 * 1024 || cache.size() > 512) {
                    auto oldest = cache.end();
                    auto rank = [&](const std::string &path) {
                        auto p = priority.find(path);
                        return p == priority.end() ? size_t(-1) : p->second;
                    };
                    for (auto it = cache.begin(); it != cache.end(); ++it) {
                        if (active.count(it->first))
                            continue;
                        if (oldest == cache.end() || rank(it->first) > rank(oldest->first) ||
                            (rank(it->first) == rank(oldest->first) && it->second.age < oldest->second.age))
                            oldest = it;
                    }
                    if (oldest == cache.end())
                        break;
                    bytes -= size(oldest->second);
                    cache.erase(oldest);
                }
            }
            if (notify)
                PostMessageW(hwnd, WM_APP + 1, 0, 0);
        }
    }

  public:
    std::atomic<uint64_t> decoded{0};
    explicit Loader(HWND w) : hwnd(w) {
        worker = std::thread([this] { run(); });
    }
    ~Loader() {
        {
            std::lock_guard<std::mutex> lock(mu);
            stop = true;
        }
        cv.notify_one();
        if (worker.joinable())
            worker.join();
    }
    void configure(size_t count) {
        std::lock_guard<std::mutex> lock(mu);
        maxEdge = std::min(
            2048, std::max(256, int(std::sqrt(96. * 1024 * 1024 / (4 * std::max(size_t(1), count))))));
    }
    void clear(size_t count = 1) {
        configure(count);
        std::map<std::string, CacheEntry> retired;
        {
            std::lock_guard<std::mutex> lock(mu);
            ++generation;
            jobs.clear();
            pending.clear();
            active.clear();
            priority.clear();
            lastWanted.clear();
            lastActive.clear();
            cache.swap(retired);
            bytes = 0;
        }
    }
    void want(const std::vector<std::string> &current, const std::vector<std::string> &adjacent) {
        std::vector<std::string> wanted = current;
        wanted.insert(wanted.end(), adjacent.begin(), adjacent.end());
        std::lock_guard<std::mutex> lock(mu);
        if (wanted == lastWanted && current == lastActive)
            return;
        lastWanted = wanted;
        lastActive = current;
        priority.clear();
        for (size_t i = 0; i < wanted.size(); ++i)
            priority.emplace(wanted[i], i);
        active.clear();
        for (auto &path : current)
            if (!path.empty())
                active.insert(path);
        jobs.clear();
        pending.clear();
        if (!inFlight.empty() && inFlightGeneration == generation)
            pending.insert(inFlight);
        for (auto &path : wanted)
            if (!path.empty() && !cache.count(path) && pending.insert(path).second)
                jobs.push_back({path, generation, maxEdge});
        cv.notify_one();
    }
    CacheEntry get(const std::string &path) {
        std::lock_guard<std::mutex> lock(mu);
        auto it = cache.find(path);
        if (it != cache.end()) {
            it->second.age = ++tick;
            return it->second;
        }
        if (pending.insert(path).second) {
            jobs.push_front({path, generation, maxEdge});
            cv.notify_one();
        }
        return {};
    }
    bool contains(const std::string &path) {
        std::lock_guard<std::mutex> lock(mu);
        return cache.count(path) != 0;
    }
    size_t queued() {
        std::lock_guard<std::mutex> lock(mu);
        return pending.size();
    }
};

struct ScanResult {
    uint64_t generation = 0;
    std::vector<ic::Folder> folders;
    std::vector<ic::Row> rows;
    std::wstring warning, error;
    int added = 0;
};
class FolderScanner {
    struct Request {
        uint64_t generation;
        std::vector<ic::Folder> base;
        std::vector<std::wstring> paths;
        bool byName, refresh;
        std::shared_ptr<std::atomic<bool>> cancelled;
    };
    HWND hwnd;
    std::mutex mu;
    std::condition_variable cv;
    bool stop = false;
    std::optional<Request> queued;
    std::optional<ScanResult> ready;
    std::shared_ptr<std::atomic<bool>> activeCancel;
    std::thread worker;
    void run() {
        for (;;) {
            std::optional<Request> request;
            {
                std::unique_lock<std::mutex> lock(mu);
                cv.wait(lock, [&] { return stop || queued.has_value(); });
                if (stop)
                    return;
                request = std::move(queued);
                queued.reset();
            }
            auto &r = *request;
            ScanResult result;
            result.generation = r.generation;
            try {
                std::map<std::string, ic::Folder> existing;
                for (auto &folder : r.base)
                    existing.emplace(platform::pathKey(fs::path(platform::wide(folder.path))),
                                     std::move(folder));
                std::set<std::string> seen;
                ic::Model model;
                model.byName = r.byName;
                for (auto &path : r.paths) {
                    if (r.cancelled->load())
                        break;
                    auto absolute = fs::absolute(fs::path(path)).lexically_normal();
                    auto key = platform::pathKey(absolute);
                    if (!seen.insert(key).second)
                        continue;
                    auto old = existing.find(key);
                    bool found = old != existing.end();
                    ic::Folder folder =
                        found ? std::move(old->second) : ic::Folder{platform::utf8(absolute.wstring()), {}};
                    if (!found || r.refresh) {
                        try {
                            folder.files = platform::scan(folder.path, r.cancelled.get());
                            if (!found)
                                ++result.added;
                        } catch (const std::exception &) {
                            result.warning += absolute.wstring() + L"\n";
                            if (!found)
                                continue;
                        }
                    }
                    model.folders.push_back(std::move(folder));
                }
                if (r.cancelled->load())
                    continue;
                model.rebuild();
                result.folders = std::move(model.folders);
                result.rows = std::move(model.rows);
            } catch (const std::exception &e) {
                result.error = platform::wide(e.what());
            }
            {
                std::lock_guard<std::mutex> lock(mu);
                if (stop || r.cancelled->load())
                    continue;
                ready = std::move(result);
            }
            PostMessageW(hwnd, WM_APP + 4, 0, 0);
        }
    }

  public:
    explicit FolderScanner(HWND w) : hwnd(w) {
        worker = std::thread([this] { run(); });
    }
    ~FolderScanner() {
        {
            std::lock_guard<std::mutex> lock(mu);
            stop = true;
            if (activeCancel)
                *activeCancel = true;
        }
        cv.notify_one();
        if (worker.joinable())
            worker.join();
    }
    void cancel() {
        std::lock_guard<std::mutex> lock(mu);
        if (activeCancel)
            *activeCancel = true;
        queued.reset();
        ready.reset();
    }
    void submit(uint64_t generation, std::vector<ic::Folder> base, std::vector<std::wstring> paths,
                bool byName, bool refresh) {
        std::lock_guard<std::mutex> lock(mu);
        if (activeCancel)
            *activeCancel = true;
        activeCancel = std::make_shared<std::atomic<bool>>(false);
        ready.reset();
        queued = Request{generation, std::move(base), std::move(paths), byName, refresh, activeCancel};
        cv.notify_one();
    }
    std::optional<ScanResult> take() {
        std::lock_guard<std::mutex> lock(mu);
        auto value = std::move(ready);
        ready.reset();
        return value;
    }
};
