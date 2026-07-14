#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

// A read-only "texture pack": one file holding many image blobs (icons or map tiles) verbatim -- NO
// recompression, the bytes are byte-identical to the loose files. It exists purely to collapse ~22.5k loose
// bundled files into a couple of blobs (no per-file AV-scan / filesystem-slack / git-blob bloat) and to seed
// GPU textures straight from memory (Textures_GetOrCreateFromMemory) with no disk round-trip.
//
// Layout (little-endian; the addon is x64-only):
//   magic   "TCPK" (4)
//   version u32
//   count   u32
//   index   count * { keyLen:u32, key:bytes, dataOffset:u64, dataSize:u64 }   (offset = absolute file offset)
//   data    concatenated blobs
//
// The file is memory-MAPPED and the mapping is opened ONCE and never closed for the process lifetime: the
// pointers handed out by Get() must stay valid until Nexus has finished decoding the texture (which can be a
// later frame), and reusing the single mapping across addon enable/disable cycles avoids leaking a handle per
// cycle. The OS pages in only the slices actually touched, so an unviewed 189 MB icon pack costs no real RAM.
class TexPack
{
public:
    struct Span { const void* data = nullptr; size_t size = 0; };

    // Map `path` and parse its index. Idempotent: a second call on an already-open pack is a no-op that
    // returns true. Returns false (and leaves the pack closed) if the file is absent, short, or corrupt --
    // callers then fall back to the loose-file / download path. THREAD-SAFE for readers vs a concurrent first
    // Open: index_ is built fully, then base_ is published LAST with a release store; IsOpen/Has/Get acquire-load
    // base_, so a reader sees EITHER null (not-yet-open -> skip) OR a fully-built, immutable pack. Lets the data
    // bootstrap Open() a freshly-downloaded pack live while image workers read other packs (DataBootstrap).
    // Two concurrent Open() calls on the SAME pack are serialized by openMtx_ (double-checked) so they can't
    // both map the file + leak a handle; readers never touch the mutex (they acquire-load base_).
    bool Open(const std::string& path);
    bool IsOpen() const { return base_.load(std::memory_order_acquire) != nullptr; }
    bool Has(const std::string& key) const
    { return base_.load(std::memory_order_acquire) != nullptr && index_.find(key) != index_.end(); }
    // Bytes for `key` (pointer into the mapping, valid for process life). {nullptr,0} if absent / not open.
    Span Get(const std::string& key) const;
    size_t Count() const { return index_.size(); }

    // Iterate a TCPK blob held in memory (NOT a mapped file), invoking `fn(key, dataPtr, dataLen)` per entry with
    // full bounds validation. Returns false on bad magic / short / corrupt (fn may have run for earlier entries).
    // The ONE parser for the format -- reused by Open (mmap) AND the core-archive extractor (DataBootstrap), so
    // the reader and the on-disk extractor can never drift.
    static bool ForEach(const void* bytes, size_t size,
                        const std::function<void(const std::string& key, const void* data, size_t len)>& fn);

private:
    void*                             fileHandle_ = nullptr;   // HANDLE (kept so the mapping stays backed)
    void*                             mapHandle_  = nullptr;   // HANDLE
    std::atomic<const unsigned char*> base_{ nullptr };        // mapped view base -- published LAST (release store)
    std::uint64_t                     size_       = 0;
    std::mutex                        openMtx_;                // serializes concurrent first-Open() of this pack
    struct Loc { std::uint64_t off, len; };
    std::unordered_map<std::string, Loc> index_;
};
