import unittest

from tests.python import support  # noqa: F401
from thunder_ide.core.debugger import StackRow, TraceSession


def step(index, line, function="<main>", stack=(), frames=None, locals_=()):
    return {
        "step": index, "function": function, "offset": index, "line": line, "column": 1,
        "instruction": "NOP", "mnemonic": "CONST", "stack": list(stack),
        "frames": frames if frames is not None else [{"function": "<main>", "base": 0, "locals": 0}],
        "locals": [{"name": name, "value": value} for name, value in locals_], "globals": [],
    }


TRACE = {
    "steps": [step(0, 1), step(1, 1), step(2, 2), step(3, 2), step(4, 3)],
    "output": [{"step": 1, "text": "a"}, {"step": 3, "text": "b\n"}],
    "error": None,
    "truncated": False,
}


class TraceSessionTests(unittest.TestCase):
    def test_navigation(self):
        session = TraceSession(TRACE)
        self.assertEqual(session.total, 5)
        self.assertTrue(session.at_start)
        session.forward()
        self.assertEqual(session.index, 1)
        session.last()
        self.assertTrue(session.at_end)
        session.forward()  # cannot go past the end
        self.assertEqual(session.index, 4)
        session.back()
        self.assertEqual(session.current["step"], 3)
        session.first()
        session.back()  # cannot go before the start
        self.assertEqual(session.index, 0)
        session.go_to(99)
        self.assertEqual(session.index, 4)

    def test_line_stepping(self):
        session = TraceSession(TRACE)
        session.forward_line()
        self.assertEqual(session.index, 2)
        session.forward_line()
        self.assertEqual(session.index, 4)
        session.forward_line()  # already on the last line
        self.assertEqual(session.index, 4)
        session.back_line()
        self.assertEqual(session.index, 2)
        session.back_line()
        self.assertEqual(session.index, 0)
        session.back_line()
        self.assertEqual(session.index, 0)

    def test_output_so_far_only_includes_executed_steps(self):
        session = TraceSession(TRACE)
        self.assertEqual(session.output_so_far(), "")
        session.go_to(2)
        self.assertEqual(session.output_so_far(), "a")
        session.last()
        self.assertEqual(session.output_so_far(), "ab\n")

    def test_empty_trace(self):
        session = TraceSession({"steps": [], "output": []})
        self.assertIsNone(session.current)
        self.assertEqual(session.stack_rows(), [])
        session.forward_line()
        session.back_line()
        session.forward()
        self.assertEqual(session.index, 0)

    def test_stack_rows_are_attributed_to_frames(self):
        frames = [{"function": "<main>", "base": 0, "locals": 1}, {"function": "f", "base": 2, "locals": 2}]
        trace = {"steps": [step(0, 4, "f", stack=["1", "7", "3", "4", "9"], frames=frames,
                                locals_=[("a", "3"), ("b", "4")])], "output": []}
        rows = TraceSession(trace).stack_rows()
        self.assertEqual(rows, [
            StackRow(4, "9", "f: operand"),
            StackRow(3, "4", "f: local b"),
            StackRow(2, "3", "f: local a"),
            StackRow(1, "7", "<main>: operand"),
            StackRow(0, "1", "<main>: local slot 0"),
        ])


if __name__ == "__main__":
    unittest.main()
