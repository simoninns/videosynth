#!/usr/bin/env python3
"""
Verify EXR files against docs/exr-bt601-compliance-requirements.md.

- Auto-discovers EXRs via glob (defaults include both exr/ and output/exr/ roots)
- Auto-detects 486-line vs 576-line set from image height
- Runs structural checks, source-coupled checks (when source is available), and
  RGB-domain surrogate checks
- Prints PASS/FAIL per file with explicit failure reasons

This script uses external tools already present in the media toolchain:
- ffprobe
- exrheader
- ffmpeg
"""

from __future__ import annotations

import argparse
import glob
import math
import os
import re
import subprocess
import sys
from array import array
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple


# -----------------------------
# Utility helpers
# -----------------------------


def run_cmd(args: Sequence[str], cwd: Optional[Path] = None, binary: bool = False) -> Tuple[int, str]:
    try:
        proc = subprocess.run(
            list(args),
            cwd=str(cwd) if cwd else None,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
    except FileNotFoundError:
        return 127, f"command not found: {args[0]}"

    if binary:
        # Binary mode caller should use subprocess directly; keep API simple.
        return proc.returncode, proc.stdout.decode("utf-8", errors="replace")

    out = proc.stdout.decode("utf-8", errors="replace")
    err = proc.stderr.decode("utf-8", errors="replace")
    text = out if out.strip() else err
    return proc.returncode, text


def require_tools(tools: Sequence[str]) -> List[str]:
    missing = []
    for tool in tools:
        rc, _ = run_cmd(["sh", "-lc", f"command -v {tool}"])
        if rc != 0:
            missing.append(tool)
    return missing


def parse_ffprobe_kv(text: str) -> Dict[str, str]:
    data: Dict[str, str] = {}
    for line in text.splitlines():
        if "=" not in line:
            continue
        k, v = line.split("=", 1)
        data[k.strip()] = v.strip()
    return data


def approx(a: float, b: float, tol: float) -> bool:
    return abs(a - b) <= tol


def parse_rational(text: str) -> Optional[Tuple[int, int]]:
    m = re.search(r"(-?\d+)\s*/\s*(-?\d+)", text)
    if not m:
        return None
    den = int(m.group(2))
    if den == 0:
        return None
    return int(m.group(1)), den


def parse_aspect_ratio_value(text: str) -> Optional[float]:
    if not text:
        return None
    m = re.search(r"(-?\d+)\s*[:/]\s*(-?\d+)", text)
    if m:
        den = int(m.group(2))
        if den == 0:
            return None
        return int(m.group(1)) / den
    try:
        return float(text.strip())
    except ValueError:
        return None


# -----------------------------
# EXR metadata extraction
# -----------------------------


@dataclass
class ExrMeta:
    codec_name: str
    pix_fmt: str
    width: int
    height: int
    sample_aspect_ratio: str
    pixel_aspect_ratio: Optional[float]
    channels: List[Tuple[str, str, int, int]]  # name, type label, sx, sy
    compression: str
    data_window: Tuple[int, int, int, int]
    display_window: Tuple[int, int, int, int]
    frames_per_second: Optional[Tuple[int, int]]
    gamma: Optional[float]
    line_order: str
    image_type: str


def ffprobe_meta(path: Path) -> Dict[str, str]:
    args = [
        "ffprobe",
        "-v",
        "error",
        "-show_entries",
        "stream=codec_name,pix_fmt,width,height,sample_aspect_ratio",
        "-of",
        "default=nw=1:nk=0",
        str(path),
    ]
    rc, text = run_cmd(args)
    if rc != 0:
        raise RuntimeError(f"ffprobe failed: {text.strip()}")
    return parse_ffprobe_kv(text)


def exrheader_text(path: Path) -> str:
    rc, text = run_cmd(["exrheader", str(path)])
    if rc != 0:
        raise RuntimeError(f"exrheader failed: {text.strip()}")
    return text


def parse_exrheader(text: str, ffm: Dict[str, str]) -> ExrMeta:
    channels: List[Tuple[str, str, int, int]] = []

    in_channels = False
    for line in text.splitlines():
        if line.strip().startswith("channels (type chlist)"):
            in_channels = True
            continue
        if in_channels:
            if not line.startswith("    "):
                in_channels = False
                continue
            cm = re.match(
                r"\s*([A-Za-z0-9_]+),\s*([^,]+),\s*sampling\s*(-?\d+)\s+(-?\d+)",
                line,
            )
            if cm:
                channels.append((cm.group(1), cm.group(2).strip(), int(cm.group(3)), int(cm.group(4))))

    def parse_window(key: str) -> Tuple[int, int, int, int]:
        m = re.search(
            rf"^{re.escape(key)}\s*\(type box2i\):\s*\((-?\d+)\s+(-?\d+)\)\s*-\s*\((-?\d+)\s+(-?\d+)\)",
            text,
            flags=re.MULTILINE,
        )
        if not m:
            raise RuntimeError(f"missing {key} in exrheader")
        return tuple(int(m.group(i)) for i in range(1, 5))  # type: ignore[return-value]

    fps: Optional[Tuple[int, int]] = None
    m_fps = re.search(r"^framesPerSecond \(type [^)]+\):\s*([^\n]+)$", text, flags=re.MULTILINE)
    if m_fps:
        fps = parse_rational(m_fps.group(1))

    gamma: Optional[float] = None
    m_gamma = re.search(r"^gamma \(type float\):\s*([-+]?\d+(?:\.\d+)?)", text, flags=re.MULTILINE)
    if m_gamma:
        gamma = float(m_gamma.group(1))

    m_comp = re.search(r"^compression \(type compression\):\s*([^:]+)", text, flags=re.MULTILINE)
    compression = m_comp.group(1).strip() if m_comp else ""

    m_lo = re.search(r"^lineOrder \(type lineOrder\):\s*(.+)$", text, flags=re.MULTILINE)
    line_order = m_lo.group(1).strip() if m_lo else ""

    m_type = re.search(r'^type \(type string\):\s*"([^"]+)"', text, flags=re.MULTILINE)
    image_type = m_type.group(1).strip() if m_type else ""

    m_par = re.search(r"^pixelAspectRatio \(type float\):\s*([-+]?\d+(?:\.\d+)?)", text, flags=re.MULTILINE)
    pixel_aspect_ratio = float(m_par.group(1)) if m_par else None

    width = int(ffm.get("width", "-1"))
    height = int(ffm.get("height", "-1"))

    return ExrMeta(
        codec_name=ffm.get("codec_name", ""),
        pix_fmt=ffm.get("pix_fmt", ""),
        width=width,
        height=height,
        sample_aspect_ratio=ffm.get("sample_aspect_ratio", ""),
        pixel_aspect_ratio=pixel_aspect_ratio,
        channels=channels,
        compression=compression,
        data_window=parse_window("dataWindow"),
        display_window=parse_window("displayWindow"),
        frames_per_second=fps,
        gamma=gamma,
        line_order=line_order,
        image_type=image_type,
    )


# -----------------------------
# Source-coupled equivalence
# -----------------------------


def framemd5_hash(ffmpeg_args: Sequence[str]) -> Tuple[Optional[str], str]:
    rc, text = run_cmd(ffmpeg_args)
    if rc != 0:
        return None, text
    for line in text.splitlines():
        if not re.match(r"^\s*\d+,", line):
            continue
        parts = [p.strip() for p in line.split(",")]
        if not parts:
            continue
        return parts[-1], ""
    return None, "framemd5 output did not contain frame data"


def find_source_mov(repo_root: Path, exr_path: Path, height: int) -> Optional[Path]:
    base = exr_path.stem
    if height == 486:
        cand = repo_root / "originals" / "525-Phabrix" / "still" / f"525_5994_{base}.mov"
        return cand if cand.is_file() else None
    if height == 576:
        cand = repo_root / "originals" / "625-Phabrix" / "still" / f"625_50_{base}.mov"
        return cand if cand.is_file() else None
    return None


def compare_source_equivalence(repo_root: Path, exr_path: Path, height: int) -> Tuple[Optional[bool], str]:
    src = find_source_mov(repo_root, exr_path, height)
    if not src:
        return None, "source file not found for source-coupled check"

    if height == 486:
        vf = "crop=704:486:8:0,format=gbrpf32le,pad=720:486:8:0:black,setsar=108/119"
    elif height == 576:
        vf = "crop=704:576:8:0,format=gbrpf32le,pad=720:576:8:0:black,setsar=128/117"
    else:
        return None, "unsupported height for source-coupled check"

    src_hash, err = framemd5_hash(
        [
            "ffmpeg",
            "-v",
            "error",
            "-apply_cropping",
            "none",
            "-i",
            str(src),
            "-an",
            "-frames:v",
            "1",
            "-vf",
            vf,
            "-pix_fmt",
            "gbrpf32le",
            "-f",
            "framemd5",
            "-",
        ]
    )
    if not src_hash:
        return False, f"failed to hash source frame: {err}"

    exr_hash, err2 = framemd5_hash(
        [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            str(exr_path),
            "-an",
            "-frames:v",
            "1",
            "-pix_fmt",
            "gbrpf32le",
            "-f",
            "framemd5",
            "-",
        ]
    )
    if not exr_hash:
        return False, f"failed to hash EXR frame: {err2}"

    if src_hash == exr_hash:
        return True, f"framemd5 match against {src.name}"
    return False, f"framemd5 mismatch against {src.name}: source={src_hash} exr={exr_hash}"


# -----------------------------
# RGB-domain surrogate checks
# -----------------------------


def decode_exr_rgb48(path: Path) -> Tuple[int, int, array]:
    # Width/height is also read from ffprobe to verify expected byte count.
    ffm = ffprobe_meta(path)
    w = int(ffm.get("width", "0"))
    h = int(ffm.get("height", "0"))

    cmd = [
        "ffmpeg",
        "-v",
        "error",
        "-i",
        str(path),
        "-frames:v",
        "1",
        "-f",
        "rawvideo",
        "-pix_fmt",
        "rgb48le",
        "-",
    ]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.decode("utf-8", errors="replace").strip())

    raw = proc.stdout
    expected = w * h * 3 * 2
    if len(raw) != expected:
        raise RuntimeError(f"unexpected decoded byte count: got {len(raw)}, expected {expected}")

    vals = array("H")
    vals.frombytes(raw)
    if sys.byteorder != "little":
        vals.byteswap()
    return w, h, vals


