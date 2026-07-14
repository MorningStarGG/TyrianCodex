#include "util/TexPack.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstring>
#include <utility>

namespace
{
    // The pack is little-endian (writer = the Python builder on x64); read scalars with memcpy so we never
    // depend on alignment of the mapped bytes.
    std::uint32_t Rd32(const unsigned char* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
    std::uint64_t Rd64(const unsigned char* p) { std::uint64_t v; std::memcpy(&v, p, 8); return v; }
}

bool TexPack::ForEach(const void* bytes, size_t size,
                      const std::function<void(const std::string&, const void*, size_t)>& fn)
{
    const unsigned char* base = (const unsigned char*)bytes;
    if (!base || size < 12 || std::memcmp(base, "TCPK", 4) != 0) return false;   // "Tyrian Codex Pack"
    // const std::uint32_t version = Rd32(base + 4);   // only v1 today; reserved for forward compat
    const std::uint32_t count = Rd32(base + 8);
    std::uint64_t p = 12;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (p + 4 > size) return false;
        const std::uint32_t keyLen = Rd32(base + p); p += 4;
        if (p + (std::uint64_t)keyLen + 16 > size) return false;
        std::string key((const char*)(base + p), keyLen); p += keyLen;
        const std::uint64_t off = Rd64(base + p); p += 8;
        const std::uint64_t len = Rd64(base + p); p += 8;
        if (off > size || len > size - off || off < p) return false;   // slice within the file, past the index (terms split so off+len can't overflow u64)
        fn(key, base + off, (size_t)len);
    }
    return true;
}

bool TexPack::Open(const std::string& path)
{
    if (base_.load(std::memory_order_acquire)) return true;   // fast path: already mapped -- open-once, never re-mapped

    std::lock_guard<std::mutex> lk(openMtx_);                  // serialize concurrent first-opens of THIS pack
    if (base_.load(std::memory_order_acquire)) return true;   // re-check under the lock (another thread just opened it)

    HANDLE hf = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(hf, &sz) || sz.QuadPart < 12) { CloseHandle(hf); return false; }
    const std::uint64_t fsize = (std::uint64_t)sz.QuadPart;

    HANDLE hm = CreateFileMappingA(hf, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!hm) { CloseHandle(hf); return false; }
    const unsigned char* base = (const unsigned char*)MapViewOfFile(hm, FILE_MAP_READ, 0, 0, 0);
    if (!base) { CloseHandle(hm); CloseHandle(hf); return false; }

    std::unordered_map<std::string, Loc> idx;
    const bool ok = ForEach(base, (size_t)fsize, [&](const std::string& key, const void* data, size_t len) {
        idx.emplace(key, Loc{ (std::uint64_t)((const unsigned char*)data - base), (std::uint64_t)len });
    });
    if (!ok) { UnmapViewOfFile(base); CloseHandle(hm); CloseHandle(hf); return false; }

    // Build everything, THEN publish base_ last with a release store (see the header): a concurrent reader sees
    // either null (skips) or a fully-built pack. index_ is written-once here and never mutated after.
    fileHandle_ = hf; mapHandle_ = hm; size_ = fsize; index_ = std::move(idx);
    base_.store(base, std::memory_order_release);
    return true;
}

TexPack::Span TexPack::Get(const std::string& key) const
{
    Span s;
    const unsigned char* b = base_.load(std::memory_order_acquire);
    if (!b) return s;
    auto it = index_.find(key);
    if (it == index_.end()) return s;
    s.data = b + it->second.off;
    s.size = (size_t)it->second.len;
    return s;
}
