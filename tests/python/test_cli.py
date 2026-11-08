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
        result, bytecode = self.compile('println("hi")')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Compiled", result.stdout)
        self.assertTrue(Path(bytecode).read_bytes().startswith(b"THND"))

    def test_explicit_output_quiet_and_no_output(self):
        source = self.write("a.th", "println(1)")
        out = self.dir / "custom.thc"
        result = run(self.thunderc, "-q", "-o", str(out), source)
        self.assertEqual((result.returncode, result.stdout), (0, ""))
        self.assertTrue(out.is_file())
        check = run(self.thunderc, "--no-output", source)
        self.assertIn("no errors", check.stdout)
        no_extension = self.write("noext", "println(1)")
        self.assertEqual(run(self.thunderc, "-q", no_extension).returncode, 0)
        self.assertTrue((self.dir / "noext.thc").is_file())

    def test_compile_errors_are_reported_with_source_context(self):
        result, _ = self.compile("var x: int = true\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("prog.th:1:14: semantic error [S005]", result.stderr)
        self.assertIn("    1 | var x: int = true", result.stderr)
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
        source = self.write("j.th", "var x = 1\nprintln(x)\n")
        result = run(self.thunderc, "--json", "--no-output", source)
        report = json.loads(result.stdout)
        self.assertTrue(report["success"])
        self.assertEqual(report["tokens"][0]["kind"], "KwVar")
        self.assertEqual(report["ast"]["kind"], "Program")
        pretty = run(self.thunderc, "--json", "--pretty", "--no-output", source)
        self.assertEqual(json.loads(pretty.stdout)["file"], source)
        self.assertIn("\n  ", pretty.stdout)

    def test_json_report_for_a_failing_program(self):
        source = self.write("bad.th", "var = 1")
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
        result = run(self.thunderc, "--no-output", "-", input_text="println(1 +)")
        self.assertEqual(result.returncode, 1)
        self.assertIn("<stdin>:1:12: syntax error [P002]", result.stderr)
        compiled = run(self.thunderc, "-q", "-", input_text="println(1)", cwd=self.dir)
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
        source = self.write("w.th", "println(1)")
        result = run(self.thunderc, "-o", str(self.dir / "no" / "such" / "dir" / "x.thc"), source)
        self.assertEqual(result.returncode, 2)
        self.assertIn("cannot write", result.stderr)

    # ------------------------------------------------------------------ thundervm

    def test_run_program(self):
        _, bytecode = self.compile('println("Hello, Thunder!")\nprintln(6 * 7)\n')
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
        _, bytecode = self.compile("var x = 2 + 3\nprintln(x)\n")
        stats = run(self.thundervm, "--stats", bytecode)
        self.assertRegex(stats.stderr, r"\[thundervm\] steps=\d+ calls=1 max_stack_depth=\d+ max_call_depth=1")
        trace = run(self.thundervm, "--trace", bytecode)
        self.assertIn("ADD", trace.stderr)
        self.assertIn("stack: [2, 3]", trace.stderr)
        listing = run(self.thundervm, "--disassemble", bytecode)
        self.assertIn("function #0 <main>", listing.stdout)

    def test_json_trace_and_result(self):
        _, bytecode = self.compile("var total = 0\nfor (var i = 0, i < 3, i = i + 1)\n    total = total + i\nprintln(total)\n")
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
        _, bytecode = self.compile("var a: int[] = []\nprintln(a[1])\n")
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
        source = self.write("src.th", "println(1)")
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
        _, bytecode = self.compile("println(1)")
        cases = [
            ([], "no program given"),
            (["--wat"], "unknown option '--wat'"),
            (["--trace-json"], "missing value after --trace-json"),
            (["--result-json"], "missing value after --result-json"),
            (["--trace-limit", "x", bytecode], "--trace-limit expects a number"),
            (["--trace-limit"], "missing value after --trace-limit"),
            (["--max-steps", "-1", bytecode], "--max-steps expects a number"),
            (["--max-depth", "0", bytecode], "--max-depth expects a positive number"),
            ([str(self.dir / "nope.thc")], "cannot read"),
            (["--trace-json", str(self.dir / "no" / "dir" / "t.json"), bytecode], "cannot write"),
            (["--result-json", str(self.dir / "no" / "dir" / "r.json"), bytecode], "cannot write"),
        ]
        for args, message in cases:
            with self.subTest(args=args):
                result = run(self.thundervm, *args)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn(message, result.stderr)

    # ------------------------------------------------------------------ modules, input, os

    def test_imports_are_found_next_to_the_program_and_on_the_module_path(self):
        (self.dir / "lib").mkdir()
        self.write("lib/greet.th", 'func greet(name: string): string\n    return "Hello, " + name\n')
        result, bytecode = self.compile('import "lib/greet"\nprintln(greet("Ada"))\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(run(self.thundervm, bytecode).stdout.replace("\r\n", "\n"), "Hello, Ada\n")

        elsewhere = Path(tempfile.mkdtemp(prefix="thunder-cli-main-"))
        self.addCleanup(shutil.rmtree, elsewhere, True)
        main = elsewhere / "main.th"
        main.write_text('import "greet"\nprintln(greet("Alan"))\n', encoding="utf-8")
        missing = run(self.thunderc, "-q", str(main))
        self.assertEqual(missing.returncode, 1)
        self.assertIn("S045", missing.stderr)
        found = run(self.thunderc, "-q", "-I", str(self.dir / "lib"), str(main))
        self.assertEqual(found.returncode, 0, found.stderr)
        also = run(self.thunderc, "-q", "--module-path", str(self.dir), "--module-path", str(self.dir / "lib"), str(main))
        self.assertEqual(also.returncode, 0, also.stderr)

        # Errors in a module are shown with the module's file name and source line.
        self.write("lib/bad.th", "func f(): int\n    return true\n")
        bad = run(self.thunderc, "-q", self.write("uses_bad.th", 'import "lib/bad"\n'))
        self.assertIn("lib/bad.th:2:12: semantic error [S027]", bad.stderr.replace("\\", "/"))
        self.assertIn("    return true", bad.stderr)

        report = json.loads(run(self.thunderc, "--json", "--no-output", self.write("j.th", 'import "lib/greet"\n')).stdout)
        self.assertEqual([m["name"] for m in report["modules"]], ["lib/greet.th"])
        bad_report = json.loads(run(self.thunderc, "--json", "--no-output", str(self.dir / "uses_bad.th")).stdout)
        self.assertEqual(bad_report["diagnostics"][0]["file"], 1)
        self.assertTrue(bad_report["diagnostics"][0]["file_name"].endswith("bad.th"))

    def test_programs_read_standard_input(self):
        result, bytecode = self.compile('var name = input("Name? ")\nprintln("Hi ", name)\nprintln(end_of_input(), input())\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        executed = run(self.thundervm, bytecode, input_text="Ada\r\n")
        self.assertEqual(executed.stdout.replace("\r\n", "\n"), "Name? Hi Ada\ntrue\n")

    def test_program_arguments_and_exit_status(self):
        result, bytecode = self.compile("import os\nprintln(args())\nexit(len(args()))\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        executed = run(self.thundervm, "--stats", bytecode, "one", "--two", "3")
        self.assertEqual(executed.returncode, 3, executed.stderr)
        self.assertEqual(executed.stdout.strip(), '["one", "--two", "3"]')
        self.assertIn("steps=", executed.stderr)
        separated = run(self.thundervm, "--", bytecode, "x")
        self.assertEqual((separated.returncode, separated.stdout.strip()), (1, '["x"]'))
        outcome = self.dir / "result.json"
        run(self.thundervm, "--result-json", str(outcome), bytecode, "a", "b")
        self.assertEqual(json.loads(outcome.read_text(encoding="utf-8"))["exit_code"], 2)

    def test_runtime_errors_in_modules_name_the_module(self):
        (self.dir / "lib").mkdir()
        self.write("lib/m.th", "func risky(n: int): int\n    return 10 / n\n")
        result, bytecode = self.compile('import "lib/m"\nprintln(risky(0))\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        outcome = self.dir / "result.json"
        executed = run(self.thundervm, "--result-json", str(outcome), bytecode)
        self.assertEqual(executed.returncode, 1)
        self.assertIn("lib/m.th:2:15: runtime error [R001]", executed.stderr)
        self.assertIn("in risky [lib/m.th] (line 2)", executed.stderr)
        error = json.loads(outcome.read_text(encoding="utf-8"))["error"]
        self.assertEqual((error["file_name"], error["call_stack"][1]["file_name"]), ("lib/m.th", ""))

    def test_language_info_lists_modules_of_builtins(self):
        info = json.loads(run(self.thunderc, "--language-info").stdout)
        modules = {builtin["name"]: builtin["module"] for builtin in info["builtins"]}
        self.assertEqual((modules["map"], modules["run"], modules["input"]), ("", "os", ""))
        self.assertIn("import", [keyword["text"] for keyword in info["keywords"]])

    def test_translate_command(self):
        source = self.write("t.th", "func f(n: int): int\n    return n * 2\nprintln(f(21), \"!\")\n")
        pashto = run(self.thunderc, "--translate", "ps", source)
        self.assertEqual(pashto.returncode, 0, pashto.stderr)
        self.assertIn("دنده f(n: عدد): عدد", pashto.stdout)
        self.assertIn("کرښه چاپ(f(۲۱)، \"!\")", pashto.stdout)
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
        self.assertIn("په <اصلي> کې (کرښه ۲)", executed.stderr)
        error = run(self.thunderc, "--lang", "ps", "--no-output", self.write("e.th", "متغیر x: عدد = رښتیا\n"))
        self.assertIn("معنايي تېروتنه [S005]", error.stderr)
        listing = run(self.thunderc, "--lang", "ps", "--no-output", "-q", "--emit", "symbols", source)
        self.assertIn("عدد[]", listing.stdout)

    @staticmethod
    def sample_input(language, path):
        """The input a sample reads in the golden tests (tests/golden/<language>/<name>.input), or None."""
        input_path = support.ROOT / "tests" / "golden" / language / (path.stem + ".input")
        return input_path.read_text(encoding="utf-8") if input_path.is_file() else None

    def test_every_pashto_sample_matches_its_golden_output(self):
        for path in sorted(support.PASHTO_SAMPLES.glob("[0-9]*.th")):
            with self.subTest(sample=path.name):
                output = self.dir / (path.stem + ".thc")
                compiled = run(self.thunderc, "-q", "-o", str(output), str(path))
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
                executed = run(self.thundervm, *self.SAMPLE_OPTIONS, str(output), input_text=self.sample_input("ps", path))
                golden = (support.ROOT / "tests" / "golden" / "ps" / (path.stem + ".expected")).read_text(encoding="utf-8")
                self.assertEqual("== output ==\n" + executed.stdout.replace("\r\n", "\n"), self.text_part(golden))

    def test_every_sample_compiles_and_runs_through_the_command_line(self):
        for path in sorted(support.SAMPLES.glob("[0-9]*.th")):
            with self.subTest(sample=path.name):
                output = self.dir / (path.stem + ".thc")
                compiled = run(self.thunderc, "-q", "-o", str(output), str(path))
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
                svg = self.dir / (path.stem + ".svg")
                executed = run(self.thundervm, *self.SAMPLE_OPTIONS, "--svg", str(svg), str(output),
                               input_text=self.sample_input("en", path))
                self.assertEqual(executed.returncode, 0, executed.stderr)
                golden = (support.ROOT / "tests" / "golden" / "en" / (path.stem + ".expected")).read_text(encoding="utf-8")
                self.assertEqual("== output ==\n" + executed.stdout.replace("\r\n", "\n"), self.text_part(golden))
                if "== graphics ==" in golden:  # graphics samples: the final picture was saved
                    self.assertIn("<svg", svg.read_text(encoding="utf-8"))

    # How the golden tests run samples: animations stop after 20 frames, random numbers are seeded.
    SAMPLE_OPTIONS = ("--seed", "1", "--max-frames", "20")

    @staticmethod
    def text_part(golden):
        """The program's text output in a golden transcript (without the description of its picture)."""
        return golden.split("== graphics ==\n")[0]

    def test_graphics_on_the_command_line(self):
        program = ('import graphics\ncanvas(40, 30)\nclear("black")\nvar n = 0\nwhile true\n'
                   '    circle(n, 10, 3, "red")\n    frame()\n    n = n + 1\n')
        result, bytecode = self.compile(program, "anim.th")
        self.assertEqual(result.returncode, 0, result.stderr)
        svg = self.dir / "anim.svg"
        outcome = self.dir / "result.json"
        executed = run(self.thundervm, "--max-frames", "3", "--svg", str(svg), "--result-json", str(outcome), bytecode)
        self.assertEqual(executed.returncode, 0, executed.stderr)
        self.assertEqual(svg.read_text(encoding="utf-8").count("<circle"), 3)
        result_json = json.loads(outcome.read_text(encoding="utf-8"))
        self.assertTrue(result_json["frame_limit_reached"])
        self.assertEqual(result_json["stats"]["frames"], 3)

        # --canvas: frames are records on standard output; events and input lines share standard input.
        _, keys = self.compile('import graphics\nvar a = input()\nprintln(a, " ", key_pressed(), " ", mouse_x())\n'
                               'rect(0, 0, 2, 2, "red")\n', "keys.th")
        shown = run(self.thundervm, "--canvas", keys, input_text="\x1ekey down left\n\x1emouse 9 4 0\nhello\n")
        self.assertEqual(shown.returncode, 0, shown.stderr)
        text, record = shown.stdout.split("\x1e")
        self.assertEqual(text.replace("\r\n", "\n"), "hello left 9\n")
        frame = json.loads(record)
        self.assertEqual((frame["type"], frame["shapes"]), ("frame", [["rect", 0, 0, 2, 2, "#ff0000"]]))

        for args, message in ((["--max-frames", "0", bytecode], "--max-frames expects a positive number"),
                              (["--seed", "x", bytecode], "--seed expects a number"),
                              (["--svg", str(self.dir / "no" / "dir" / "x.svg"), bytecode], "cannot write")):
            with self.subTest(args=args):
                failed = run(self.thundervm, "--max-frames", "1", *args)
                self.assertEqual(failed.returncode, 2, failed.stderr)
                self.assertIn(message, failed.stderr)

    # ------------------------------------------------------------------ packaging

    def test_package_runs_on_its_own(self):
        """thunderc --package writes an executable that needs neither thunderc nor thundervm."""
        source = self.write("app.th", 'import os\nprintln("packaged ", 6 * 7)\nprintln(args())\n')
        app = str(self.dir / "myapp")
        built = run(self.thunderc, "--package", app, source)
        self.assertEqual(built.returncode, 0, built.stderr)
        self.assertIn("Packaged", built.stdout)
        # --package alone does not also leave a .thc behind.
        self.assertFalse((self.dir / "app.thc").exists())

        executable = app + support.EXE_SUFFIX
        self.assertTrue(os.path.exists(executable))
        self.assertTrue(os.access(executable, os.X_OK))

        ran = run(executable, "one", "two")
        self.assertEqual(ran.returncode, 0, ran.stderr)
        self.assertEqual(ran.stdout.replace("\r\n", "\n"), 'packaged 42\n["one", "two"]\n')

        # The virtual machine's own options are still reachable, behind --thunder-.
        helped = run(executable, "--thunder-help")
        self.assertEqual(helped.returncode, 0)
        self.assertIn("packaged as a native executable", helped.stdout)
        self.assertIn(support.version(), run(executable, "--thunder-version").stdout)

    def test_package_a_graphics_program_and_an_exit_code(self):
        source = self.write("pic.th", 'import graphics\nimport os\ncircle(5.5, 5.5, 2.25, "red")\nshow()\nexit(3)\n')
        app = str(self.dir / "pic") + support.EXE_SUFFIX
        self.assertEqual(run(self.thunderc, "--package", app, source).returncode, 0)
        svg = self.dir / "pic.svg"
        ran = run(app, "--thunder-svg", str(svg))
        self.assertEqual(ran.returncode, 3)  # exit(3) reaches the operating system
        self.assertIn('cx="5.5" cy="5.5" r="2.25"', svg.read_text(encoding="utf-8"))

    def test_package_carries_its_native_libraries(self):
        library = Path(support.bin_dir()) / support.library_file_name("thunder_demo")
        if not library.exists():
            self.skipTest("the demo native module was not built")
        source = self.write("nat.th", 'import native "thunder_demo"\n'
                                      "native func hypot(x: float, y: float): float\n"
                                      "println(hypot(3, 4))\n")
        app = str(self.dir / "nat") + support.EXE_SUFFIX
        built = run(self.thunderc, "--package", app, source, "-I", str(support.bin_dir()))
        self.assertEqual(built.returncode, 0, built.stderr)
        self.assertIn(library.name, built.stdout)
        self.assertTrue((self.dir / library.name).exists())
        ran = run(app)
        self.assertEqual(ran.returncode, 0, ran.stderr)
        self.assertEqual(ran.stdout.replace("\r\n", "\n"), "5.0\n")

    def test_package_usage_errors(self):
        source = self.write("bad.th", "println(1)\n")
        failed = run(self.thunderc, "--package", str(self.dir / "x"), "--runtime", "no/such/vm", source)
        self.assertEqual(failed.returncode, 2, failed.stderr)
        self.assertIn("cannot read", failed.stderr)
        # A program that does not compile is never packaged.
        broken = self.write("broken.th", "var = \n")
        self.assertEqual(run(self.thunderc, "--package", str(self.dir / "y"), broken).returncode, 1)
        self.assertFalse((self.dir / "y").exists())

    # ------------------------------------------------------------------ the native interface

    def test_native_library_is_loaded_and_can_be_switched_off(self):
        library = Path(support.bin_dir()) / support.library_file_name("thunder_demo")
        if not library.exists():
            self.skipTest("the demo native module was not built")
        result, bytecode = self.compile('import native "thunder_demo"\n'
                                        "native func shout(text: string): string\n"
                                        'println(shout("hi"))\n', "n.th")
        self.assertEqual(result.returncode, 0, result.stderr)
        ran = run(self.thundervm, "--native-path", str(support.bin_dir()), bytecode)
        self.assertEqual(ran.returncode, 0, ran.stderr)
        self.assertEqual(ran.stdout.replace("\r\n", "\n"), "HI!\n")

        refused = run(self.thundervm, "--no-native", "--native-path", str(support.bin_dir()), bytecode)
        self.assertEqual(refused.returncode, 1)
        self.assertIn("R028", refused.stderr)

    # ------------------------------------------------------------------ display plug-ins

    def test_graphics_program_in_a_display_plug_in(self):
        """--display shows a graphics program outside the IDE (docs/display.md)."""
        plugin = Path(support.bin_dir()) / support.library_file_name("thunder_display_text")
        if not plugin.exists():
            self.skipTest("the example display plug-in was not built")
        _, bytecode = self.compile('import graphics\ncanvas(60, 40)\nwhile true\n'
                                   '    clear("white")\n    circle(5.5, 5, 2, "red")\n'
                                   '    println(key_pressed())\n    frame()\n', "draw.th")
        environment = dict(os.environ, THUNDER_DISPLAY_TEXT_FRAMES="3", THUNDER_DISPLAY_TEXT_KEYS="left")
        shown = subprocess.run([self.thundervm, "--display", "text", bytecode], capture_output=True,
                               text=True, encoding="utf-8", errors="replace", env=environment, timeout=120)
        self.assertEqual(shown.returncode, 0, shown.stderr)          # a closed window is a normal end
        self.assertIn("[display] frame 1", shown.stderr)
        self.assertIn("circle", shown.stderr)
        self.assertIn("left", shown.stdout)                          # the key reached the program

        missing = run(self.thundervm, "--display", "no_such_display", bytecode)
        self.assertEqual(missing.returncode, 2)
        self.assertIn("cannot load the display", missing.stderr)

    def test_package_with_a_display_plug_in(self):
        plugin = Path(support.bin_dir()) / support.library_file_name("thunder_display_text")
        if not plugin.exists():
            self.skipTest("the example display plug-in was not built")
        source = self.write("app.th", 'import graphics\ncanvas(20, 20)\nrect(0, 0, 4, 4, "red")\nshow()\n')
        app = str(self.dir / "drawapp") + support.EXE_SUFFIX
        built = run(self.thunderc, "--package", app, "--display", "text", "-I", str(support.bin_dir()), source)
        self.assertEqual(built.returncode, 0, built.stderr)
        self.assertIn(plugin.name, built.stdout)
        self.assertTrue((self.dir / plugin.name).exists())           # it travels with the program
        ran = subprocess.run([app, "--thunder-display", "text"], capture_output=True, text=True,
                             encoding="utf-8", errors="replace", env=dict(os.environ), timeout=120)
        self.assertEqual(ran.returncode, 0, ran.stderr)
        self.assertIn("[display]", ran.stderr)


if __name__ == "__main__":
    unittest.main()