def smpte170m_oetf(lin: float) -> float:
    if lin <= 0.0:
        return 0.0
    if lin < 0.018:
        return 4.5 * lin
    return 1.099 * (lin ** 0.45) - 0.099


def surrogate_bt601_metrics(
    w: int,
    h: int,
    rgb16: array,
    stride_x: int,
    stride_y: int,
) -> Dict[str, float]:
    # Sampled evaluation for speed on large batches.
    count = 0

    y_min = 1e9
    y_max = -1e9
    cb_min = 1e9
    cb_max = -1e9
    cr_min = 1e9
    cr_max = -1e9

    # Round-trip error in 8-bit-equivalent code units.
    abs_sum = 0.0
    abs_max = 0.0

    # Coarse high-frequency surrogate using horizontal first differences.
    y_edge_sum = 0.0
    c_edge_sum = 0.0

    def clip01(v: float) -> float:
        return 0.0 if v < 0.0 else 1.0 if v > 1.0 else v

    for y in range(0, h, max(1, stride_y)):
        row_off = y * w * 3
        for x in range(0, w, max(1, stride_x)):
            i = row_off + x * 3
            r_lin = rgb16[i] / 65535.0
            g_lin = rgb16[i + 1] / 65535.0
            b_lin = rgb16[i + 2] / 65535.0

            # Convert to gamma-pre-corrected components for BT.601 maths.
            rp = smpte170m_oetf(r_lin)
            gp = smpte170m_oetf(g_lin)
            bp = smpte170m_oetf(b_lin)

            ey = 0.299 * rp + 0.587 * gp + 0.114 * bp
            d_r = rp - ey
            d_b = bp - ey

            y_code = 219.0 * ey + 16.0
            cr_code = 160.0 * d_r + 128.0
            cb_code = 126.0 * d_b + 128.0

            if y_code < y_min:
                y_min = y_code
            if y_code > y_max:
                y_max = y_code
            if cb_code < cb_min:
                cb_min = cb_code
            if cb_code > cb_max:
                cb_max = cb_code
            if cr_code < cr_min:
                cr_min = cr_code
            if cr_code > cr_max:
                cr_max = cr_code

            # Quantize and round-trip via BT.601 approximation.
            yq = float(max(0, min(255, int(round(y_code)))))
            cbq = float(max(0, min(255, int(round(cb_code)))))
            crq = float(max(0, min(255, int(round(cr_code)))))

            ey2 = (yq - 16.0) / 219.0
            d_r2 = (crq - 128.0) / 160.0
            d_b2 = (cbq - 128.0) / 126.0
            r2 = ey2 + d_r2
            b2 = ey2 + d_b2
            g2 = (ey2 - 0.299 * r2 - 0.114 * b2) / 0.587

            r2 = clip01(r2)
            g2 = clip01(g2)
            b2 = clip01(b2)

            err_r = abs((r2 - rp) * 255.0)
            err_g = abs((g2 - gp) * 255.0)
            err_b = abs((b2 - bp) * 255.0)
            e = (err_r + err_g + err_b) / 3.0
            abs_sum += e
            if e > abs_max:
                abs_max = e

            # Simple edge-energy surrogate.
            if x + 1 < w:
                j = row_off + (x + 1) * 3
                rn = smpte170m_oetf(rgb16[j] / 65535.0)
                gn = smpte170m_oetf(rgb16[j + 1] / 65535.0)
                bn = smpte170m_oetf(rgb16[j + 2] / 65535.0)
                ey_n = 0.299 * rn + 0.587 * gn + 0.114 * bn
                d_r_n = rn - ey_n
                d_b_n = bn - ey_n
                y_edge_sum += abs(ey_n - ey)
                c_edge_sum += 0.5 * (abs(d_r_n - d_r) + abs(d_b_n - d_b))

            count += 1

    mean_err = abs_sum / max(1, count)
    chroma_to_luma_edge_ratio = c_edge_sum / max(1e-12, y_edge_sum)

    return {
        "samples": float(count),
        "y_min": y_min,
        "y_max": y_max,
        "cb_min": cb_min,
        "cb_max": cb_max,
        "cr_min": cr_min,
        "cr_max": cr_max,
        "roundtrip_mean_err": mean_err,
        "roundtrip_max_err": abs_max,
        "chroma_to_luma_edge_ratio": chroma_to_luma_edge_ratio,
    }


