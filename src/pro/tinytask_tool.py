#!/usr/bin/env python3
"""
TinyTask Pro (.ttp) Project Unpacker & Inspector Tool
Author: Antigravity Pair-Programming
Usage:
    python tinytask_tool.py unpack <path_to_macro.ttp> [output_directory]
    python tinytask_tool.py info <path_to_macro.ttp>
"""

import sys
import os
import struct
import json

HEADER_FORMAT = "<4sIII"  # magic(4), version(4), stepCount(4), flags(4) -> 16 bytes
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)

STEP_FORMAT = "<IIIiiiiII128sII"
# stepId(4), actionType(4), targetMode(4), origX(4), origY(4), destX(4), destY(4),
# timeoutMs(4), postDelayMs(4), textKey(128), imageOffset(4), imageSize(4) -> 172 bytes
STEP_SIZE = struct.calcsize(STEP_FORMAT)

ACTION_NAMES = {
    1: "CLICK",
    2: "DBLCLICK",
    3: "RCLICK",
    4: "DRAG",
    5: "TYPE_TEXT",
    6: "HOTKEY"
}

TARGET_MODE_NAMES = {
    1: "TEXT",
    2: "IMAGE",
    3: "COORD"
}

TIMEOUT_ACTION_NAMES = {
    0: "PROMPT",
    1: "RETRY",
    2: "USE_RECORDED",
    3: "SKIP",
    4: "STOP"
}

def sanitize_filename(name: str) -> str:
    invalid_chars = '<>:"/\\|?*'
    for c in invalid_chars:
        name = name.replace(c, "_")
    return name.strip()

def parse_bmp_dimensions(bmp_bytes: bytes):
    if len(bmp_bytes) >= 26 and bmp_bytes[:2] == b"BM":
        w, h = struct.unpack("<ii", bmp_bytes[18:26])
        return w, abs(h)
    return None, None

