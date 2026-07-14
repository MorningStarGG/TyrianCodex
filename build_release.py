"""Assemble the RELEASE data assets for the DLL's self-download bootstrap (src/app/DataBootstrap.cpp).

Nexus's addon updater ships ONLY the DLL, so the ~600 MB data/ folder is fetched at runtime from the GitHub
Release tagged v<TC_DATA_VERSION>. This produces the two asset tiers the DLL downloads:

  * tc-core.tcpk  -- ONE TCPK archive (the addon's own pack format, src/util/TexPack) of ALL non-pack data: guide
                     datasets, fonts, loose UI/marker art, every json -- keyed by RELATIVE PATH (forward slashes).
                     The DLL downloads + extracts this (BLOCKING) on a fresh install / data-version bump.
  * the *.pack files -- the mmap'd image packs (items/tiles/maps/portraits/decorations/cats/skins/
                     cosmetic_renders/cosmetic_icons/loadingscreens), downloaded in the BACKGROUND. Copied verbatim.

This is PUBLIC + self-contained (stdlib only, no builder/ dependency): it packages the COMMITTED data/ (the .pack
files are committed), so it runs identically on a dev box and on the GitHub CI runner. It does NOT (re)build the
image packs -- that's builder/build_texpacks.py, run privately before committing updated data.

Run from the repo root:  python build_release.py [out_dir]   (default out_dir = ./release).
Upload every file it emits to the GitHub Release tagged v<TC_DATA_VERSION> (matches the DLL's download URL).
"""

from __future__ import annotations

import hashlib
import re
import shutil
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DATA = ROOT / "data"
MAGIC = b"TCPK"
VERSION = 1
LFS_POINTER_PREFIX = b"version https://git-lfs.github.com/spec"

# Loose texture dirs that are REDUNDANT with a shipped .pack -> NEVER go in core (they'd double the download).
# data/textures/ui + data/textures/markers are NOT here: they're loose-only (no pack) and belong in core.
LOOSE_PACK_DIRS = {"items", "tiles", "maps", "portraits", "decorations", "cats", "skins",
                   "cosmetic_icons", "cosmetic_renders", "loading"}


def write_pack(out_path: Path, entries: list[tuple[str, bytes]]) -> int:
    """entries = [(key, data), ...]. Little-endian TCPK, byte-identical blobs de-duplicated. Mirrors
    builder/build_texpacks.write_pack + src/util/TexPack.cpp. Returns total file size."""
    header = len(MAGIC) + 4 + 4
    index_size = sum(4 + len(k.encode("utf-8")) + 16 for k, _ in entries)
    data_start = header + index_size

    idx = bytearray()
    blob = bytearray()
    seen: dict[bytes, int] = {}
    off = data_start
    for k, d in entries:
        kb = k.encode("utf-8")
        h = hashlib.sha1(d).digest()
        doff = seen.get(h)
        if doff is None:
            doff = off
            seen[h] = off
            blob += d
            off += len(d)
        idx += struct.pack("<I", len(kb)) + kb + struct.pack("<QQ", doff, len(d))

    tmp = out_path.with_name(out_path.name + ".tmp")
    with open(tmp, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<II", VERSION, len(entries)))
        f.write(idx)
        f.write(blob)
    tmp.replace(out_path)
    return off


def selftest(out_path: Path, entries: list[tuple[str, bytes]]) -> None:
    raw = out_path.read_bytes()
    assert raw[:4] == MAGIC, "bad magic"
    _ver, count = struct.unpack_from("<II", raw, 4)
    p, back = 12, {}
    for _ in range(count):
        (klen,) = struct.unpack_from("<I", raw, p); p += 4
        key = raw[p:p + klen].decode("utf-8"); p += klen
        off, size = struct.unpack_from("<QQ", raw, p); p += 16
        back[key] = raw[off:off + size]
    if len(back) != len(entries):
        raise SystemExit(f"self-test FAILED: {len(back)} keys != {len(entries)}")
    for k, d in entries:
        if back.get(k) != d:
            raise SystemExit(f"self-test FAILED: key {k!r} mismatch")


def validate_pack(path: Path) -> None:
    """Fail early if a release asset is an LFS pointer or not one of our TCPK packs."""
    size = path.stat().st_size
    with open(path, "rb") as f:
        head = f.read(128)
    if head.startswith(LFS_POINTER_PREFIX):
        raise SystemExit(f"{path} is a Git LFS pointer, not the real .pack file. Run `git lfs pull`.")
    if len(head) < 12 or head[:4] != MAGIC:
        raise SystemExit(f"{path} is not a TCPK pack.")
    ver, count = struct.unpack_from("<II", head, 4)
    if ver != VERSION:
        raise SystemExit(f"{path} has unsupported TCPK version {ver}.")
    if count <= 0:
        raise SystemExit(f"{path} has no entries.")
    if size < 12:
        raise SystemExit(f"{path} is too small to be a valid pack.")


def data_version() -> str:
    txt = (ROOT / "src" / "Version.h").read_text(encoding="utf-8")
    m = re.search(r'#define\s+TC_DATA_VERSION\s+"([^"]+)"', txt)
    return m.group(1) if m else "?"


def core_entries() -> list[tuple[str, bytes]]:
    """Every file under data/ EXCEPT the image .pack files and the loose pack-source dirs -> (relpath, bytes)."""
    entries: list[tuple[str, bytes]] = []
    for p in sorted(DATA.rglob("*")):
        if not p.is_file():
            continue
        rel = p.relative_to(DATA)
        if p.suffix == ".pack":
            continue                                                  # image packs -> ASSETS tier, not core
        if len(rel.parts) >= 2 and rel.parts[0] == "textures" and rel.parts[1] in LOOSE_PACK_DIRS:
            continue                                                  # loose duplicate of a pack -> skip
        entries.append((rel.as_posix(), p.read_bytes()))             # forward-slash key == the runtime extractor path
    return entries


def main(argv: list[str]) -> int:
    out = Path(argv[0]) if argv else (ROOT / "release")
    out.mkdir(parents=True, exist_ok=True)
    ver = data_version()
    print(f"Building release data assets for TC_DATA_VERSION = {ver} -> {out}\n")

    entries = core_entries()
    core = out / "tc-core.tcpk"
    total = write_pack(core, entries)
    selftest(core, entries)
    print(f"tc-core.tcpk: {len(entries)} files, {total / (1024 * 1024):.1f} MB (self-test OK)")

    packs = sorted((DATA / "textures").glob("*.pack"))
    if not packs:
        raise SystemExit("no data/textures/*.pack found -- run builder/build_texpacks.py first (packs are committed).")
    for pk in packs:
        validate_pack(pk)
        shutil.copy2(pk, out / pk.name)
    ptotal = sum(pk.stat().st_size for pk in packs) / (1024 * 1024)
    print(f"copied {len(packs)} .pack files ({ptotal:.0f} MB)")

    print(f"\nRelease assets ready in {out}:  tc-core.tcpk + {len(packs)} packs")
    print(f"Upload ALL of them to the GitHub Release tagged  v{ver}  (matches the DLL's download URL).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
