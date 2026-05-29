#!/usr/bin/env python3
"""Convert progressive yuv422p10le RAW still fixtures to RGB OpenEXR.

Stage 1 migration utility:
- Enumerates RAW fixtures under resources/assets/*/stills/raw.
- Infers raster from file size for supported PAL/NTSC dimensions.
- Converts YCbCr (BT.601 studio-domain codes) to unclamped RGB float.
- Writes ZIP-compressed OpenEXR files with required videosynth metadata.
- Verifies decode round-trip exactness against conversion output.
- Emits a deterministic machine-readable JSON manifest.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

import numpy as np


SUPPORTED_RASTERS: Tuple[Tuple[int, int], ...] = (
    (720, 576),
    (704, 576),
    (720, 480),
    (704, 480),
)

STANDARD_HINT_BY_RASTER: Dict[Tuple[int, int], str] = {
    (720, 576): "PAL",
    (704, 576): "PAL",
    (720, 480): "NTSC",
    (704, 480): "NTSC",
}

REQUIRED_METADATA = {
    "videosynth.source_pixel_format": "yuv422p10le",
    "videosynth.source_sampling": "422_to_444_expanded",
    "videosynth.color_model": "rgb",
    "videosynth.color_primaries": "bt601",
    "videosynth.transfer": "bt601",
    "videosynth.matrix": "bt601_ycbcr_to_rgb",
    "videosynth.code_range": "studio",
}


@dataclass(frozen=True)
class Raster:
    width: int
    height: int


@dataclass
class ConversionResult:
    input_path: Path
    output_path: Path
    width: int
    height: int
    rgb_min: Dict[str, float]
    rgb_max: Dict[str, float]
    outside_nominal_display_range: bool
    input_sha256: str
    output_sha256: str


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def ensure_tool_available(tool_name: str) -> None:
    if shutil.which(tool_name) is None:
        raise RuntimeError(
            f"Required tool '{tool_name}' not found in PATH. "
            f"Run via nix shell with the required packages."
        )


def infer_raster_from_file_size(file_size_bytes: int) -> Raster:
    candidates: List[Raster] = []
    for width, height in SUPPORTED_RASTERS:
        # yuv422p10le packed as Y0 Cb Y1 Cr with 16-bit words.
        expected_size = width * height * 4
        if expected_size == file_size_bytes:
            candidates.append(Raster(width=width, height=height))

    if len(candidates) == 1:
        return candidates[0]

    if len(candidates) == 0:
        raise ValueError(
            f"RAW file size {file_size_bytes} bytes does not match supported rasters."
        )

    raise ValueError(
        f"RAW file size {file_size_bytes} bytes maps to multiple rasters: {candidates}"
    )


def decode_raw_ycbcr444(raw_path: Path, raster: Raster) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    samples = np.fromfile(raw_path, dtype="<u2")
    expected_words = raster.width * raster.height * 2
    if samples.size != expected_words:
        raise ValueError(
            f"Unexpected word count in {raw_path}: got {samples.size}, expected {expected_words}."
        )

    packed = (samples & 0x03FF).reshape(raster.height, raster.width // 2, 4)

    y_plane = np.empty((raster.height, raster.width), dtype=np.float32)
    cb_plane = np.empty((raster.height, raster.width), dtype=np.float32)
    cr_plane = np.empty((raster.height, raster.width), dtype=np.float32)

    y_plane[:, 0::2] = packed[:, :, 0]
    y_plane[:, 1::2] = packed[:, :, 2]
    cb_plane[:, 0::2] = packed[:, :, 1]
    cb_plane[:, 1::2] = packed[:, :, 1]
    cr_plane[:, 0::2] = packed[:, :, 3]
    cr_plane[:, 1::2] = packed[:, :, 3]
    return y_plane, cb_plane, cr_plane


def convert_bt601_ycbcr_to_rgb(y_plane: np.ndarray, cb_plane: np.ndarray, cr_plane: np.ndarray) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    # Keep conversion unclamped to preserve studio-range excursions.
    y = (y_plane - 64.0) / 876.0
    cb = (cb_plane - 512.0) / 896.0
    cr = (cr_plane - 512.0) / 896.0

    r = y + (1.402 * cr)
    g = y - (0.344136 * cb) - (0.714136 * cr)
    b = y + (1.772 * cb)
    return r.astype(np.float32), g.astype(np.float32), b.astype(np.float32)


def pack_gbrpf32le(g: np.ndarray, b: np.ndarray, r: np.ndarray) -> bytes:
    packed = np.concatenate((g.reshape(-1), b.reshape(-1), r.reshape(-1))).astype("<f4")
    return packed.tobytes()


def run_command(command: List[str], stdin_bytes: bytes | None = None) -> bytes:
    completed = subprocess.run(
        command,
        input=stdin_bytes,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if completed.returncode != 0:
        stderr = completed.stderr.decode("utf-8", errors="replace")
        raise RuntimeError(f"Command failed ({' '.join(command)}):\n{stderr}")
    return completed.stdout


def write_exr_with_metadata(
    exr_output_path: Path,
    width: int,
    height: int,
    gbrpf32_bytes: bytes,
    standard_hint: str,
) -> None:
    with tempfile.TemporaryDirectory(prefix="videosynth-exr-convert-") as temp_dir:
        temp_dir_path = Path(temp_dir)
        raw_float_path = temp_dir_path / "frame.gbrpf32le"
        encoded_path = temp_dir_path / "encoded.exr"
        metadata_path = temp_dir_path / "metadata.exr"

        raw_float_path.write_bytes(gbrpf32_bytes)

        run_command(
            [
                "ffmpeg",
                "-v",
                "error",
                "-f",
                "rawvideo",
                "-pix_fmt",
                "gbrpf32le",
                "-s:v",
                f"{width}x{height}",
                "-i",
                str(raw_float_path),
                "-frames:v",
                "1",
                "-c:v",
                "exr",
                "-compression",
                "zip16",
                "-format",
                "float",
                "-y",
                str(encoded_path),
            ]
        )

        metadata_command = [
            "exrstdattr",
            "-string",
            "videosynth.source_pixel_format",
            "yuv422p10le",
            "-string",
            "videosynth.source_sampling",
            "422_to_444_expanded",
            "-string",
            "videosynth.color_model",
            "rgb",
            "-string",
            "videosynth.color_primaries",
            "bt601",
            "-string",
            "videosynth.transfer",
            "bt601",
            "-string",
            "videosynth.matrix",
            "bt601_ycbcr_to_rgb",
            "-string",
            "videosynth.code_range",
            "studio",
            "-string",
            "videosynth.standard_hint",
            standard_hint,
            "-int",
            "videosynth.source_width",
            str(width),
            "-int",
            "videosynth.source_height",
            str(height),
            str(encoded_path),
            str(metadata_path),
        ]
        run_command(metadata_command)

        exr_output_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(metadata_path), str(exr_output_path))


def decode_exr_to_gbrpf32_bytes(exr_path: Path) -> bytes:
    return run_command(
        [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            str(exr_path),
            "-frames:v",
            "1",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "gbrpf32le",
            "-",
        ]
    )


def read_exr_header(exr_path: Path) -> str:
    output = run_command(["exrheader", str(exr_path)])
    return output.decode("utf-8", errors="replace")


def verify_exr_output(
    exr_path: Path,
    expected_gbrpf32_bytes: bytes,
    width: int,
    height: int,
    standard_hint: str,
) -> None:
    actual = decode_exr_to_gbrpf32_bytes(exr_path)
    if len(actual) != len(expected_gbrpf32_bytes):
        raise RuntimeError(
            f"Decoded EXR byte size mismatch for {exr_path}: "
            f"got {len(actual)}, expected {len(expected_gbrpf32_bytes)}."
        )

    expected_arr = np.frombuffer(expected_gbrpf32_bytes, dtype="<f4")
    actual_arr = np.frombuffer(actual, dtype="<f4")
    if not np.array_equal(actual_arr, expected_arr):
        max_abs = float(np.max(np.abs(actual_arr - expected_arr)))
        raise RuntimeError(
            f"Decoded EXR values differ from converter output for {exr_path}. "
            f"Max abs delta: {max_abs}."
        )

    header = read_exr_header(exr_path)
    required_fragments = [
        "compression (type compression): zip",
        'R, 32-bit floating-point',
        'G, 32-bit floating-point',
        'B, 32-bit floating-point',
        f'videosynth.standard_hint (type string): "{standard_hint}"',
        f"videosynth.source_width (type int): {width}",
        f"videosynth.source_height (type int): {height}",
    ]

    for key, value in REQUIRED_METADATA.items():
        required_fragments.append(f'videosynth.{key.split("videosynth.", 1)[1]} (type string): "{value}"')

    for fragment in required_fragments:
        if fragment not in header:
            raise RuntimeError(
                f"EXR header validation failed for {exr_path}. Missing fragment: {fragment}"
            )


def discover_raw_fixtures(assets_root: Path) -> List[Path]:
    raw_files = sorted(assets_root.glob("**/stills/raw/*.raw"))
    if not raw_files:
        raise RuntimeError(f"No RAW fixtures found under {assets_root}.")
    return raw_files


def build_output_path(raw_path: Path) -> Path:
    if raw_path.parent.name != "raw":
        raise ValueError(f"Unexpected RAW fixture layout: {raw_path}")
    exr_dir = raw_path.parent.parent / "exr"
    return exr_dir / f"{raw_path.stem}.exr"


def convert_one_fixture(raw_path: Path, repo_root: Path) -> ConversionResult:
    file_size = raw_path.stat().st_size
    raster = infer_raster_from_file_size(file_size)
    standard_hint = STANDARD_HINT_BY_RASTER[(raster.width, raster.height)]

    y_plane, cb_plane, cr_plane = decode_raw_ycbcr444(raw_path, raster)
    r, g, b = convert_bt601_ycbcr_to_rgb(y_plane, cb_plane, cr_plane)

    combined = np.concatenate((r.reshape(-1), g.reshape(-1), b.reshape(-1)))
    rgb_min = {
        "R": float(np.min(r)),
        "G": float(np.min(g)),
        "B": float(np.min(b)),
    }
    rgb_max = {
        "R": float(np.max(r)),
        "G": float(np.max(g)),
        "B": float(np.max(b)),
    }

    outside_nominal = bool(np.any(combined < 0.0) or np.any(combined > 1.0))

    gbrpf32 = pack_gbrpf32le(g=g, b=b, r=r)
    output_path = build_output_path(raw_path)
    write_exr_with_metadata(
        exr_output_path=output_path,
        width=raster.width,
        height=raster.height,
        gbrpf32_bytes=gbrpf32,
        standard_hint=standard_hint,
    )

    verify_exr_output(
        exr_path=output_path,
        expected_gbrpf32_bytes=gbrpf32,
        width=raster.width,
        height=raster.height,
        standard_hint=standard_hint,
    )

    return ConversionResult(
        input_path=raw_path,
        output_path=output_path,
        width=raster.width,
        height=raster.height,
        rgb_min=rgb_min,
        rgb_max=rgb_max,
        outside_nominal_display_range=outside_nominal,
        input_sha256=sha256_file(raw_path),
        output_sha256=sha256_file(output_path),
    )


def make_manifest(
    results: Iterable[ConversionResult],
    repo_root: Path,
    channel_type: str,
    compression: str,
) -> Dict[str, object]:
    sorted_results = sorted(results, key=lambda item: str(item.input_path))
    entries = []
    for result in sorted_results:
        entries.append(
            {
                "input_path": str(result.input_path.relative_to(repo_root)),
                "output_path": str(result.output_path.relative_to(repo_root)),
                "width": result.width,
                "height": result.height,
                "rgb_min": result.rgb_min,
                "rgb_max": result.rgb_max,
                "outside_nominal_display_range": result.outside_nominal_display_range,
                "conversion": {
                    "matrix": "bt601_ycbcr_to_rgb",
                    "range_policy": "studio_no_clamp",
                    "sampling": "422_to_444_expanded",
                    "channel_type": channel_type,
                    "compression": compression,
                },
                "checksums": {
                    "input_raw_sha256": result.input_sha256,
                    "output_exr_sha256": result.output_sha256,
                },
            }
        )

    return {
        "schema_version": 1,
        "summary": {
            "fixture_count": len(entries),
            "supported_rasters": [
                {"width": width, "height": height} for width, height in SUPPORTED_RASTERS
            ],
            "source_pixel_format": "yuv422p10le",
            "target_container": "openexr_scanline",
            "target_channels": ["R", "G", "B"],
            "target_channel_type": channel_type,
            "target_compression": compression,
            "matrix": "bt601_ycbcr_to_rgb",
            "range_policy": "studio_no_clamp",
        },
        "fixtures": entries,
    }


def parse_args(argv: List[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert progressive RAW still fixtures to RGB OpenEXR with manifest output."
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="Repository root (default: inferred from script location).",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=None,
        help="Manifest path (default: <repo-root>/resources/assets/stills-exr-conversion-manifest.json)",
    )
    parser.add_argument(
        "--assets-root",
        type=Path,
        default=None,
        help="Assets root to scan (default: <repo-root>/resources/assets)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Only list candidate RAW fixtures and inferred output paths.",
    )
    return parser.parse_args(argv)


def main(argv: List[str]) -> int:
    args = parse_args(argv)

    repo_root = args.repo_root.resolve()
    assets_root = (args.assets_root or (repo_root / "resources" / "assets")).resolve()
    manifest_path = (
        args.manifest.resolve()
        if args.manifest is not None
        else (repo_root / "resources" / "assets" / "stills-exr-conversion-manifest.json")
    )

    ensure_tool_available("ffmpeg")
    ensure_tool_available("exrstdattr")
    ensure_tool_available("exrheader")

    raw_fixtures = discover_raw_fixtures(assets_root)
    if args.dry_run:
        for raw in raw_fixtures:
            out = build_output_path(raw)
            print(f"{raw.relative_to(repo_root)} -> {out.relative_to(repo_root)}")
        print(f"Fixtures discovered: {len(raw_fixtures)}")
        return 0

    results: List[ConversionResult] = []
    for raw_path in raw_fixtures:
        result = convert_one_fixture(raw_path=raw_path, repo_root=repo_root)
        results.append(result)
        print(f"Converted {raw_path.relative_to(repo_root)} -> {result.output_path.relative_to(repo_root)}")

    manifest = make_manifest(
        results=results,
        repo_root=repo_root,
        channel_type="float32",
        compression="zip16",
    )

    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(
        json.dumps(manifest, sort_keys=True, indent=2) + "\n",
        encoding="utf-8",
    )

    print(f"Wrote manifest: {manifest_path.relative_to(repo_root)}")
    print(f"Converted fixtures: {len(results)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except Exception as exc:  # pylint: disable=broad-except
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