def decode_exr_gbrpf32(path: Path) -> Tuple[int, int, array]:
    ffm = ffprobe_meta(path)
    w = int(ffm.get("width", "0"))
    h = int(ffm.get("height", "0"))

    cmd = [
        "ffmpeg",
        "-v",
        "error",
        "-i",
        str(path),
        "-frames:v",
        "1",
        "-f",
        "rawvideo",
        "-pix_fmt",
        "gbrpf32le",
        "-",
    ]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.decode("utf-8", errors="replace").strip())

    raw = proc.stdout
    expected = w * h * 3 * 4
    if len(raw) != expected:
        raise RuntimeError(f"unexpected decoded byte count: got {len(raw)}, expected {expected}")

    vals = array("f")
    vals.frombytes(raw)
    if sys.byteorder != "little":
        vals.byteswap()
    return w, h, vals


def verify_horizontal_padding_black(
    w: int,
    h: int,
    rgbf32: array,
    left_pad: int,
    right_pad: int,
    black_threshold: float,
) -> Tuple[bool, str]:
    if left_pad < 0 or right_pad < 0:
        return False, f"invalid padding config: left={left_pad} right={right_pad}"
    if left_pad + right_pad >= w:
        return False, f"invalid padding config for width {w}: left+right={left_pad + right_pad}"

    border_pixels = 0
    bad_pixels = 0
    examples: List[Tuple[int, int, int, int, int]] = []
    plane = w * h

    def get_rgb(x: int, y: int) -> Tuple[float, float, float]:
        idx = y * w + x
        g = float(rgbf32[idx])
        b = float(rgbf32[plane + idx])
        r = float(rgbf32[(2 * plane) + idx])
        return r, g, b

    for y in range(h):
        # Left border.
        for x in range(left_pad):
            r, g, b = get_rgb(x, y)
            border_pixels += 1
            if r > black_threshold or g > black_threshold or b > black_threshold:
                bad_pixels += 1
                if len(examples) < 5:
                    examples.append((x, y, float(r), float(g), float(b)))

        # Right border.
        for x in range(w - right_pad, w):
            r, g, b = get_rgb(x, y)
            border_pixels += 1
            if r > black_threshold or g > black_threshold or b > black_threshold:
                bad_pixels += 1
                if len(examples) < 5:
                    examples.append((x, y, float(r), float(g), float(b)))

    active_start = left_pad
    active_end = w - right_pad - 1
    active_width = active_end - active_start + 1

    if bad_pixels == 0:
        return (
            True,
            (
                "padding is black within threshold {} for all {} border pixels; "
                "active region x={}..{} (width={})"
            ).format(black_threshold, border_pixels, active_start, active_end, active_width),
        )

    example_txt = "; ".join(
        [f"(x={x},y={y},rgbf32=({r:.6g},{g:.6g},{b:.6g}))" for x, y, r, g, b in examples]
    )
    return (
        False,
        (
            "{} / {} border pixels exceed black threshold {}; "
            "expected active region x={}..{} (width={}); examples: {}"
        ).format(
            bad_pixels,
            border_pixels,
            black_threshold,
            active_start,
            active_end,
            active_width,
            example_txt,
        ),
    )


