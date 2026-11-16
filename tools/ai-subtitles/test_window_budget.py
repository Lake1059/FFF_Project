"""Offline security regression for WindowBuilder's retained PCM budget."""
import importlib.util
from pathlib import Path
import unittest

import numpy as np

spec = importlib.util.spec_from_file_location("asr_worker", Path(__file__).with_name("asr_worker.py"))
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)


class WindowBudgetTests(unittest.TestCase):
    def builder(self):
        result = worker.WindowBuilder(5.0, 3.0)
        result.MAX_BUFFER_BYTES = 1024
        result.MAX_BUFFER_BLOCKS = 8
        return result

    def assert_bounded(self, builder):
        self.assertLessEqual(builder.buffered_bytes, builder.MAX_BUFFER_BYTES)
        self.assertLessEqual(len(builder.buf), builder.MAX_BUFFER_BLOCKS)
        self.assertEqual(builder.buffered_bytes, sum(x.nbytes for _, x in builder.buf))

    def test_repeated_timestamps_byte_budget(self):
        builder = self.builder()
        for _ in range(10000):
            builder.add(0, np.zeros((100, 2), np.float32), 48000, 2)
            self.assert_bounded(builder)

    def test_repeated_timestamps_block_budget(self):
        builder = self.builder()
        for _ in range(10000):
            builder.add(0, np.zeros((1, 1), np.float32), 48000, 1)
            self.assert_bounded(builder)

    def test_backwards_and_format_change(self):
        builder = self.builder()
        for pos, rate, channels in [(100, 48000, 2), (50, 48000, 2), (60, 96000, 2), (70, 96000, 6)]:
            builder.add(pos, np.zeros((10, channels), np.float32), rate, channels)
            self.assertEqual(len(builder.buf), 1)
            self.assertEqual(builder.window_start, pos)
            self.assert_bounded(builder)

    def test_oversized_block_and_generation_reset(self):
        builder = self.builder()
        builder.add(0, np.zeros((20, 1), np.float32), 48000, 1)
        builder.add(1, np.zeros((300, 1), np.float32), 48000, 1)
        self.assertEqual(builder.buf, [])
        self.assertIsNone(builder.window_start)
        self.assert_bounded(builder)
        builder.add(20, np.zeros((20, 1), np.float32), 48000, 1)
        builder.reset_generation()
        self.assertEqual(builder.buf, [])
        self.assert_bounded(builder)

    def test_normal_windows_and_recovery(self):
        for rate, channels in [(16000, 1), (48000, 2), (96000, 6)]:
            builder = worker.WindowBuilder(5.0, 3.0)
            # First recover from an abnormal timeline, then deliver 8 seconds.
            builder.add(100, np.zeros((1, channels), np.float32), rate, channels)
            builder.add(0, np.zeros((rate, channels), np.float32), rate, channels)
            windows = []
            for second in range(1, 8):
                builder.add(second * 10_000_000, np.zeros((rate, channels), np.float32), rate, channels)
                window = builder.poll((second + 1) * 10_000_000)
                if window is not None:
                    windows.append(window)
                self.assert_bounded(builder)
            self.assertEqual([(a, b) for a, b, _ in windows], [(0, 50_000_000), (30_000_000, 80_000_000)])
            for _, _, samples in windows:
                self.assertEqual(samples.shape, (80000,))
                self.assertTrue(np.all(samples == 0))


if __name__ == "__main__":
    unittest.main()
