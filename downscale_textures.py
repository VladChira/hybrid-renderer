#!/usr/bin/env python3
import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Pillow is required. Install with: pip install pillow", file=sys.stderr)
    sys.exit(1)


SUPPORTED_EXTS = {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".tif", ".tiff", ".webp"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Recursively downscale textures in a folder, preserving file paths and names."
    )
    parser.add_argument("folder", type=Path, help="Root folder containing textures")
    parser.add_argument(
        "--scale",
        type=float,
        default=0.5,
        help="Scale factor (0 < scale <= 1). Default: 0.5",
    )
    parser.add_argument(
        "--max-size",
        type=int,
        default=0,
        help="Optional max width/height after scaling (0 disables).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would be changed without writing files.",
    )
    parser.add_argument(
        "--skip-existing-small",
        action="store_true",
        help="Skip files already <= --max-size when max-size is enabled.",
    )
    return parser.parse_args()


def target_size(width: int, height: int, scale: float, max_size: int) -> tuple[int, int]:
    new_w = max(1, int(round(width * scale)))
    new_h = max(1, int(round(height * scale)))

    if max_size > 0:
        longest = max(new_w, new_h)
        if longest > max_size:
            factor = max_size / float(longest)
            new_w = max(1, int(round(new_w * factor)))
            new_h = max(1, int(round(new_h * factor)))

    return new_w, new_h


def process_image(path: Path, scale: float, max_size: int, dry_run: bool, skip_existing_small: bool) -> bool:
    try:
        with Image.open(path) as img:
            w, h = img.size

            if max_size > 0 and skip_existing_small and max(w, h) <= max_size:
                return False

            new_w, new_h = target_size(w, h, scale, max_size)

            if new_w >= w and new_h >= h:
                return False

            if dry_run:
                print(f"[DRY] {path} : {w}x{h} -> {new_w}x{new_h}")
                return True

            resized = img.resize((new_w, new_h), Image.Resampling.LANCZOS)

            save_kwargs = {}
            ext = path.suffix.lower()
            if ext in {".jpg", ".jpeg"}:
                save_kwargs["quality"] = 95
                save_kwargs["optimize"] = True
            elif ext == ".png":
                save_kwargs["optimize"] = True

            resized.save(path, **save_kwargs)
            print(f"{path} : {w}x{h} -> {new_w}x{new_h}")
            return True
    except Exception as exc:
        print(f"[ERROR] {path} : {exc}", file=sys.stderr)
        return False


def main() -> int:
    args = parse_args()

    if not args.folder.exists() or not args.folder.is_dir():
        print(f"Folder not found: {args.folder}", file=sys.stderr)
        return 1

    if not (0.0 < args.scale <= 1.0):
        print("--scale must be in (0, 1].", file=sys.stderr)
        return 1

    files = [p for p in args.folder.rglob("*") if p.is_file() and p.suffix.lower() in SUPPORTED_EXTS]
    if not files:
        print("No supported texture files found.")
        return 0

    changed = 0
    for path in files:
        if process_image(path, args.scale, args.max_size, args.dry_run, args.skip_existing_small):
            changed += 1

    print(f"\nDone. Processed {len(files)} file(s), changed {changed}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
