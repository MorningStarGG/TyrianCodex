"""Public, stdlib-only validation of generated data and its completion manifest."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import shutil
import struct
from pathlib import Path

MANIFEST = "build_manifest.json"
EXPECTED_POIS = {62: 28, 1165: 7, 1195: 22, 18: 21, 326: 24, 139: 19, 91: 19, 1052: 26}
TEXTURE_PACKS = json.loads((Path(__file__).resolve().parent / "cmake/texture_packs.json").read_text(encoding="utf-8"))
REQUIRED_PACKS = set(TEXTURE_PACKS)
LOOSE_PACK_DIRS = {pack["sourceDir"] for pack in TEXTURE_PACKS.values()}


def bundle_files(data):
    """The shipped inventory excludes loose image inputs represented by texture packs."""
    for path in sorted(data.rglob("*")):
        if not path.is_file() or path.name == MANIFEST:
            continue
        relative = path.relative_to(data)
        if len(relative.parts) >= 2 and relative.parts[0] == "textures" and relative.parts[1] in LOOSE_PACK_DIRS:
            continue
        yield path


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def pack_index(path: Path):
    """Validate a TCPK archive without loading its image blobs into memory."""
    size = path.stat().st_size
    with path.open("rb") as stream:
        header = stream.read(12)
        if len(header) != 12 or header[:4] != b"TCPK":
            raise ValueError(f"Not a TCPK archive (possibly an LFS pointer): {path}")
        version, count = struct.unpack("<II", header[4:])
        if version != 1 or not 0 < count <= (size - 12) // 21:
            raise ValueError(f"Invalid TCPK header: {path}")
        entries = {}
        try:
            for _ in range(count):
                length, = struct.unpack("<I", stream.read(4))
                if not 0 < length <= 4096:
                    raise ValueError("invalid key length")
                key = stream.read(length).decode("utf-8")
                offset, length = struct.unpack("<QQ", stream.read(16))
                if key in entries or length <= 0 or offset + length > size:
                    raise ValueError("duplicate key or invalid blob range")
                entries[key] = (offset, length)
            if any(offset < stream.tell() for offset, _ in entries.values()):
                raise ValueError("blob overlaps the archive index")
        except (ValueError, struct.error, UnicodeDecodeError) as error:
            raise ValueError(f"Malformed TCPK index in {path}: {error}") from error
    return entries


def clean_pack_inputs(data: Path, packs=None):
    """Remove known build inputs only after every replacement pack is verified."""
    data = data.resolve()
    textures = (data / "textures").resolve()
    if not textures.is_relative_to(data):
        raise ValueError(f"Texture directory escapes data directory: {textures}")
    sources = []
    for name in sorted(REQUIRED_PACKS if packs is None else packs):
        source = textures / TEXTURE_PACKS[name]["sourceDir"]
        if not source.exists():
            continue
        if source.is_symlink() or source.resolve() != source or not source.is_dir():
            raise ValueError(f"Unsafe pack source directory: {source}")
        pack_index(textures / (name + ".pack"))
        sources.append(source)
    # Validate the entire selection before deleting any source directory.
    for source in sources:
        shutil.rmtree(source)
    return sources


def objective_snapshot(data):
    objectives = {}
    for path in sorted((data / "zones").glob("*.json")):
        if path.name == "index.json" or path.name.endswith(".trail.json"):
            continue
        zone = json.loads(path.read_text(encoding="utf-8"))
        for step in zone.get("steps", []):
            objectives[step["stepId"]] = {"mapId": zone["zone"]["mapId"], "name": step.get("name", ""), "type": step["type"]}
    return objectives


def compare_objectives(before, after):
    lost = sorted(set(before) - set(after))
    added = [{"stepId": sid, **after[sid]} for sid in sorted(set(after) - set(before))]
    return {"previousObjectives": len(before), "objectives": len(after), "lostStepIds": lost, "added": added}


def validate_data(data: Path, current_content=False):
    zones = {}
    for path in sorted((data / "zones").glob("*.json")):
        if path.name == "index.json" or path.name.endswith(".trail.json"):
            continue
        raw = json.loads(path.read_text(encoding="utf-8"))
        mid = raw.get("zone", {}).get("mapId")
        if not mid:
            raise ValueError(f"Missing map identity: {path}")
        steps = raw.get("steps", [])
        identities = [s["stepId"] for s in steps]
        if len(identities) != len(set(identities)):
            raise ValueError(f"Duplicate objective identity: {path}")
        if raw.get("hasTrail"):
            side = path.with_name(path.stem + ".trail.json")
            geom = json.loads(side.read_text(encoding="utf-8")) if side.exists() else raw
            if len(geom.get("trail", [])) < 2:
                raise ValueError(f"Advertised route missing geometry: {path}")
            for route in geom.get("routeVariants", []):
                count = len(route.get("trail", []))
                for section in route.get("sections", route.get("trailSections", [])):
                    start, end = (section["start"], section["end"]) if isinstance(section, dict) else section[:2]
                    if not 0 <= start <= end < count:
                        raise ValueError(f"Invalid route section: {path}")
        zones[mid] = raw
    hearts = json.loads((data / "heart_tasks.json").read_text(encoding="utf-8"))
    tasks = hearts.get("tasksByStepId", {})
    known_steps = {s["stepId"] for z in zones.values() for s in z.get("steps", [])}
    for sid, heart in tasks.items():
        if sid not in known_steps:
            raise ValueError(f"Heart data has no objective: {sid}")
        for field in ("actions", "targets", "details", "anchors"):
            ids = [row["id"] for row in heart.get(field, [])]
            if len(ids) != len(set(ids)):
                raise ValueError(f"Duplicate heart {field} identity: {sid}")
        action_ids = {a["id"] for a in heart.get("actions", [])}
        detail_ids = {d["id"] for d in heart.get("details", [])}
        target_ids = {t["id"] for t in heart.get("targets", [])}
        for anchor in heart.get("anchors", []):
            if anchor.get("actionId") and anchor["actionId"] not in action_ids:
                raise ValueError(f"Unresolved heart action: {sid}")
            if not set(anchor.get("actionIds", [])).issubset(action_ids):
                raise ValueError(f"Unresolved heart overview action: {sid}")
            if not set(anchor.get("detailIds", [])).issubset(detail_ids):
                raise ValueError(f"Unresolved heart detail: {sid}")
            if len(anchor.get("worldPos", [])) != 3:
                raise ValueError(f"Invalid heart position: {sid}")
            if not all(math.isfinite(value) for value in anchor["worldPos"]):
                raise ValueError(f"Non-finite heart position: {sid}")
            if anchor.get("targetId") and anchor["targetId"] not in target_ids:
                raise ValueError(f"Unresolved heart target: {sid}")
    if current_content:
        pack_keys = {}
        for name in REQUIRED_PACKS:
            if not (data / "textures" / (name + ".pack")).is_file():
                raise ValueError(f"Missing required texture pack: {name}")
        for path in sorted((data / "textures").glob("*.pack")):
            pack_keys[path.stem] = pack_index(path)
        for mid, minimum in EXPECTED_POIS.items():
            count = sum(s["type"] == "poi" for s in zones.get(mid, {}).get("steps", []))
            if count < minimum:
                raise ValueError(f"Map {mid}: only {count}/{minimum} expected POIs")
        if 1633 not in zones or zones[1633]["zone"].get("release") != "voe":
            raise ValueError("Leyspring Hollows is missing or misclassified")
        maps = json.loads((data / "maps/index.json").read_text(encoding="utf-8"))
        places = {m["mapId"]: m for m in maps.get("maps", []) if m.get("mapId")}
        for mid, kind in ((1633, "OpenWorld"), (1638, "Raid"), (1627, "Convergence"), (1629, "Convergence")):
            place = places.get(mid, {})
            if place.get("kind") != kind or place.get("release") != "voe":
                raise ValueError(f"Code of Creation map missing or misclassified: {mid}")
            if kind != "OpenWorld" and place.get("trackingAvailability") != "unavailable":
                raise ValueError(f"Unsupported encounter tracking must be explicit: {mid}")
            if place.get("staticMap") and place["staticMap"] not in pack_keys["maps"]:
                raise ValueError(f"Supported static map asset missing: {mid}")
        content = json.loads((data / "content.json").read_text(encoding="utf-8"))
        nexus = content.get("raids", {}).get("nexus_of_eternity", {})
        if nexus.get("tracking") != "unavailable" or nexus.get("apiId") or nexus.get("wings"):
            raise ValueError("Nexus of Eternity must not advertise invented API encounters")
        screens = json.loads((data / "loading_screens.json").read_text(encoding="utf-8"))
        if not any(1633 in s.get("mapIds", []) and Path(s["relPath"]).name in pack_keys["loadingscreens"]
                   for s in screens.get("screens", [])):
            raise ValueError("Leyspring Hollows loading screen is missing")
        lattice = tasks.get("54:heart:308", {})
        answer_rows = [row for d in lattice.get("details", []) for row in d.get("rows", [])]
        answer_text = json.dumps(answer_rows)
        if len(answer_rows) < 20 or "22.04" not in answer_text or "16,128" not in answer_text.replace(" ", ""):
            raise ValueError("Lattice Configurator answer coverage is incomplete")
        headstones = tasks.get("26:heart:5", {})
        answers = {row[0]: row[1] for detail in headstones.get("details", []) for row in detail.get("rows", []) if len(row) >= 2}
        expected_answers = {"Alesia": "Second", "Hagen": "First", "Hagnon Warblade": "Third", "Hoknil the Lesser": "First",
                            "Little Thom": "Third", "Makar Thoughtslayer": "Third", "Merin Trollsbane": "Second",
                            "Reyna": "First", "Tharn Stonerift": "First", "Volsung": "Second"}
        if any(answers.get(name, "").casefold().rstrip(".") != (line + " line").casefold() for name, line in expected_answers.items()):
            raise ValueError("Dwarven Headstone answer notes are missing")
        headstone_targets = {t["id"] for t in headstones.get("targets", []) if t.get("title") == "Dwarven Headstone"}
        answer_anchors = [a for a in headstones.get("anchors", []) if a.get("targetId") in headstone_targets and a.get("detailIds")]
        if len(answer_anchors) < 10:
            raise ValueError("Dwarven Headstone answer notes are not attached to all world hints")
        rotations = json.loads((data / "rotations.json").read_text(encoding="utf-8"))
        if "Solitary Throne" not in json.dumps(rotations):
            raise ValueError("Solitary Throne rotations are missing")
        scale_names = rotations.get("fractalsRecommended", {}).get("scaleNames", {})
        if any(scale_names.get(str(scale)) != "Solitary Throne" for scale in (19, 44, 64, 94)):
            raise ValueError("Solitary Throne scale mapping is incorrect")
        stories = json.loads((data / "stories.json").read_text(encoding="utf-8"))
        episodes = {e["storyId"]: e for e in stories.get("releases", {}).get("voe", {}).get("episodes", [])}
        if not {177, 180, 184, 183, 182, 181}.issubset(episodes):
            raise ValueError("Code of Creation story chapters are missing")
        if not all(episodes[sid].get("achievementIds") for sid in (177, 180, 184, 183, 182, 181)):
            raise ValueError("Code of Creation story completion associations are missing")
        journal = json.loads((data / "journal_locations.json").read_text(encoding="utf-8"))
        if not all(str(sid) in journal.get("stories", {}) for sid in (177, 180, 184, 183, 182, 181)):
            raise ValueError("Code of Creation story locations are missing")
        timers = json.loads((data / "event_timers.json").read_text(encoding="utf-8"))
        if not any(e["name"] == "Depths of Cruelty" for e in timers["events"]):
            raise ValueError("Depths of Cruelty schedule is missing")
        if not timers.get("scheduleCoverage"):
            raise ValueError("Event schedule coverage was not audited")
        if len(tasks) < 395:
            raise ValueError("Heart coverage regressed below the original 395-heart bundle")
    return {"zones": len(zones), "hearts": len(tasks), "chips": sum(len(h.get("chips", [])) for h in tasks.values())}


def validate_manifest(data: Path, version=None):
    path = data / MANIFEST
    if not path.exists():
        raise ValueError("No completion manifest; run the validated builder workflow")
    manifest = json.loads(path.read_text(encoding="utf-8"))
    if manifest.get("complete") is not True or not manifest.get("files"):
        raise ValueError("Incomplete build manifest")
    if version is not None and manifest.get("dataVersion") != version:
        raise ValueError("Build manifest does not match TC_DATA_VERSION")
    actual = {p.relative_to(data).as_posix() for p in bundle_files(data)}
    if actual != set(manifest["files"]):
        raise ValueError("Bundle file inventory changed after validation")
    for relative, expected in manifest["files"].items():
        path = data / relative
        if not path.resolve().is_relative_to(data.resolve()) or digest(path) != expected:
            raise ValueError(f"Bundle changed after validation: {relative}")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data", type=Path)
    parser.add_argument("--manifest", action="store_true")
    parser.add_argument("--version")
    parser.add_argument("--current-content", action="store_true")
    parser.add_argument("--clean-pack-inputs", action="store_true", help="Remove loose pack sources after bundle and manifest validation.")
    args = parser.parse_args()
    try:
        counts = validate_data(args.data, args.current_content)
        if args.manifest or args.clean_pack_inputs: validate_manifest(args.data, args.version)
        if args.clean_pack_inputs:
            print(f"Removed {len(clean_pack_inputs(args.data))} loose pack source directories")
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f"Bundle validation failed: {error}\n")
    print("Bundle validation OK:", counts)


if __name__ == "__main__":
    main()