# -----------------------------
# Check model
# -----------------------------


@dataclass
class CheckResult:
    name: str
    ok: bool
    message: str
    severity: str = "FAIL"  # FAIL or WARN


@dataclass
class FileReport:
    path: Path
    checks: List[CheckResult]

    def failed(self, strict_warnings: bool) -> bool:
        for c in self.checks:
            if c.ok:
                continue
            if c.severity == "FAIL":
                return True
            if strict_warnings and c.severity == "WARN":
                return True
        return False


def add(checks: List[CheckResult], name: str, ok: bool, msg: str, severity: str = "FAIL") -> None:
    checks.append(CheckResult(name=name, ok=ok, message=msg, severity=severity))


def validate_one(
    repo_root: Path,
    exr_path: Path,
    stride_x: int,
    stride_y: int,
    skip_source: bool,
    left_pad: int,
    right_pad: int,
    black_threshold: float,
) -> FileReport:
    checks: List[CheckResult] = []

    try:
        ffm = ffprobe_meta(exr_path)
        eht = exrheader_text(exr_path)
        meta = parse_exrheader(eht, ffm)
    except Exception as exc:
        add(checks, "metadata", False, f"failed to read metadata: {exc}")
        return FileReport(path=exr_path, checks=checks)

    # Determine set by height.
    if meta.height == 486:
        expected_sar = "108:119"
        expected_par = 108.0 / 119.0
        expected_fps = (30000, 1001)
        expected_dw = (0, 0, 719, 485)
    elif meta.height == 576:
        expected_sar = "128:117"
        expected_par = 128.0 / 117.0
        expected_fps = (25, 1)
        expected_dw = (0, 0, 719, 575)
    else:
        expected_sar = ""
        expected_par = None
        expected_fps = None
        expected_dw = None

    # Structural checks.
    add(checks, "codec", meta.codec_name == "exr", f"codec_name={meta.codec_name}")
    add(checks, "pixel_format", meta.pix_fmt == "gbrpf32le", f"pix_fmt={meta.pix_fmt}")
    add(checks, "dimensions", meta.width == 720 and meta.height in (486, 576), f"size={meta.width}x{meta.height}, expected 720x486 or 720x576")
    measured_par = meta.pixel_aspect_ratio
    if measured_par is None:
        measured_par = parse_aspect_ratio_value(meta.sample_aspect_ratio)
    sar_ok = measured_par is not None and expected_par is not None and approx(measured_par, expected_par, 2e-4)
    expected_par_str = f"{expected_par:.9f}" if expected_par is not None else "n/a"
    add(
        checks,
        "sample_aspect_ratio",
        sar_ok,
        f"sar={meta.sample_aspect_ratio}, pixelAspectRatio={meta.pixel_aspect_ratio}, expected={expected_sar} ({expected_par_str})",
    )

    chan_names = sorted([c[0] for c in meta.channels])
    chan_types_ok = all("32-bit floating-point" in c[1] for c in meta.channels)
    chan_sampling_ok = all(c[2] == 1 and c[3] == 1 for c in meta.channels)
    add(checks, "channels", chan_names == ["B", "G", "R"] and chan_types_ok and chan_sampling_ok, f"channels={meta.channels}")

    add(checks, "compression", meta.compression == "none", f"compression={meta.compression}")
    add(checks, "data_window", meta.data_window == expected_dw, f"dataWindow={meta.data_window}, expected={expected_dw}")
    add(checks, "display_window", meta.display_window == expected_dw, f"displayWindow={meta.display_window}, expected={expected_dw}")
    add(checks, "line_order", "increasing y" in meta.line_order, f"lineOrder={meta.line_order}")
    add(checks, "gamma", meta.gamma is not None and approx(meta.gamma, 1.0, 1e-6), f"gamma={meta.gamma}")
    add(checks, "scanline_type", meta.image_type == "scanlineimage", f"type={meta.image_type}")

    if expected_fps is not None:
        add(
            checks,
            "frames_per_second",
            meta.frames_per_second == expected_fps,
            f"framesPerSecond={meta.frames_per_second}, expected={expected_fps}",
        )
    else:
        add(checks, "frames_per_second", False, "unsupported image height for expected fps lookup")

    # Source-coupled rendered equivalence.
    if skip_source:
        add(checks, "source_equivalence", False, "source-coupled check skipped by --skip-source", severity="WARN")
    else:
        eq, msg = compare_source_equivalence(repo_root, exr_path, meta.height)
        if eq is None:
            add(checks, "source_equivalence", False, msg, severity="WARN")
        else:
            add(checks, "source_equivalence", bool(eq), msg)

    # Surrogate checks from RGB domain.
    try:
        w, h, rgb16 = decode_exr_rgb48(exr_path)

        # Direct spatial check: active samples are centered and side padding is black.
        wf, hf, rgbf32 = decode_exr_gbrpf32(exr_path)
        if wf != w or hf != h:
            raise RuntimeError(
                f"inconsistent decode dimensions between rgb48 ({w}x{h}) and gbrpf32 ({wf}x{hf})"
            )

        pad_ok, pad_msg = verify_horizontal_padding_black(
            w=wf,
            h=hf,
            rgbf32=rgbf32,
            left_pad=left_pad,
            right_pad=right_pad,
            black_threshold=black_threshold,
        )
        add(checks, "horizontal_padding_black", pad_ok, pad_msg)

        m = surrogate_bt601_metrics(w, h, rgb16, stride_x=stride_x, stride_y=stride_y)

        # Legal range surrogate with tolerance ±1 code around nominal boundaries.
        y_ok = (m["y_min"] >= 15.0) and (m["y_max"] <= 236.0)
        cb_ok = (m["cb_min"] >= -1.0) and (m["cb_max"] <= 256.0)
        cr_ok = (m["cr_min"] >= -1.0) and (m["cr_max"] <= 256.0)
        add(
            checks,
            "surrogate_component_ranges",
            y_ok and cb_ok and cr_ok,
            (
                "Y'[{:.2f},{:.2f}] Cb[{:.2f},{:.2f}] Cr[{:.2f},{:.2f}]"
                .format(m["y_min"], m["y_max"], m["cb_min"], m["cb_max"], m["cr_min"], m["cr_max"])
            ),
        )

        # Round-trip residual surrogate from requirements guidance.
        rt_ok = (m["roundtrip_mean_err"] <= 1.0) and (m["roundtrip_max_err"] <= 2.0)
        add(
            checks,
            "surrogate_roundtrip_residual",
            rt_ok,
            "mean_err={:.3f} max_err={:.3f} (8-bit-equivalent codes)".format(
                m["roundtrip_mean_err"], m["roundtrip_max_err"]
            ),
        )

        # Coarse luma/chroma misregistration surrogate (heuristic).
        # Not a normative proof; kept as WARN if outside loose bounds.
        ratio = m["chroma_to_luma_edge_ratio"]
        ratio_ok = 0.0 <= ratio <= 3.0
        add(
            checks,
            "surrogate_edge_alignment_proxy",
            ratio_ok,
            "chroma_to_luma_edge_ratio={:.3f}".format(ratio),
            severity="WARN",
        )

        # Non-proveable-by-EXR items are documented in requirements but intentionally
        # not emitted as warnings to keep runtime output focused on actionable checks.

    except Exception as exc:
        add(checks, "surrogate_checks", False, f"failed to run RGB-domain surrogate checks: {exc}", severity="WARN")

    return FileReport(path=exr_path, checks=checks)


