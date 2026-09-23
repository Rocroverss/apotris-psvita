#!/usr/bin/env python3
"""Convert MP3 assets in a Dark Lands APK to Vita-SoLoud-compatible Ogg files."""

from __future__ import annotations

import argparse
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tempfile
import zipfile


def safe_archive_path(name: str) -> PurePosixPath:
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError(f"unsafe APK entry: {name}")
    return path


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert all MP3 files in base.apk to an Ogg override tree."
    )
    parser.add_argument("apk", type=Path, help="path to the original base.apk")
    parser.add_argument(
        "output",
        nargs="?",
        type=Path,
        default=Path("converted_audio"),
        help="output directory (default: converted_audio)",
    )
    parser.add_argument(
        "--quality",
        type=float,
        default=5.0,
        help="ffmpeg libvorbis quality from -1 to 10 (default: 5)",
    )
    args = parser.parse_args()

    if not args.apk.is_file():
        parser.error(f"APK does not exist: {args.apk}")
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        parser.error("ffmpeg was not found in PATH")

    converted = 0
    with zipfile.ZipFile(args.apk) as apk, tempfile.TemporaryDirectory() as temporary:
        temp_root = Path(temporary)
        mp3_entries = [name for name in apk.namelist() if name.lower().endswith(".mp3")]

        for index, name in enumerate(mp3_entries, 1):
            relative = safe_archive_path(name)
            source = temp_root / f"audio-{index}.mp3"
            destination = args.output.joinpath(*relative.with_suffix(".ogg").parts)
            destination.parent.mkdir(parents=True, exist_ok=True)

            source.write_bytes(apk.read(name))
            subprocess.run(
                [
                    ffmpeg,
                    "-nostdin",
                    "-hide_banner",
                    "-loglevel",
                    "error",
                    "-y",
                    "-i",
                    str(source),
                    "-vn",
                    "-c:a",
                    "libvorbis",
                    "-q:a",
                    str(args.quality),
                    str(destination),
                ],
                check=True,
            )
            converted += 1
            print(f"[{index}/{len(mp3_entries)}] {name} -> {destination}")

    print(f"Converted {converted} MP3 file(s) into {args.output}")
    print("Copy the output assets directory to ux0:data/dla/assets on the Vita.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
