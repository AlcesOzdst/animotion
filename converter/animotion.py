#!/usr/bin/env python3
"""
Animotion v0.1 — Offline Animation-to-Firmware Converter
Converts GIF animations into monochrome bitmap C/C++ headers for ESP microcontrollers.

Bit order: Row-major, 1-bit per pixel, MSB-first.
Compatible with Adafruit_GFX, Adafruit_SSD1306, and the Animotion embedded player.
"""

from __future__ import annotations

import argparse
import math
import os
import re
import sys
from typing import List, Tuple, Optional

try:
    from PIL import Image, ImageDraw, ImageSequence
except ImportError:
    print("Error: Pillow library is required. Install it using 'pip install Pillow'.", file=sys.stderr)
    sys.exit(1)


def sanitize_identifier(name: str) -> str:
    """Sanitize a string for use as a C/C++ identifier."""
    cleaned = re.sub(r"[^a-zA-Z0-9_]", "_", name)
    if not cleaned or cleaned[0].isdigit():
        cleaned = "_" + cleaned
    return cleaned


def validate_input_path(path: str) -> None:
    """Validate that the input file exists, is a regular file, and is not empty."""
    if not os.path.exists(path):
        raise FileNotFoundError(f"Input file does not exist: {path}")
    if os.path.isdir(path):
        raise ValueError(f"Input path is a directory, not a file: {path}")
    if os.path.getsize(path) == 0:
        raise ValueError(f"Input file is empty (0 bytes): {path}")


def validate_dimensions(width: int, height: int) -> None:
    """Validate target bitmap dimensions."""
    if not isinstance(width, int) or not isinstance(height, int):
        raise TypeError("Width and height must be integers.")
    if width <= 0 or height <= 0:
        raise ValueError(f"Dimensions must be positive integers, got {width}x{height}.")
    if width > 1024 or height > 1024:
        raise ValueError(f"Dimensions ({width}x{height}) exceed maximum supported resolution (1024x1024).")


def validate_timing_overrides(fps: Optional[float], delay_ms: Optional[int]) -> None:
    """Validate FPS and delay overrides."""
    if fps is not None:
        if fps <= 0 or fps > 120:
            raise ValueError(f"FPS override must be between 0.1 and 120.0, got {fps}.")
    if delay_ms is not None:
        if delay_ms <= 0 or delay_ms > 10000:
            raise ValueError(f"Delay override must be between 1 and 10000 ms, got {delay_ms}.")


