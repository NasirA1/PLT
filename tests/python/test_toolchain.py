"""Tests of the IDE's interface to the command-line tools."""

import os
import shutil
import stat
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

from tests.python import support
from thunder_ide.core import toolchain
from thunder_ide.core.toolchain import (CompileOutcome, RunResult, Toolchain, ToolchainError, find_bin_dir,
                                   format_command, repository_root)


class HelperTests(unittest.TestCase):
    def test_repository_root(self):
        self.assertEqual(repository_root(), support.ROOT)

    def test_format_command(self):
        self.assertEqual(format_command(["a", "b c", ""]), 'a "b c" ""')

    def test_find_bin_dir_reports_where_it_looked(self):
        with tempfile.TemporaryDirectory() as empty, mock.patch.dict(os.environ, {"THUNDER_BIN": empty}), \
                mock.patch.object(toolchain, "repository_root", return_value=Path(empty)):
            with self.assertRaises(ToolchainError) as context:
                find_bin_dir(empty)
            self.assertIn("were not found", str(context.exception))

    def test_compile_outcome_and_run_result_properties(self):
        outcome = CompileOutcome(report={"success": True, "diagnostics": [{"code": "W001"}], "error_count": 0,
                                         "warning_count": 1}, command=[], exit_code=0, source_path=Path("a.th"),
                                 bytecode_path=None)
        self.assertTrue(outcome.success)
        self.assertEqual(outcome.warning_count, 1)
        self.assertEqual(outcome.error_count, 0)
        self.assertEqual(outcome.diagnostics[0]["code"], "W001")
        self.assertIsNone(RunResult(exit_code=0).runtime_error)
        self.assertIsNone(RunResult(exit_code=0).stats)
        result = RunResult(exit_code=1, result={"error": {"code": "R001"}, "stats": {"steps": 3}})
        self.assertEqual(result.runtime_error["code"], "R001")
        self.assertEqual(result.stats["steps"], 3)


