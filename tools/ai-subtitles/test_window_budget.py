"""Offline regressions for bounded PCM, sentence assembly, and caption scheduling."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import Mock, patch

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


class SubtitlePhraseTests(unittest.TestCase):
    def lines(self, text, language):
        translator = worker.SubtitleTranslator(Mock())
        return [p["text"] for p in translator.phrases(worker.SubtitleCue(0, 50_000_000, language, text))]

    def test_english_keeps_brief_comma_clause_in_same_cue(self):
        text = "Well, don't do it! Take the 1,200.50-dollar offer."
        self.assertEqual(self.lines(text, "english"), ["Well don't do it", "Take the 1,200.50-dollar offer"])

    def test_chinese_keeps_sentence_boundaries_and_replaces_internal_punctuation_with_space(self):
        text = "「您猜怎么着？」他呀、还真来了！没错：就是这位。"
        self.assertEqual(self.lines(text, "zh-Hans"), ["您猜怎么着", "他呀 还真来了", "没错 就是这位"])
        self.assertEqual(self.lines("【一共 3.5％，可以吗？】", "cantonese"), ["一共 百分之3点5 可以吗"])

    def test_overlength_dialogue_prefers_a_comma_without_stranding_the_interjection(self):
        text = "好，我听说他们已经想清楚了，等大家回来我们一起商量。"
        self.assertEqual(self.lines(text, "zh"), ["好 我听说他们已经想清楚了", "等大家回来我们一起商量"])

    def test_untagged_chinese_does_not_require_a_second_inference(self):
        backend = Mock()
        translator = worker.SubtitleTranslator(backend, chinese=True)
        phrases = list(translator.phrases(worker.SubtitleCue(0, 50_000_000, "", "你好啊，老朋友！")))
        self.assertEqual([p["text"] for p in phrases], ["你好啊 老朋友"])
        backend.translate.assert_not_called()

    def test_overlength_unpunctuated_chinese_still_splits(self):
        text = "我听说他们今天特意从很远的地方过来就是为了跟大家见上一面"
        phrases = self.lines(text, "Chinese")
        self.assertEqual("".join(phrases), text)
        self.assertEqual(len(phrases), 2)
        self.assertTrue(all(len(phrase) <= 20 for phrase in phrases))

    def test_oversize_word_is_bounded(self):
        text = "A" * 101
        phrases = self.lines(text, "english")
        self.assertEqual("".join(phrases), text)
        self.assertEqual([len(p) for p in phrases], [56, 45])


class SentenceAssemblyTests(unittest.TestCase):
    def setUp(self):
        self.assembler = worker.SubtitleAssembler(20_000_000)

    def accept(self, start, end, text, language="english"):
        return self.assembler.accept(start * 10_000_000, end * 10_000_000, language, text)

    def test_overlap_keeps_the_original_start_and_revises_a_false_stop(self):
        self.assertEqual(self.accept(0, 5, "If you change your mind about the train."), [])
        self.assertEqual(self.accept(3, 8, "About the train, I can arrange a ride."), [])
        received = self.accept(6, 11, "Arrange a ride. Call me tomorrow.")
        self.assertEqual([cue.text for cue in received], ["If you change your mind about the train, I can arrange a ride."])
        self.assertEqual(received[0].start, 0)
        tail = self.assembler.finish()
        self.assertEqual([cue.text for cue in tail], ["Call me tomorrow."])
        self.assertLessEqual(received[0].end, tail[0].start)
        self.assertEqual(self.assembler.finish(), [])

    def test_closed_cue_is_not_replayed_by_an_overlap(self):
        first = self.accept(0, 5, "We are home. Please put the keys by the door.")
        self.assertEqual([cue.text for cue in first], ["We are home."])
        second = self.accept(3, 8, "The keys by the door. We need them tomorrow.") + self.assembler.finish()
        self.assertEqual([cue.text for cue in second], ["Please put the keys by the door.", "We need them tomorrow."])
        self.assertLessEqual(first[0].end, second[0].start)

    def test_cjk_overlap_and_quoted_sentence_boundary(self):
        self.accept(0, 5, "她说：「如果明天有空。」", "chinese")
        self.accept(3, 8, "有空，我们再约时间。」", "chinese")
        tail = self.assembler.finish()
        self.assertEqual([cue.text for cue in tail], ["她说：「如果明天有空，我们再约时间。」"])
        self.assertEqual(tail[0].start, 0)

    def test_real_repeated_dialogue_outside_the_overlap_is_preserved(self):
        first = self.accept(0, 5, "Please stop.")
        second = self.accept(8, 13, "Please stop.") + self.assembler.finish()
        self.assertEqual([cue.text for cue in first + second], ["Please stop.", "Please stop."])
        self.assertEqual([cue.start for cue in first + second], [0, 80_000_000])

    def test_empty_window_can_confirm_a_pause(self):
        self.accept(0, 5, "We have nothing to add")
        self.assertEqual(self.accept(3, 8, ""), [])
        self.assertEqual([cue.text for cue in self.accept(6, 11, "")], ["We have nothing to add"])
        self.assertEqual(self.assembler.finish(), [])

    def test_seek_discards_an_incomplete_sentence(self):
        self.accept(0, 5, "If you change your mind about the train")
        self.assembler.reset()
        self.accept(900, 905, "We are back.")
        self.assertEqual([cue.text for cue in self.assembler.finish()], ["We are back."])

    def test_decimal_and_title_are_not_sentence_stops(self):
        self.accept(0, 5, "Ask Dr. Hopps about 3.5 dollars. Call me tomorrow.")
        self.assertEqual([cue.text for cue in self.assembler.finish()], [
            "Ask Dr. Hopps about 3.5 dollars.", "Call me tomorrow.",
        ])

    def test_no_preroll_and_no_trailing_hold_across_quiet(self):
        samples = np.zeros(5 * 16000, np.float32)
        samples[16000:48000] = 0.08
        spans = worker.subtitle_audio_spans(samples, 0, 50_000_000)
        cues = self.assembler.accept(0, 50_000_000, "english", "Stop here.", spans)
        cues.extend(self.assembler.finish())
        self.assertEqual([cue.text for cue in cues], ["Stop here."])
        self.assertEqual(cues[0].start, 10_000_000)
        self.assertEqual(cues[0].end, 30_000_000)

    def test_no_caption_from_quiet_or_isolated_impulse(self):
        samples = np.zeros(5 * 16000, np.float32)
        samples[16000:16320] = 0.09
        spans = worker.subtitle_audio_spans(samples, 0, 50_000_000)
        self.assertEqual(spans, ())
        self.assertEqual(self.assembler.accept(0, 50_000_000, "chinese", "多谢观看。", spans), [])
        self.assertEqual(self.assembler.finish(), [])

    def test_quiet_gap_is_not_filled_by_the_previous_or_following_phrase(self):
        samples = np.full(5 * 16000, 0.003, np.float32)
        samples[16000:25600] = 0.08
        samples[41600:64000] = 0.08
        spans = worker.subtitle_audio_spans(samples, 0, 50_000_000)
        # No comma comes from this recognizer; quiet should still split display.
        cues = self.assembler.accept(0, 50_000_000, "chinese", "请听我说好戏还在后头呢。", spans)
        cues.extend(self.assembler.finish())
        self.assertEqual(len(cues), 1)
        phrases = list(worker.SubtitleTranslator(Mock()).phrases(cues[0]))
        self.assertEqual([(p["start100ns"], p["end100ns"]) for p in phrases],
                         [(10_000_000, 16_000_000), (26_000_000, 40_000_000)])

    def test_brief_dialogue_pause_does_not_blank_or_change_the_caption(self):
        samples = np.zeros(5 * 16000, np.float32)
        samples[16000:32000] = 0.08
        samples[37120:64000] = 0.08
        spans = worker.subtitle_audio_spans(samples, 0, 50_000_000)
        self.assertEqual(spans, ((10_000_000, 20_000_000), (23_200_000, 40_000_000)))
        cues = self.assembler.accept(0, 50_000_000, "zh", "你先别急，我们一起想办法。", spans)
        cues.extend(self.assembler.finish())
        self.assertEqual(len(cues), 1)
        self.assertEqual(list(worker.SubtitleTranslator(Mock()).phrases(cues[0])), [{
            "text": "你先别急 我们一起想办法", "text2": "", "start100ns": 10_000_000, "end100ns": 40_000_000,
        }])

    def test_overlap_never_pulls_a_new_phrase_into_the_preceding_pause(self):
        first = self.assembler.accept(0, 50_000_000, "english", "I saw that same detective",
                                       ((0, 30_000_000), (36_000_000, 48_000_000)))
        tail = self.assembler.accept(30_000_000, 80_000_000, "english", "Detective and their partner.",
                                      ((41_000_000, 71_000_000),)) + self.assembler.finish()
        self.assertEqual(first, [])
        self.assertEqual([c.text for c in tail], ["I saw that same detective and their partner."])
        self.assertGreaterEqual(next(w.start for w in tail[0].words if w.key == "detective"), 41_000_000)

    def test_sustained_music_does_not_claim_a_quiet_gap(self):
        samples = np.full(5 * 16000, 0.018, np.float32)
        samples[16000:48000] = 0.09
        self.assertEqual(worker.subtitle_audio_spans(samples, 0, 50_000_000), ((0, 50_000_000),))

    def test_eof_keeps_left_overlap_to_complete_a_sentence(self):
        builder = worker.WindowBuilder(5.0, 3.0)
        for i in range(7):
            builder.add(i * 10_000_000, np.zeros((16000, 1), np.float32), 16000, 1)
            builder.poll((i + 1) * 10_000_000)
        start, end, samples = builder.finish(70_000_000)
        self.assertEqual((start, end, len(samples)), (30_000_000, 70_000_000, 64000))


class ContextTranslationTests(unittest.TestCase):
    def setUp(self):
        self.backend = Mock()
        self.backend.translate.return_value = "请用自然的中文翻译整个句子"
        self.translator = worker.SubtitleTranslator(self.backend, chinese=True)

    def test_whole_sentence_translated_once_without_flashing_the_short_interjection(self):
        text = "Wait, we can still make the last train."
        self.backend.translate.return_value = "「等等！」，我们还能赶上末班车。"
        phrases = list(self.translator.phrases(worker.SubtitleCue(0, 60_000_000, "english", text)))
        self.backend.translate.assert_called_once_with(text, "zh", ())
        self.assertEqual(phrases, [{
            "text": "等等 我们还能赶上末班车", "text2": "Wait we can still make the last train",
            "start100ns": 0, "end100ns": 60_000_000,
        }])

    def test_bilingual_clauses_begin_with_their_source_audio_not_equal_pages(self):
        text = "Please stand right here, don't go."
        self.backend.translate.return_value = "请站在这里，别走。"
        words = worker._subtitle_words(text, 0, 55_000_000, ((12_000_000, 38_000_000), (45_000_000, 54_000_000)))
        phrases = list(self.translator.phrases(worker.SubtitleCue(0, 55_000_000, "English", text, tuple(words))))
        self.assertEqual([(p["text"], p["text2"], p["start100ns"], p["end100ns"]) for p in phrases], [
            ("请站在这里", "Please stand right here", 12_000_000, 38_000_000),
            ("别走", "don't go", 45_000_000, 54_000_000),
        ])

    def test_context_keeps_three_recent_cues_and_clears_on_seek(self):
        for i in range(5):
            list(self.translator.phrases(worker.SubtitleCue(i * 10_000_000, (i + 1) * 10_000_000, "english", f"Line {i}.")))
        history = self.backend.translate.call_args.args[2]
        self.assertEqual([line for line, _ in history], ["Line 1.", "Line 2.", "Line 3."])
        prompt = worker.translation_prompt("That is the one.", "zh", history)
        self.assertIn("前文对白（参考）", prompt)
        self.assertIn("当前原文：That is the one.", prompt)
        self.translator.reset()
        list(self.translator.phrases(worker.SubtitleCue(0, 10_000_000, "english", "Back at the start.")))
        self.assertEqual(self.backend.translate.call_args.args[2], ())
        list(self.translator.phrases(worker.SubtitleCue(900_000_000, 930_000_000, "english", "After a long pause.")))
        self.assertEqual(self.backend.translate.call_args.args[2], ())

    def test_context_echo_retries_the_current_sentence_only(self):
        backend = object.__new__(worker.LlamaBackend)
        backend._proc = Mock()
        backend._proc.poll.return_value = None
        backend._port = 1
        history = (("The two police officers will attend the celebration tomorrow.", "两位警官会在明天出席一场非常盛大的庆祝仪式"),)
        with patch.object(worker, "_post_json", side_effect=[
            {"content": "他们会在明天出席一场非常盛大的庆祝仪式。"}, {"content": "现在就走。"},
        ]) as complete:
            self.assertEqual(backend.translate("Let's go now.", "zh", history), "现在就走。")
            self.assertEqual(complete.call_count, 2)
            self.assertNotIn(history[0][0], complete.call_args.args[2]["prompt"])
        backend._proc = None


if __name__ == "__main__":
    unittest.main()