def extract_gif_frames(
    gif_path: str,
    target_width: int,
    target_height: int,
    fit_mode: str = "stretch",
    threshold: int = 128,
    dither: bool = False,
    invert: bool = False,
    fps_override: Optional[float] = None,
    delay_override: Optional[int] = None,
) -> Tuple[List[bytearray], List[int]]:
    """
    Extract frames from a GIF, composite transparency, resize, binarize to monochrome,
    and pack into row-major 1-bit MSB-first byte arrays.

    Returns:
        (frames_bytes, frame_delays_ms)
    """
    validate_input_path(gif_path)
    validate_dimensions(target_width, target_height)
    validate_timing_overrides(fps_override, delay_override)

    if not (0 <= threshold <= 255):
        raise ValueError(f"Threshold must be between 0 and 255, got {threshold}.")

    try:
        im = Image.open(gif_path)
    except Exception as exc:
        raise ValueError(f"Unable to open '{gif_path}' as an image: {exc}") from exc

    if im.format != "GIF":
        im.close()
        raise ValueError(f"Input file '{gif_path}' has format '{im.format}'. Animotion v0.1 requires GIF.")

    num_frames = getattr(im, "n_frames", 1)
    if num_frames == 0:
        im.close()
        raise ValueError(f"GIF '{gif_path}' contains 0 frames.")

    row_bytes = (target_width + 7) // 8
    frame_size_bytes = row_bytes * target_height

    packed_frames: List[bytearray] = []
    frame_delays: List[int] = []

    # Canvas compositing state for animated GIFs
    canvas_size = im.size
    canvas = Image.new("RGBA", canvas_size, (0, 0, 0, 255))
    prev_canvas = canvas.copy()

    try:
        for frame_idx in range(num_frames):
            im.seek(frame_idx)

            # Determine frame delay
            if fps_override is not None:
                duration = max(1, int(round(1000.0 / fps_override)))
            elif delay_override is not None:
                duration = delay_override
            else:
                raw_duration = im.info.get("duration", 100)
                if raw_duration is None or raw_duration < 10:
                    duration = 100
                else:
                    duration = int(raw_duration)

            disposal = im.info.get("disposal", 2)
            frame_rgba = im.convert("RGBA")

            # Paste current frame over canvas using alpha mask
            if frame_rgba.size == canvas.size:
                canvas.paste(frame_rgba, (0, 0), frame_rgba)
            else:
                canvas.paste(frame_rgba, (0, 0), frame_rgba)

            # Resize composed frame
            if fit_mode == "contain":
                # Maintain aspect ratio, letterbox with black
                scale = min(target_width / canvas.width, target_height / canvas.height)
                new_w = max(1, int(round(canvas.width * scale)))
                new_h = max(1, int(round(canvas.height * scale)))
                scaled = canvas.resize((new_w, new_h), Image.Resampling.LANCZOS)
                resized = Image.new("RGBA", (target_width, target_height), (0, 0, 0, 255))
                offset_x = (target_width - new_w) // 2
                offset_y = (target_height - new_h) // 2
                resized.paste(scaled, (offset_x, offset_y))
            else:
                # Direct stretch to target dimensions
                resized = canvas.resize((target_width, target_height), Image.Resampling.LANCZOS)

            # Convert to grayscale
            gray = resized.convert("L")

            # Binarize to monochrome
            if dither:
                mono = gray.convert("1", dither=Image.Dither.FLOYDSTEINBERG)
            else:
                mono = gray.point(lambda p: 255 if p >= threshold else 0, mode="1")

            # Pack 1-bit MSB-first row-major
            pixels = mono.load()
            frame_buffer = bytearray(frame_size_bytes)

            for y in range(target_height):
                row_offset = y * row_bytes
                for x in range(target_width):
                    is_white = pixels[x, y] > 0
                    if invert:
                        is_white = not is_white
                    if is_white:
                        byte_index = row_offset + (x // 8)
                        bit_index = 7 - (x % 8)
                        frame_buffer[byte_index] |= (1 << bit_index)

            packed_frames.append(frame_buffer)
            frame_delays.append(duration)

            # Handle GIF frame disposal
            if disposal == 2:
                canvas = Image.new("RGBA", canvas_size, (0, 0, 0, 255))
            elif disposal == 3:
                canvas = prev_canvas.copy()
            else:
                prev_canvas = canvas.copy()

    finally:
        im.close()

    return packed_frames, frame_delays


def generate_cpp_header(
    frames: List[bytearray],
    delays: List[int],
    width: int,
    height: int,
    var_name: str,
    source_filename: str = "animation.gif",
) -> str:
    """
    Format frame bitmaps and timing metadata into a clean, PROGMEM-enabled C++ header.
    """
    sanitized = sanitize_identifier(var_name)
    var_lower = sanitized.lower()
    var_upper = sanitized.upper()

    row_bytes = (width + 7) // 8
    bytes_per_frame = row_bytes * height
    frame_count = len(frames)
    total_duration_ms = sum(delays)
    default_delay_ms = delays[0] if delays else 100

    lines: List[str] = []
    lines.append("// =============================================================================")
    lines.append("// Generated by Animotion v0.1 (Animation-to-Firmware Tool for ESP Microcontrollers)")
    lines.append(f"// Source Asset: {source_filename}")
    lines.append(f"// Dimensions:   {width}x{height} pixels")
    lines.append(f"// Frame Count:  {frame_count}")
    lines.append(f"// Frame Size:   {bytes_per_frame} bytes (row-major, 1-bit MSB-first, padded to full byte)")
    lines.append(f"// Total Memory: {bytes_per_frame * frame_count} bytes in Flash (PROGMEM)")
    lines.append(f"// Duration:     {total_duration_ms} ms")
    lines.append("//")
    lines.append("// Bit packing order:")
    lines.append("//   - Row-major: scanlines are packed from top (y=0) to bottom (y=height-1).")
    lines.append("//   - Each row is padded to a whole byte boundary.")
    lines.append("//   - MSB-first: bit 7 (0x80) represents the leftmost pixel (x % 8 == 0).")
    lines.append("//   - 1 = Pixel ON (White), 0 = Pixel OFF (Black).")
    lines.append("//   - Directly compatible with Adafruit_GFX / Adafruit_SSD1306 drawBitmap().")
    lines.append("// =============================================================================")
    lines.append("")
    lines.append(f"#ifndef ANIMOTION_GENERATED_{var_upper}_H")
    lines.append(f"#define ANIMOTION_GENERATED_{var_upper}_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("// Cross-platform PROGMEM portability")
    lines.append("#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO)")
    lines.append("    #include <pgmspace.h>")
    lines.append("#else")
    lines.append("    #ifndef PROGMEM")
    lines.append("        #define PROGMEM")
    lines.append("    #endif")
    lines.append("#endif")
    lines.append("")
    lines.append(f"#define {var_upper}_WIDTH {width}")
    lines.append(f"#define {var_upper}_HEIGHT {height}")
    lines.append(f"#define {var_upper}_FRAME_COUNT {frame_count}")
    lines.append(f"#define {var_upper}_BYTES_PER_FRAME {bytes_per_frame}")
    lines.append(f"#define {var_upper}_DEFAULT_DELAY_MS {default_delay_ms}")
    lines.append(f"#define {var_upper}_TOTAL_DURATION_MS {total_duration_ms}")
    lines.append("")

    # Delays array
    lines.append(f"// Frame duration in milliseconds (PROGMEM)")
    lines.append(f"static const uint16_t {var_lower}_delays[{var_upper}_FRAME_COUNT] PROGMEM = {{")
    delay_chunks = [str(d) for d in delays]
    for i in range(0, len(delay_chunks), 12):
        chunk = ", ".join(delay_chunks[i : i + 12])
        comma = "," if i + 12 < len(delay_chunks) else ""
        lines.append(f"    {chunk}{comma}")
    lines.append("};")
    lines.append("")

    # Individual frames
    for idx, f_data in enumerate(frames):
        lines.append(f"// Frame {idx} ({bytes_per_frame} bytes)")
        lines.append(f"static const uint8_t {var_lower}_frame_{idx}[{var_upper}_BYTES_PER_FRAME] PROGMEM = {{")
        hex_values = [f"0x{b:02x}" for b in f_data]
        for i in range(0, len(hex_values), 16):
            chunk = ", ".join(hex_values[i : i + 16])
            comma = "," if i + 16 < len(hex_values) else ""
            lines.append(f"    {chunk}{comma}")
        lines.append("};")
        lines.append("")

    # Array of pointers to all frames
    lines.append(f"// Pointers to all animation frames (PROGMEM)")
    lines.append(f"static const uint8_t* const {var_lower}_frames[{var_upper}_FRAME_COUNT] PROGMEM = {{")
    for idx in range(frame_count):
        comma = "," if idx + 1 < frame_count else ""
        lines.append(f"    {var_lower}_frame_{idx}{comma}")
    lines.append("};")
    lines.append("")

    # Struct descriptor compatible with Animotion engine
    lines.append("// Animotion Engine Animation descriptor (optional runtime integration)")
    lines.append("#ifdef __cplusplus")
    lines.append("namespace animotion {")
    lines.append("#ifndef ANIMOTION_STRUCT_DEFINED")
    lines.append("#define ANIMOTION_STRUCT_DEFINED")
    lines.append("struct Animation {")
    lines.append("    const uint8_t* const* frames;")
    lines.append("    const uint16_t* frame_delays_ms;")
    lines.append("    uint16_t frame_count;")
    lines.append("    uint16_t width;")
    lines.append("    uint16_t height;")
    lines.append("    uint16_t default_delay_ms;")
    lines.append("    bool loop;")
    lines.append("};")
    lines.append("#endif")
    lines.append("} // namespace animotion")
    lines.append("")
    lines.append(f"static const animotion::Animation {var_upper}_DATA = {{")
    lines.append(f"    {var_lower}_frames,")
    lines.append(f"    {var_lower}_delays,")
    lines.append(f"    {var_upper}_FRAME_COUNT,")
    lines.append(f"    {var_upper}_WIDTH,")
    lines.append(f"    {var_upper}_HEIGHT,")
    lines.append(f"    {var_upper}_DEFAULT_DELAY_MS,")
    lines.append("    true /* loop */")
    lines.append("};")
    lines.append("#endif")
    lines.append("")
    lines.append(f"#endif // ANIMOTION_GENERATED_{var_upper}_H")
    lines.append("")

    return "\n".join(lines)


def generate_sample_gif(
    output_path: str = "sample.gif",
    width: int = 128,
    height: int = 64,
    num_frames: int = 16,
    duration_ms: int = 60,
) -> str:
    """
    Generate a high-contrast, stylish 128x64 animated GIF asset for testing and demonstrations.
    Displays an animated rotating radar/satellite scanner on the left and dynamic telemetry on the right.
    """
    validate_dimensions(width, height)
    frames: List[Image.Image] = []

    for i in range(num_frames):
        img = Image.new("RGB", (width, height), (0, 0, 0))
        draw = ImageDraw.Draw(img)

        # Left scanner geometry
        cx, cy = width // 4, height // 2
        max_r = min(width // 4 - 4, height // 2 - 4)
        draw.ellipse((cx - max_r, cy - max_r, cx + max_r, cy + max_r), outline=(255, 255, 255))
        draw.ellipse((cx - 8, cy - 8, cx + 8, cy + 8), outline=(255, 255, 255))

        # Sweep beam line
        angle = (2.0 * math.pi * i) / num_frames
        x2 = cx + int(max_r * math.cos(angle))
        y2 = cy + int(max_r * math.sin(angle))
        draw.line((cx, cy, x2, y2), fill=(255, 255, 255), width=2)

        # Orbiting satellite beacon
        sat_r = max_r - 8
        sat_angle = -angle * 2.0
        sx = cx + int(sat_r * math.cos(sat_angle))
        sy = cy + int(sat_r * math.sin(sat_angle))
        draw.rectangle((sx - 1, sy - 1, sx + 1, sy + 1), fill=(255, 255, 255))

        # Divider line
        div_x = width // 2 - 2
        draw.line((div_x, 6, div_x, height - 7), fill=(255, 255, 255))

        # Right side telemetry text
        text_x = div_x + 8
        draw.text((text_x, 6), "ANIMOTION", fill=(255, 255, 255))
        draw.text((text_x, 19), "v0.1 MVP", fill=(255, 255, 255))
        draw.text((text_x, 32), "ESP32-C3", fill=(255, 255, 255))

        # Equalizer audio wave bars
        num_bars = 6
        bar_width = 5
        bar_gap = 3
        bars_start_x = text_x
        for b in range(num_bars):
            bx = bars_start_x + b * (bar_width + bar_gap)
            bh = max(2, int(8 + 6 * math.sin(angle + b * 0.9)))
            y_top = (height - 8) - bh
            y_bottom = height - 8
            draw.rectangle((bx, y_top, bx + bar_width, y_bottom), fill=(255, 255, 255))

        frames.append(img)

    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    frames[0].save(
        output_path,
        save_all=True,
        append_images=frames[1:],
        duration=duration_ms,
        loop=0,
        optimize=True,
    )
    return output_path


def convert_gif(
    input_path: str,
    output_path: Optional[str] = None,
    width: int = 128,
    height: int = 64,
    fit_mode: str = "stretch",
    threshold: int = 128,
    dither: bool = False,
    invert: bool = False,
    fps_override: Optional[float] = None,
    delay_override: Optional[int] = None,
    var_name: Optional[str] = None,
    verbose: bool = False,
) -> str:
    """
    Main conversion routine: GIF -> monochrome bit-packed C++ header.
    Returns path to generated header file.
    """
    if output_path is None:
        base_name = os.path.splitext(os.path.basename(input_path))[0]
        output_path = f"{base_name}.h"

    if var_name is None:
        var_name = os.path.splitext(os.path.basename(output_path))[0]
        if var_name.lower() in ("main", "index", "data"):
            var_name = os.path.splitext(os.path.basename(input_path))[0]

    if verbose:
        print(f"Reading GIF: {input_path}")
        print(f"Target dimensions: {width}x{height}")
        print(f"Mode: fit={fit_mode}, threshold={threshold}, dither={dither}, invert={invert}")

    frames, delays = extract_gif_frames(
        gif_path=input_path,
        target_width=width,
        target_height=height,
        fit_mode=fit_mode,
        threshold=threshold,
        dither=dither,
        invert=invert,
        fps_override=fps_override,
        delay_override=delay_override,
    )

    header_content = generate_cpp_header(
        frames=frames,
        delays=delays,
        width=width,
        height=height,
        var_name=var_name,
        source_filename=os.path.basename(input_path),
    )

    out_dir = os.path.dirname(os.path.abspath(output_path))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(header_content)

    if verbose:
        total_bytes = len(frames) * ((width + 7) // 8) * height
        print(f"Converted {len(frames)} frames successfully.")
        print(f"Total PROGMEM: {total_bytes} bytes.")
        print(f"Exported to: {output_path}")

    return output_path


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="animotion",
        description="Animotion v0.1: Convert GIFs into optimized C/C++ firmware headers for ESP microcontrollers.",
    )
    parser.add_argument("input", nargs="?", help="Path to input GIF file.")
    parser.add_argument("-o", "--output", help="Path to output C++ header file (default: <name>.h).")
    parser.add_argument("-W", "--width", type=int, default=128, help="Target width in pixels (default: 128).")
    parser.add_argument("-H", "--height", type=int, default=64, help="Target height in pixels (default: 64).")
    parser.add_argument("--fps", type=float, help="Override playback frame rate (e.g. 15 for 15 FPS).")
    parser.add_argument("--delay-ms", type=int, help="Override frame delay in milliseconds directly.")
    parser.add_argument("--threshold", type=int, default=128, help="Monochrome threshold 0-255 (default: 128).")
    parser.add_argument("--dither", action="store_true", help="Enable Floyd-Steinberg dithering for smooth gradients.")
    parser.add_argument("--invert", action="store_true", help="Invert monochrome output (1=black, 0=white).")
    parser.add_argument(
        "--fit",
        choices=["stretch", "contain"],
        default="stretch",
        help="Resize mode: stretch to fill or contain with aspect ratio (default: stretch).",
    )
    parser.add_argument("--var-name", help="C/C++ identifier prefix (default: derived from filename).")
    parser.add_argument(
        "--generate-sample",
        nargs="?",
        const="sample.gif",
        help="Generate a sample GIF animation and exit (or convert if input is not provided).",
    )
    parser.add_argument("-v", "--verbose", action="store_true", help="Enable verbose conversion output.")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    # Handle sample generation option
    if args.generate_sample:
        sample_path = args.generate_sample
        try:
            generate_sample_gif(
                output_path=sample_path,
                width=args.width,
                height=args.height,
                num_frames=16,
                duration_ms=int(round(1000.0 / args.fps)) if args.fps else (args.delay_ms or 60),
            )
            print(f"[Animotion] Generated sample animation asset: {sample_path}")
        except Exception as exc:
            print(f"[Animotion] Error generating sample GIF: {exc}", file=sys.stderr)
            return 1

        # If no separate input was given, convert the generated sample
        if not args.input:
            args.input = sample_path

    if not args.input:
        parser.print_help(sys.stderr)
        print("\nError: Please provide an input GIF file, or use --generate-sample.", file=sys.stderr)
        return 1

    try:
        out_path = convert_gif(
            input_path=args.input,
            output_path=args.output,
            width=args.width,
            height=args.height,
            fit_mode=args.fit,
            threshold=args.threshold,
            dither=args.dither,
            invert=args.invert,
            fps_override=args.fps,
            delay_override=args.delay_ms,
            var_name=args.var_name,
            verbose=args.verbose,
        )
        print(f"[Animotion] Successfully converted '{args.input}' -> '{out_path}'")
        return 0
    except (FileNotFoundError, ValueError, TypeError) as exc:
        print(f"[Animotion] Validation Error: {exc}", file=sys.stderr)
        return 1
    except Exception as exc:
        print(f"[Animotion] Unexpected Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
