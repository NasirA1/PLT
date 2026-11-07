"""Tests of the toolkit-independent canvas model and the graphics record protocol."""

import json
import threading
import unittest

from tests.python import support  # noqa: F401  (puts the IDE on the path)
from thunder_ide.core.canvas import (RECORD_SEPARATOR, CanvasModel, RecordSplitter, key_event, key_name,
                                     mouse_event)


def frame(n, keep, shapes, width=40, height=30):
    return {"type": "frame", "n": n, "width": width, "height": height, "keep": keep, "shapes": shapes}


class RecordSplitterTests(unittest.TestCase):
    def test_records_are_separated_from_text(self):
        splitter = RecordSplitter()
        record = json.dumps(frame(1, 0, [["clear", "#000000"]]))
        text, records = splitter.feed("hello " + RECORD_SEPARATOR + record + "\nworld\n")
        self.assertEqual(text, "hello world\n")
        self.assertEqual(records, [frame(1, 0, [["clear", "#000000"]])])

    def test_a_record_may_arrive_in_pieces(self):
        splitter = RecordSplitter()
        line = RECORD_SEPARATOR + json.dumps(frame(2, 1, [["rect", 1, 2, 3, 4, "#ff0000"]])) + "\n"
        texts, records = [], []
        for i in range(0, len(line), 7):  # 7-character pieces
            text, found = splitter.feed(line[i:i + 7])
            texts.append(text)
            records += found
        self.assertEqual("".join(texts), "")
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]["keep"], 1)
        # A separator at the very end of a piece, then the record in the next piece.
        text, found = splitter.feed("a" + RECORD_SEPARATOR)
        self.assertEqual((text, found), ("a", []))
        text, found = splitter.feed('{"type": "frame"}\nb')
        self.assertEqual((text, found), ("b", [{"type": "frame"}]))

    def test_broken_records_are_dropped(self):
        splitter = RecordSplitter()
        text, records = splitter.feed(RECORD_SEPARATOR + "not json\n" + RECORD_SEPARATOR + "[1, 2]\nok")
        self.assertEqual((text, records), ("ok", []))


class CanvasModelTests(unittest.TestCase):
    def test_frames_keep_and_add_shapes(self):
        model = CanvasModel()
        self.assertEqual(model.snapshot().shapes, [])
        self.assertTrue(model.apply(frame(1, 0, [["clear", "#000000"], ["rect", 0, 0, 1, 1, "#ff0000"]])))
        self.assertTrue(model.apply(frame(2, 2, [["circle", 5, 5, 2, "#00ff00"]])))
        snapshot = model.snapshot()
        self.assertEqual((snapshot.width, snapshot.height, snapshot.frame), (40, 30, 2))
        self.assertEqual([shape[0] for shape in snapshot.shapes], ["clear", "rect", "circle"])
        self.assertTrue(model.apply(frame(3, 0, [["clear", "#ffffff"]], width=10)))
        self.assertEqual(model.snapshot().shapes, [["clear", "#ffffff"]])
        self.assertEqual(model.snapshot().width, 10)
        # Snapshots are copies: painting one while the next frame arrives is safe.
        snapshot = model.snapshot()
        model.apply(frame(4, 1, [["pixel", 1, 1, "#000000"]]))
        self.assertEqual(len(snapshot.shapes), 1)

    def test_invalid_records_change_nothing(self):
        model = CanvasModel()
        model.apply(frame(1, 0, [["clear", "#000000"]]))
        version = model.version
        for record in ({"type": "other"}, {"type": "frame"}, frame("x", 0, []),
                       {**frame(2, 0, []), "shapes": 5}):
            self.assertFalse(model.apply(record))
        self.assertEqual(model.version, version)
        model.apply(frame(2, 1, [["unknown", 1], [], "text", ["pixel", 1, 1, "#ffffff"]]))
        self.assertEqual([shape[0] for shape in model.snapshot().shapes], ["clear", "pixel"])
        model.reset()
        self.assertEqual(model.snapshot().shapes, [])

    def test_frames_from_another_thread(self):
        model = CanvasModel()

        def produce():
            model.apply(frame(1, 0, [["clear", "#000000"]]))
            for n in range(2, 500):
                model.apply(frame(n, n - 1, [["pixel", n, 0, "#ffffff"]]))

        thread = threading.Thread(target=produce)
        thread.start()
        while thread.is_alive():
            snapshot = model.snapshot()
            self.assertEqual(len(snapshot.shapes), snapshot.frame)  # always a consistent frame
        thread.join()
        self.assertEqual(model.snapshot().frame, 499)


class EventTests(unittest.TestCase):
    def test_event_lines(self):
        self.assertEqual(key_event("left", True), "\x1ekey down left\n")
        self.assertEqual(key_event("a", False), "\x1ekey up a\n")
        self.assertEqual(mouse_event(10.7, 3, True), "\x1emouse 10 3 1\n")
        self.assertEqual(mouse_event(0, 0, False), "\x1emouse 0 0 0\n")

    def test_key_names(self):
        self.assertEqual(key_name("", "left"), "left")
        self.assertEqual(key_name(" ", "space"), "space")
        self.assertEqual(key_name("A"), "a")
        self.assertEqual(key_name("7"), "7")
        self.assertEqual(key_name("ښ"), "ښ")  # letters of any script (a Pashto keyboard)
        self.assertEqual(key_name("۷"), "۷")
        self.assertIsNone(key_name("%"))
        self.assertIsNone(key_name("", "f1"))


if __name__ == "__main__":
    unittest.main()