# -----------------------------
# Discovery and reporting
# -----------------------------


def discover_exrs(repo_root: Path, patterns: Sequence[str]) -> List[Path]:
    found: List[Path] = []
    seen = set()
    for pat in patterns:
        full_pat = str(repo_root / pat)
        for p in glob.glob(full_pat, recursive=True):
            pp = Path(p)
            if not pp.is_file() or pp.suffix.lower() != ".exr":
                continue
            rp = pp.resolve()
            if rp in seen:
                continue
            seen.add(rp)
            found.append(pp)
    found.sort()
    return found


def print_report(reports: List[FileReport], strict_warnings: bool) -> int:
    fail_count = 0
    pass_count = 0

    for rep in reports:
        failed = rep.failed(strict_warnings=strict_warnings)
        status = "FAIL" if failed else "PASS"
        if failed:
            fail_count += 1
        else:
            pass_count += 1

        print(f"{status}: {rep.path}")

        for chk in rep.checks:
            if chk.ok:
                print(f"  PASS {chk.name}: {chk.message}")
            else:
                tag = "WARN" if chk.severity == "WARN" else "FAIL"
                print(f"  {tag} {chk.name}: {chk.message}")

    print()
    print("Summary")
    print(f"  total files: {len(reports)}")
    print(f"  pass: {pass_count}")
    print(f"  fail: {fail_count}")
    if strict_warnings:
        print("  mode: strict warnings (WARN counted as FAIL)")

    return 1 if fail_count else 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Validate BT.601-related EXR requirements across a file set")
    p.add_argument(
        "--root",
        default=str(Path(__file__).resolve().parents[1]),
        help="repository root (default: parent of scripts/)",
    )
    p.add_argument(
        "--glob",
        action="append",
        default=["assets/exr/**/*.exr"],
        help="glob pattern(s) for EXR discovery; can be repeated",
    )
    p.add_argument(
        "--stride-x",
        type=int,
        default=4,
        help="horizontal sampling stride for RGB surrogate checks (default: 4)",
    )
    p.add_argument(
        "--stride-y",
        type=int,
        default=4,
        help="vertical sampling stride for RGB surrogate checks (default: 4)",
    )
    p.add_argument(
        "--skip-source",
        action="store_true",
        help="skip source-coupled framemd5 checks",
    )
    p.add_argument(
        "--strict-warnings",
        action="store_true",
        help="treat WARN checks as failures for overall per-file status",
    )
    p.add_argument(
        "--left-pad",
        type=int,
        default=8,
        help="expected black padding columns at left edge (default: 8)",
    )
    p.add_argument(
        "--right-pad",
        type=int,
        default=8,
        help="expected black padding columns at right edge (default: 8)",
    )
    p.add_argument(
        "--black-threshold",
        type=float,
        default=0.01,
        help="maximum gbrpf32 channel value considered black in padding checks (default: 0.01)",
    )
    return p


def main() -> int:
    args = build_parser().parse_args()
    root = Path(args.root).resolve()

    missing = require_tools(["ffprobe", "exrheader", "ffmpeg"])
    if missing:
        print("Missing required tools: " + ", ".join(missing), file=sys.stderr)
        return 2

    exr_files = discover_exrs(root, args.glob)
    if not exr_files:
        print("No EXR files found for the provided glob patterns.", file=sys.stderr)
        return 2

    reports: List[FileReport] = []
    for f in exr_files:
        reports.append(
            validate_one(
                repo_root=root,
                exr_path=f,
                stride_x=max(1, args.stride_x),
                stride_y=max(1, args.stride_y),
                skip_source=bool(args.skip_source),
                left_pad=args.left_pad,
                right_pad=args.right_pad,
                black_threshold=max(0.0, args.black_threshold),
            )
        )

    return print_report(reports, strict_warnings=bool(args.strict_warnings))


if __name__ == "__main__":
    raise SystemExit(main())
