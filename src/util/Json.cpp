#include "util/Json.h"
#include <fstream>
#include <filesystem>

namespace Json
{
    void LoadStoriesKeyed(const std::string& path, const std::function<void(int, const nlohmann::json&)>& fn)
    {
        std::ifstream f(path);
        if (!f) return;
        nlohmann::json j;
        try { f >> j; } catch (...) { return; }
        auto sit = j.find("stories");
        if (sit == j.end() || !sit->is_object()) return;
        for (auto e = sit->begin(); e != sit->end(); ++e)
        {
            int id; try { id = std::stoi(e.key()); } catch (...) { continue; }   // skip "_about" etc.
            fn(id, e.value());
        }
    }

    void WriteAtomic(const std::string& path, const std::string& bytes)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(fs::path(path).parent_path(), ec);
        const std::string tmp = path + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary);
            if (!f) return;
            f.write(bytes.data(), (std::streamsize)bytes.size());
        }
        fs::rename(tmp, path, ec);
        if (ec)
        {
            // Windows can't rename onto an existing file -> remove the target then retry.
            fs::remove(path, ec);
            fs::rename(tmp, path, ec);
            if (ec) fs::remove(tmp, ec);   // double-failure: don't leave a stray .tmp behind
        }
    }
}
