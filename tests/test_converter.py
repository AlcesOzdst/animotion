"""
Tests for Animotion v0.1 offline converter (converter/animotion.py).
"""

import os
import sys
import tempfile
import unittest
from PIL import Image

# Ensure converter module is importable
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(REPO_ROOT, "converter"))

import animotion  # type: ignore


class TestAnimotionConverter(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.temp_dir.cleanup()

    def _create_dummy_gif(self, filename: str, num_frames: int = 3, size=(64, 32), duration=80) -> str:
        filepath = os.path.join(self.temp_dir.name, filename)
        frames = []
        for i in range(num_frames):
            img = Image.new("RGB", size, (i * 50, i * 50, i * 50))
            frames.append(img)
        frames[0].save(
            filepath,
            save_all=True,
            append_images=frames[1:],
            duration=duration,
            loop=0,
        )
        return filepath

    def test_generate_sample_gif(self):
        sample_path = os.path.join(self.temp_dir.name, "test_sample.gif")
        animotion.generate_sample_gif(output_path=sample_path, width=128, height=64, num_frames=8, duration_ms=50)

        self.assertTrue(os.path.exists(sample_path))
        with Image.open(sample_path) as im:
            self.assertEqual(im.format, "GIF")
            self.assertEqual(im.size, (128, 64))
            self.assertEqual(getattr(im, "n_frames", 1), 8)

    def test_extract_frames_and_bit_packing(self):
        # Create a simple 8x8 GIF with 1 frame: top-left pixel white, rest black
        gif_path = os.path.join(self.temp_dir.name, "pixel_test.gif")
        img = Image.new("RGB", (8, 8), (0, 0, 0))
        img.putpixel((0, 0), (255, 255, 255))  # x=0, y=0 -> bit 7 of byte 0
        img.putpixel((7, 0), (255, 255, 255))  # x=7, y=0 -> bit 0 of byte 0
        img.save(gif_path)

        frames, delays = animotion.extract_gif_frames(
            gif_path=gif_path,
            target_width=8,
            target_height=8,
            threshold=128,
            dither=False,
            invert=False,
        )

        self.assertEqual(len(frames), 1)
        # 8x8 pixels = 1 byte per row, 8 bytes total
        self.assertEqual(len(frames[0]), 8)
        # Byte 0 should have bit 7 (0x80) and bit 0 (0x01) set = 0x81
        self.assertEqual(frames[0][0], 0x81)
        # Row 1..7 should be 0x00
        for r in range(1, 8):
            self.assertEqual(frames[0][r], 0x00)

    def test_invert_mode(self):
        gif_path = os.path.join(self.temp_dir.name, "invert_test.gif")
        img = Image.new("RGB", (8, 8), (0, 0, 0))
        img.save(gif_path)

        # Invert black image: all pixels should be set (0xFF)
        frames, _ = animotion.extract_gif_frames(
            gif_path=gif_path,
            target_width=8,
            target_height=8,
            invert=True,
        )
        for b in frames[0]:
            self.assertEqual(b, 0xFF)

    def test_fps_override(self):
        gif_path = self._create_dummy_gif("fps_test.gif", num_frames=4, duration=100)
        frames, delays = animotion.extract_gif_frames(
            gif_path=gif_path,
            target_width=64,
            target_height=32,
            fps_override=20.0,
        )
        self.assertEqual(len(delays), 4)
        for d in delays:
            self.assertEqual(d, 50)  # 1000 / 20 = 50ms

    def test_delay_ms_override(self):
        gif_path = self._create_dummy_gif("delay_test.gif", num_frames=4, duration=100)
        frames, delays = animotion.extract_gif_frames(
            gif_path=gif_path,
            target_width=64,
            target_height=32,
            delay_override=125,
        )
        self.assertEqual(len(delays), 4)
        for d in delays:
            self.assertEqual(d, 125)

    def test_generate_cpp_header_syntax(self):
        gif_path = self._create_dummy_gif("header_test.gif", num_frames=2, size=(16, 16), duration=60)
        out_header = os.path.join(self.temp_dir.name, "my_anim.h")

        animotion.convert_gif(
            input_path=gif_path,
            output_path=out_header,
            width=16,
            height=16,
            var_name="radar_pulse",
        )

        self.assertTrue(os.path.exists(out_header))
        with open(out_header, "r", encoding="utf-8") as f:
            content = f.read()

        self.assertIn("#ifndef ANIMOTION_GENERATED_RADAR_PULSE_H", content)
        self.assertIn("#define RADAR_PULSE_WIDTH 16", content)
        self.assertIn("#define RADAR_PULSE_HEIGHT 16", content)
        self.assertIn("#define RADAR_PULSE_FRAME_COUNT 2", content)
        self.assertIn("radar_pulse_delays", content)
        self.assertIn("radar_pulse_frame_0", content)
        self.assertIn("radar_pulse_frame_1", content)
        self.assertIn("radar_pulse_frames", content)
        self.assertIn("RADAR_PULSE_DATA", content)

    def test_validation_nonexistent_file(self):
        with self.assertRaises(FileNotFoundError):
            animotion.validate_input_path(os.path.join(self.temp_dir.name, "missing.gif"))

    def test_validation_empty_file(self):
        empty_file = os.path.join(self.temp_dir.name, "empty.gif")
        with open(empty_file, "wb") as f:
            pass
        with self.assertRaises(ValueError):
            animotion.validate_input_path(empty_file)

    def test_validation_non_gif(self):
        png_file = os.path.join(self.temp_dir.name, "not_gif.png")
        img = Image.new("RGB", (32, 32), (255, 0, 0))
        img.save(png_file, format="PNG")
        with self.assertRaises(ValueError) as ctx:
            animotion.extract_gif_frames(png_file, 32, 32)
        self.assertIn("requires GIF", str(ctx.exception))

    def test_validation_unsupported_dimensions(self):
        with self.assertRaises(ValueError):
            animotion.validate_dimensions(0, 64)
        with self.assertRaises(ValueError):
            animotion.validate_dimensions(128, -10)
        with self.assertRaises(ValueError):
            animotion.validate_dimensions(2048, 64)

    def test_validation_timing(self):
        with self.assertRaises(ValueError):
            animotion.validate_timing_overrides(fps=-5.0, delay_ms=None)
        with self.assertRaises(ValueError):
            animotion.validate_timing_overrides(fps=None, delay_ms=0)


if __name__ == "__main__":
    unittest.main()