@support.requires_toolchain
class ToolchainTests(unittest.TestCase):
    def setUp(self):
        self.toolchain = Toolchain.locate(str(support.bin_dir()))

    def tearDown(self):
        shutil.rmtree(self.toolchain.work_dir, ignore_errors=True)

    def run_and_wait(self, bytecode, **options):
        output = []
        finished = []
        run = self.toolchain.start_run(bytecode, on_output=lambda stream, text: output.append((stream, text)),
                                       on_exit=finished.append, **options)
        self.assertTrue(run.wait(60))
        self.assertFalse(run.running)
        return output, finished[0]

    def test_paths(self):
        self.assertTrue(self.toolchain.compiler.is_file())
        self.assertTrue(self.toolchain.vm.is_file())

    def test_language_info_is_cached_per_language(self):
        info = self.toolchain.language_info()
        self.assertEqual(info["code"], "en")
        self.assertIs(self.toolchain.language_info(), info)
        pashto = self.toolchain.language_info("ps")
        self.assertTrue(pashto["right_to_left"])
        self.assertIs(self.toolchain.language_info("ps"), pashto)

    def test_translate(self):
        outcome = self.toolchain.translate("var x = 1\nprintln(x, true)\n", "ps")
        self.assertTrue(outcome.ok)
        self.assertEqual(outcome.text, "// ژبه: ps\nمتغیر x = ۱\nکرښه چاپ(x, رښتیا)\n")
        self.assertEqual(outcome.messages, "")
        failed = self.toolchain.translate("var x = #", "ps")
        self.assertFalse(failed.ok)
        self.assertIn("L001", failed.messages)

    def test_compile_in_pashto(self):
        self.toolchain.language = "ps"
        outcome = self.toolchain.compile("کرښه چاپ(۲ + ۲)\n", "p.th")
        self.assertTrue(outcome.success)
        output, result = self.run_and_wait(outcome.bytecode_path)
        self.assertEqual("".join(text for stream, text in output if stream == "stdout"), "۴\n")

    def test_compile_and_run(self):
        outcome = self.toolchain.compile('println("hi ", 1 + 1);\n', "demo.th")
        self.assertTrue(outcome.success)
        self.assertEqual(outcome.exit_code, 0)
        self.assertEqual(outcome.source_path.name, "demo.th")
        self.assertTrue(outcome.bytecode_path.is_file())
        output, result = self.run_and_wait(outcome.bytecode_path)
        self.assertEqual("".join(text for stream, text in output if stream == "stdout"), "hi 2\n")
        self.assertEqual(result.exit_code, 0)
        self.assertGreater(result.stats["steps"], 0)
        self.assertIsNone(result.trace)

    def test_compile_failure_has_no_bytecode(self):
        self.toolchain.compile("println(1);", "same.th")
        outcome = self.toolchain.compile("println(;", "same.th")
        self.assertFalse(outcome.success)
        self.assertIsNone(outcome.bytecode_path)
        self.assertEqual(outcome.diagnostics[0]["code"], "P002")
        self.assertEqual(self.toolchain.compile("", "").source_path.name, "untitled.th")

    def test_runtime_error_and_trace(self):
        outcome = self.toolchain.compile("var a = [1];\nprintln(a[5]);\n", "err.th")
        output, result = self.run_and_wait(outcome.bytecode_path, trace_limit=100, max_steps=1000)
        self.assertEqual(result.exit_code, 1)
        self.assertEqual(result.runtime_error["code"], "R003")
        self.assertTrue(any(stream == "stderr" and "R003" in text for stream, text in output))
        self.assertGreater(len(result.trace["steps"]), 0)

    def test_stop_kills_a_running_program(self):
        outcome = self.toolchain.compile("while (true)\n    pass\n", "loop.th")
        finished = threading.Event()
        results = []

        def on_exit(result):
            results.append(result)
            finished.set()

        run = self.toolchain.start_run(outcome.bytecode_path, on_output=lambda *_: None, on_exit=on_exit)
        self.assertTrue(run.running)
        run.stop()
        self.assertTrue(finished.wait(30))
        self.assertTrue(results[0].stopped)
        run.stop()  # stopping twice is harmless

    def test_unreadable_results_are_reported(self):
        outcome = self.toolchain.compile("println(1);", "r.th")
        with mock.patch("json.loads", side_effect=ValueError("bad json")):
            _, result = self.run_and_wait(outcome.bytecode_path)
        self.assertEqual(result.error, "bad json")

    def test_missing_executable_raises(self):
        broken = Toolchain(Path(tempfile.mkdtemp()))
        try:
            with self.assertRaises(ToolchainError):
                broken.compile("println(1);", "x.th")
            with self.assertRaises(ToolchainError):
                broken.language_info()
        finally:
            shutil.rmtree(broken.bin_dir, ignore_errors=True)
            shutil.rmtree(broken.work_dir, ignore_errors=True)


@unittest.skipIf(os.name == "nt", "uses shell scripts as fake tools")
class MisbehavingToolTests(unittest.TestCase):
    """The IDE must cope with tools that print garbage instead of JSON."""

    def setUp(self):
        self.bin = Path(tempfile.mkdtemp(prefix="fake-bin-"))
        for name in ("thunderc", "thundervm"):
            script = self.bin / name
            script.write_text("#!/bin/sh\necho not-json\necho oops >&2\nexit 3\n", encoding="utf-8")
            script.chmod(script.stat().st_mode | stat.S_IEXEC)
        self.toolchain = Toolchain(self.bin)

    def tearDown(self):
        shutil.rmtree(self.bin, ignore_errors=True)
        shutil.rmtree(self.toolchain.work_dir, ignore_errors=True)

    def test_garbage_output(self):
        with self.assertRaises(ToolchainError) as context:
            self.toolchain.compile("println(1);", "x.th")
        self.assertIn("oops", str(context.exception))
        with self.assertRaises(ToolchainError):
            self.toolchain.language_info()
        self.assertEqual(find_bin_dir(str(self.bin)), self.bin.resolve())


if __name__ == "__main__":
    unittest.main()