def unpack_ttp(file_path: str, output_dir: str = None):
    if not os.path.isfile(file_path):
        print(f"[Error] File not found: {file_path}")
        return False

    with open(file_path, "rb") as f:
        data = f.read()

    file_size = len(data)
    if file_size < HEADER_SIZE:
        print(f"[Error] File too small to be a valid .ttp project ({file_size} bytes)")
        return False

    magic, version, step_count, flags = struct.unpack_from(HEADER_FORMAT, data, 0)
    magic_str = magic.decode("ascii", errors="ignore")
    if magic_str != "TTP1":
        print(f"[Error] Invalid magic header: expected 'TTP1', got '{magic_str}'")
        return False

    if output_dir is None:
        base_dir = os.path.dirname(os.path.abspath(file_path))
        base_name = os.path.splitext(os.path.basename(file_path))[0]
        output_dir = os.path.join(base_dir, f"{base_name}_unpacked")

    os.makedirs(output_dir, exist_ok=True)

    print("=" * 65)
    print(f"  TinyTask Pro (.ttp) Project Unpacker")
    print("=" * 65)
    print(f"File:       {file_path} ({file_size:,} bytes)")
    print(f"Version:    {version}")
    print(f"Steps:      {step_count}")
    print(f"Output Dir: {output_dir}")
    print("-" * 65)

    offset = HEADER_SIZE
    steps_info = []

    for i in range(step_count):
        if offset + STEP_SIZE > file_size:
            print(f"[Warning] Truncated file: could not read step {i+1}")
            break

        (step_id, action_type, raw_target_mode,
         orig_x, orig_y, dest_x, dest_y,
         timeout_ms, post_delay_ms, raw_text_key,
         image_offset, image_size) = struct.unpack_from(STEP_FORMAT, data, offset)
        offset += STEP_SIZE

        # Text key is null-terminated 128 bytes string
        null_idx = raw_text_key.find(b"\x00")
        if null_idx >= 0:
            text_key = raw_text_key[:null_idx].decode("gbk", errors="ignore")
        else:
            text_key = raw_text_key.decode("gbk", errors="ignore")

        base_mode = raw_target_mode & 0xFFFF
        timeout_act = (raw_target_mode >> 16) & 0xFFFF

        action_name = ACTION_NAMES.get(action_type, f"UNKNOWN({action_type})")
        mode_name = TARGET_MODE_NAMES.get(base_mode, f"UNKNOWN({base_mode})")
        timeout_act_name = TIMEOUT_ACTION_NAMES.get(timeout_act, f"UNKNOWN({timeout_act})")

        bmp_filename = None
        bmp_dims = None

        if image_size > 0 and image_offset + image_size <= file_size:
            bmp_bytes = data[image_offset:image_offset + image_size]
            bw, bh = parse_bmp_dimensions(bmp_bytes)
            if bw is not None:
                bmp_dims = f"{bw}x{bh}"

            clean_text = sanitize_filename(text_key) if text_key else f"{orig_x}_{orig_y}"
            bmp_filename = f"step{step_id:02d}_{action_name}_{clean_text}.bmp"
            bmp_path = os.path.join(output_dir, bmp_filename)
            with open(bmp_path, "wb") as bf:
                bf.write(bmp_bytes)

        step_record = {
            "stepId": step_id,
            "action": action_name,
            "targetMode": mode_name,
            "onTimeout": timeout_act_name,
            "orig": [orig_x, orig_y],
            "dest": [dest_x, dest_y],
            "timeoutMs": timeout_ms,
            "postDelayMs": post_delay_ms,
            "textKey": text_key,
            "bmpFile": bmp_filename,
            "bmpSize": image_size,
            "bmpDimensions": bmp_dims
        }
        steps_info.append(step_record)

        dim_str = f" ({bmp_dims})" if bmp_dims else ""
        asset_str = f"{image_size}B{dim_str} -> {bmp_filename}" if bmp_filename else "None"
        print(f"Step {step_id:02d} | {action_name:<8} | Mode: {mode_name:<5} | Orig: ({orig_x:>4}, {orig_y:>4}) | Text: '{text_key}'")
        print(f"        | Timeout: {timeout_ms/1000.0:.1f}s [{timeout_act_name}] | Delay: {post_delay_ms}ms | Asset: {asset_str}")

    # Write summary manifest JSON
    manifest_path = os.path.join(output_dir, "manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as jf:
        json.dump({
            "sourceFile": file_path,
            "fileSize": file_size,
            "version": version,
            "stepCount": step_count,
            "steps": steps_info
        }, jf, ensure_ascii=False, indent=2)

    print("-" * 65)
    print(f"[Success] Extracted {len(steps_info)} steps and {sum(1 for s in steps_info if s['bmpFile'])} BMP images.")
    print(f"Summary manifest saved to: {manifest_path}")
    print("=" * 65)
    return True

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python tinytask_tool.py unpack <file.ttp> [output_dir]")
        print("  python tinytask_tool.py info <file.ttp>")
        print("  (Or drag and drop a .ttp file onto this script)")
        sys.exit(1)

    cmd = sys.argv[1].lower()
    if cmd in ("unpack", "-u"):
        if len(sys.argv) < 3:
            print("[Error] Missing path to .ttp file.")
            sys.exit(1)
        target = sys.argv[2]
        out_dir = sys.argv[3] if len(sys.argv) > 3 else None
        unpack_ttp(target, out_dir)
    elif cmd in ("info", "-i"):
        if len(sys.argv) < 3:
            print("[Error] Missing path to .ttp file.")
            sys.exit(1)
        # info can extract to temp or print without saving
        target = sys.argv[2]
        unpack_ttp(target, None)
    else:
        # Default fallback: treat first arg as file path directly (for drag & drop)
        target = sys.argv[1]
        out_dir = sys.argv[2] if len(sys.argv) > 2 else None
        unpack_ttp(target, out_dir)
