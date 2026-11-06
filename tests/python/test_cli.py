"""End-to-end tests of the thunderc and thundervm command-line tools."""

import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.python import support


def run(*args, input_text=None, cwd=None):
    return subprocess.run([*args], capture_output=True, text=True, encoding="utf-8", errors="replace",
                          input=input_text, cwd=cwd, env=support.environment(), timeout=120)


@support.requires_toolchain
class CliTests(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix="thunder-cli-test-"))
        self.thunderc = support.tool("thunderc")
        self.thundervm = support.tool("thundervm")

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def write(self, name, text):
        path = self.dir / name
        path.write_text(text, encoding="utf-8")
        return str(path)

    def compile(self, text, name="prog.th", *extra):
        source = self.write(name, text)
        result = run(self.thunderc, *extra, source)
        return result, str(Path(source).with_suffix(".thc"))

    # ------------------------------------------------------------------ thunderc

    def test_help_and_version(self):
        for tool in (self.thunderc, self.thundervm):
            with self.subTest(tool=tool):
                help_result = run(tool, "--help")
                self.assertEqual(help_result.returncode, 0)
                self.assertIn("Usage:", help_result.stdout)
                self.assertEqual(run(tool, "-h").returncode, 0)
                version = run(tool, "--version")
                self.assertEqual(version.returncode, 0)
                self.assertRegex(version.stdout, r"^thunder(c|vm) \d+\.\d+\.\d+")

    def test_compile_success_writes_bytecode(self):
        result, bytecode = self.compile('println("hi");')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Compiled", result.stdout)
        self.assertTrue(Path(bytecode).read_bytes().startswith(b"THND"))

    def test_explicit_output_quiet_and_no_output(self):
        source = self.write("a.th", "println(1);")
        out = self.dir / "custom.thc"
        result = run(self.thunderc, "-q", "-o", str(out), source)
        self.assertEqual((result.returncode, result.stdout), (0, ""))
        self.assertTrue(out.is_file())
        check = run(self.thunderc, "--no-output", source)
        self.assertIn("no errors", check.stdout)
        no_extension = self.write("noext", "println(1);")
        self.assertEqual(run(self.thunderc, "-q", no_extension).returncode, 0)
        self.assertTrue((self.dir / "noext.thc").is_file())

    def test_compile_errors_are_reported_with_source_context(self):
        result, _ = self.compile("var x: int = true;\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("prog.th:1:14: semantic error [S005]", result.stderr)
        self.assertIn("    1 | var x: int = true;", result.stderr)
        self.assertIn("^~~~", result.stderr)
        self.assertIn("1 error(s), 0 warning(s); compilation failed.", result.stderr)

    def test_warnings_are_printed_but_compilation_succeeds(self):
        result, _ = self.compile("func f()\n    var unused = 1\nf()\n")
        self.assertEqual(result.returncode, 0)
        self.assertIn("semantic warning [W001]", result.stderr)
        self.assertIn("1 warning(s)", result.stdout)

    def test_emit_each_stage(self):
        source = self.write("e.th", "func sq(n: int): int\n    return n * n\nprintln(sq(3))\n")
        expectations = {
            "tokens": "KwFunc         keyword      func",
            "ast": "== abstract syntax tree ==",
            "symbols": "#1 sq(int): int",
            "asm": "MUL",
            "hex": "54 48 4e 44",
        }
        for stage, expected in expectations.items():
            with self.subTest(stage=stage):
                result = run(self.thunderc, "--no-output", "-q", "--emit", stage, source)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(expected, result.stdout)
        everything = run(self.thunderc, "--no-output", "-q", "--emit", "all", source)
        for expected in expectations.values():
            self.assertIn(expected, everything.stdout)

    def test_json_report(self):
        source = self.write("j.th", "var x = 1;\nprintln(x);\n")
        result = run(self.thunderc, "--json", "--no-output", source)
        report = json.loads(result.stdout)
        self.assertTrue(report["success"])
        self.assertEqual(report["tokens"][0]["kind"], "KwVar")
        self.assertEqual(report["ast"]["kind"], "Program")
        pretty = run(self.thunderc, "--json", "--pretty", "--no-output", source)
        self.assertEqual(json.loads(pretty.stdout)["file"], source)
        self.assertIn("\n  ", pretty.stdout)

    def test_json_report_for_a_failing_program(self):
        source = self.write("bad.th", "var = 1;")
        result = run(self.thunderc, "--json", source)
        self.assertEqual(result.returncode, 1)
        report = json.loads(result.stdout)
        self.assertFalse(report["success"])
        self.assertEqual(report["diagnostics"][0]["code"], "P001")

    def test_language_info(self):
        result = run(self.thunderc, "--language-info")
        info = json.loads(result.stdout)
        self.assertIn("while", [k["text"] for k in info["keywords"]])
        self.assertIn("JUMP", [op["mnemonic"] for op in info["opcodes"]])
        self.assertEqual(json.loads(run(self.thunderc, "--language-info", "--pretty", "--lang", "en").stdout)["code"], "en")

    def test_source_from_standard_input(self):
        result = run(self.thunderc, "--no-output", "-", input_text="println(1 +);")
        self.assertEqual(result.returncode, 1)
        self.assertIn("<stdin>:1:12: syntax error [P002]", result.stderr)
        compiled = run(self.thunderc, "-q", "-", input_text="println(1);", cwd=self.dir)
        self.assertEqual(compiled.returncode, 0)
        self.assertTrue((self.dir / "out.thc").is_file())

    def test_usage_errors(self):
        cases = [
            ([], "no source file given"),
            (["--bogus"], "unknown option '--bogus'"),
            (["-o"], "missing value after -o"),
            (["--emit"], "missing value after --emit"),
            (["--emit", "nonsense", "x.th"], "--emit expects one of"),
            (["--lang"], "missing value after --lang"),
            (["--lang", "xx", "x.th"], "unknown language 'xx'"),
            (["a.th", "b.th"], "only one source file"),
            ([str(self.dir / "missing.th")], "cannot read"),
        ]
        for args, message in cases:
            with self.subTest(args=args):
                result = run(self.thunderc, *args)
                self.assertEqual(result.returncode, 2)
                self.assertIn(message, result.stderr)

    def test_unwritable_output(self):
        source = self.write("w.th", "println(1);")
        result = run(self.thunderc, "-o", str(self.dir / "no" / "such" / "dir" / "x.thc"), source)
        self.assertEqual(result.returncode, 2)
        self.assertIn("cannot write", result.stderr)

    # ------------------------------------------------------------------ thundervm

    def test_run_program(self):
        _, bytecode = self.compile('println("Hello, Thunder!");\nprintln(6 * 7);\n')
        result = run(self.thundervm, bytecode)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout.replace("\r\n", "\n"), "Hello, Thunder!\n42\n")

    def test_runtime_error(self):
        _, bytecode = self.compile("func f(d: int): int\n    return 1 / d\nprintln(f(0))\n")
        result = run(self.thundervm, bytecode)
        self.assertEqual(result.returncode, 1)
        self.assertIn("prog.th:2:14: runtime error [R001]: division by zero", result.stderr)
        self.assertIn("in f (line 2)", result.stderr)

    def test_stats_trace_and_disassembly(self):
        _, bytecode = self.compile("var x = 2 + 3;\nprintln(x);\n")
        stats = run(self.thundervm, "--stats", bytecode)
        self.assertRegex(stats.stderr, r"\[thundervm\] steps=\d+ calls=1 max_stack_depth=\d+ max_call_depth=1")
        trace = run(self.thundervm, "--trace", bytecode)
        self.assertIn("ADD", trace.stderr)
        self.assertIn("stack: [2, 3]", trace.stderr)
        listing = run(self.thundervm, "--disassemble", bytecode)
        self.assertIn("function #0 <main>", listing.stdout)

    def test_json_trace_and_result(self):
        _, bytecode = self.compile("var total = 0\nfor (var i = 0; i < 3; i = i + 1)\n    total = total + i\nprintln(total)\n")
        trace_path = self.dir / "t.json"
        result_path = self.dir / "r.json"
        result = run(self.thundervm, "--trace-json", str(trace_path), "--trace-limit", "5",
                     "--result-json", str(result_path), bytecode)
        self.assertEqual(result.returncode, 0)
        trace = json.loads(trace_path.read_text(encoding="utf-8"))
        self.assertTrue(trace["truncated"])
        self.assertEqual(len(trace["steps"]), 5)
        self.assertEqual(trace["output"][0]["text"], "3\n")
        outcome = json.loads(result_path.read_text(encoding="utf-8"))
        self.assertTrue(outcome["ok"])
        self.assertIsNone(outcome["error"])
        self.assertGreater(outcome["stats"]["steps"], 20)

    def test_result_json_for_a_runtime_error(self):
        _, bytecode = self.compile("var a: int[] = [];\nprintln(a[1]);\n")
        result_path = self.dir / "r.json"
        run(self.thundervm, "--result-json", str(result_path), bytecode)
        outcome = json.loads(result_path.read_text(encoding="utf-8"))
        self.assertFalse(outcome["ok"])
        self.assertEqual(outcome["error"]["code"], "R003")

    def test_limits(self):
        _, loop = self.compile("while (true)\n    pass\n", "loop.th")
        limited = run(self.thundervm, "--max-steps", "100", loop)
        self.assertEqual(limited.returncode, 1)
        self.assertIn("step limit of 100 instructions exceeded", limited.stderr)
        _, recursion = self.compile("func f(n: int): int\n    return f(n + 1)\nprintln(f(0))\n", "rec.th")
        deep = run(self.thundervm, "--max-depth", "50", recursion)
        self.assertIn("more than 50 nested function calls", deep.stderr)

    def test_invalid_bytecode_files(self):
        source = self.write("src.th", "println(1);")
        not_bytecode = run(self.thundervm, source)
        self.assertEqual(not_bytecode.returncode, 2)
        self.assertIn("bad magic number", not_bytecode.stderr)
        self.assertIn("compile source code with thunderc first", not_bytecode.stderr)
        empty = self.dir / "empty.thc"
        empty.write_bytes(b"")
        self.assertEqual(run(self.thundervm, str(empty)).returncode, 2)
        truncated = self.dir / "truncated.thc"
        truncated.write_bytes(b"THND\x01\x00\x05")
        result = run(self.thundervm, str(truncated))
        self.assertEqual(result.returncode, 2)
        self.assertNotIn("compile source code", result.stderr)

    def test_vm_usage_errors(self):
        _, bytecode = self.compile("println(1);")
        cases = [
            ([], "no program given"),
            (["--wat"], "unknown option '--wat'"),
            (["--trace-json"], "missing value after --trace-json"),
            (["--result-json"], "missing value after --result-json"),
            (["--trace-limit", "x", bytecode], "--trace-limit expects a number"),
            (["--trace-limit"], "missing value after --trace-limit"),
            (["--max-steps", "-1", bytecode], "--max-steps expects a number"),
            (["--max-depth", "0", bytecode], "--max-depth expects a positive number"),
            ([bytecode, bytecode], "only one program"),
            ([str(self.dir / "nope.thc")], "cannot read"),
            (["--trace-json", str(self.dir / "no" / "dir" / "t.json"), bytecode], "cannot write"),
            (["--result-json", str(self.dir / "no" / "dir" / "r.json"), bytecode], "cannot write"),
        ]
        for args, message in cases:
            with self.subTest(args=args):
                result = run(self.thundervm, *args)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn(message, result.stderr)

    def test_translate_command(self):
        source = self.write("t.th", "func f(n: int): int\n    return n * 2\nprintln(f(21), \"!\")\n")
        pashto = run(self.thunderc, "--translate", "ps", source)
        self.assertEqual(pashto.returncode, 0, pashto.stderr)
        self.assertIn("دنده f(n: عدد): عدد", pashto.stdout)
        self.assertIn("کرښه چاپ(f(۲۱), \"!\")", pashto.stdout)
        out = self.dir / "p.th"
        written = run(self.thunderc, "--translate", "pashto", "-o", str(out), source)
        self.assertEqual((written.returncode, written.stdout), (0, ""))
        compiled = run(self.thunderc, "-q", str(out))
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        executed = run(self.thundervm, str(out.with_suffix(".thc")))
        self.assertEqual(executed.stdout.replace("\r\n", "\n"), "۴۲!\n")
        back = run(self.thunderc, "--translate", "en", str(out))
        self.assertIn("println(f(21), \"!\")", back.stdout)

    def test_translate_errors(self):
        source = self.write("t.th", "var که = #\n")
        self.assertEqual(run(self.thunderc, "--translate", "xx", source).returncode, 2)
        failed = run(self.thunderc, "--translate", "ps", source)
        self.assertEqual(failed.returncode, 1)
        self.assertIn("L001", failed.stderr)
        renamed = run(self.thunderc, "--translate", "ps", self.write("r.th", "var که = 1\n"))
        self.assertIn("renamed 'که' to 'که_'", renamed.stderr)
        blocked = run(self.thunderc, "--translate", "ps", "-o", str(self.dir / "no" / "x.th"),
                      self.write("ok.th", "println(1)\n"))
        self.assertEqual(blocked.returncode, 2)

    def test_pashto_programs(self):
        source = self.write("p.th", "متغیر لړ = [۱، ۲]\nکرښه چاپ(لړ[۵])\n")
        result, bytecode = self.compile(Path(source).read_text(encoding="utf-8"), "p2.th", "--lang", "ps")
        self.assertEqual(result.returncode, 0, result.stderr)
        executed = run(self.thundervm, bytecode)
        self.assertEqual(executed.returncode, 1)
        self.assertIn("د اجرا تېروتنه [R003]", executed.stderr)
        self.assertIn("په <main> کې (کرښه ۲)", executed.stderr)
        error = run(self.thunderc, "--lang", "ps", "--no-output", self.write("e.th", "متغیر x: عدد = رښتیا\n"))
        self.assertIn("معنايي تېروتنه [S005]", error.stderr)
        listing = run(self.thunderc, "--lang", "ps", "--no-output", "-q", "--emit", "symbols", source)
        self.assertIn("عدد[]", listing.stdout)

    def test_every_pashto_sample_matches_its_golden_output(self):
        for path in sorted(support.PASHTO_SAMPLES.glob("[0-9]*.th")):
            with self.subTest(sample=path.name):
                output = self.dir / (path.stem + ".thc")
                compiled = run(self.thunderc, "-q", "-o", str(output), str(path))
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
                executed = run(self.thundervm, str(output))
                golden = (support.ROOT / "tests" / "golden" / "ps" / (path.stem + ".expected")).read_text(encoding="utf-8")
                self.assertEqual("== output ==\n" + executed.stdout.replace("\r\n", "\n"), golden)

    def test_every_sample_compiles_and_runs_through_the_command_line(self):
        for path in sorted(support.SAMPLES.glob("[0-9]*.th")):
            with self.subTest(sample=path.name):
                output = self.dir / (path.stem + ".thc")
                compiled = run(self.thunderc, "-q", "-o", str(output), str(path))
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
                executed = run(self.thundervm, str(output))
                self.assertEqual(executed.returncode, 0, executed.stderr)
                golden = (support.ROOT / "tests" / "golden" / "en" / (path.stem + ".expected")).read_text(encoding="utf-8")
                self.assertEqual("== output ==\n" + executed.stdout.replace("\r\n", "\n"), golden)


if __name__ == "__main__":
    unittest.main()
