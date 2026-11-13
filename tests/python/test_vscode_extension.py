"""The VS Code extension: its assets, its agreement with the compiler, and its own test suite.

The extension (``ide/vscode/``) is plain JavaScript with no dependencies and no build step, so
these tests check the three things that can still go wrong:

* the generated assets (grammar, snippets, language configuration, the bundled language
  information) are regenerated from ``thunderc --language-info`` and must not have drifted;
* the pieces that exist twice - tidying the layout, the problem matcher, the project templates -
  must behave exactly like the Python/C++ originals they mirror;
* the extension's own JavaScript suite must pass. It is run with whatever Node.js is available;
  VS Code ships one, so a machine that can run the extension can run its tests.
"""

from __future__ import annotations

import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
import xml.etree.ElementTree as ElementTree
from pathlib import Path

from tests.python import support

EXTENSION = support.ROOT / "ide" / "vscode"
GENERATOR = EXTENSION / "tools" / "generate_assets.py"


def find_node() -> str | None:
    """A Node.js to run the extension's tests with.

    In order: THUNDER_NODE, ``node`` on the PATH, and finally the Node that VS Code itself is
    built on (``ELECTRON_RUN_AS_NODE=1 code``), so no separate installation is needed.
    """
    if os.environ.get("THUNDER_NODE"):
        return os.environ["THUNDER_NODE"]
    found = shutil.which("node") or shutil.which("nodejs")
    if found:
        return found
    candidates: list[Path] = []
    for name in ("code", "code-insiders", "codium"):
        path = shutil.which(name)
        if path:
            # `code` on the PATH is a shell script in <installation>/bin; the Electron binary
            # that can act as Node is its grandparent's <installation>/code.
            resolved = Path(path).resolve()
            candidates += [resolved.parent.parent / name, resolved.parent.parent / "code", resolved]
    if platform.system() == "Windows":
        for base in (os.environ.get("LOCALAPPDATA", ""), os.environ.get("ProgramFiles", "")):
            if base:
                candidates.append(Path(base) / "Programs" / "Microsoft VS Code" / "Code.exe")
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK) and _runs_as_node(candidate):
            return str(candidate)
    return None


def _runs_as_node(executable: Path) -> bool:
    """True if ELECTRON_RUN_AS_NODE turns this executable into a Node.js interpreter."""
    environment = dict(os.environ, ELECTRON_RUN_AS_NODE="1")
    try:
        result = subprocess.run([str(executable), "-e", "process.stdout.write(process.version)"],
                                capture_output=True, text=True, timeout=120, env=environment)
    except (OSError, subprocess.SubprocessError):
        return False
    return result.returncode == 0 and result.stdout.strip().startswith("v")


def run_node(script: Path, *arguments: str, cwd: Path | None = None) -> subprocess.CompletedProcess:
    """Runs a JavaScript file, through VS Code's own Node if that is the only one there is."""
    node = find_node()
    assert node is not None
    environment = support.environment()
    command = [node, str(script), *arguments]
    if Path(node).name.lower().startswith(("code", "codium")):
        environment["ELECTRON_RUN_AS_NODE"] = "1"
    return subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace",
                          env=environment, cwd=cwd, timeout=600)


def find_code() -> str | None:
    """The VS Code command line, if it is installed."""
    for name in ("code", "code-insiders", "codium"):
        found = shutil.which(name)
        if found:
            return found
    if platform.system() == "Windows":
        for base in (os.environ.get("LOCALAPPDATA", ""), os.environ.get("ProgramFiles", "")):
            candidate = Path(base) / "Programs" / "Microsoft VS Code" / "bin" / "code.cmd"
            if base and candidate.is_file():
                return str(candidate)
    return None


def requires_node(test):
    return unittest.skipIf(find_node() is None,
                           "no Node.js found (install Node, or VS Code, or set THUNDER_NODE)")(test)


class ManifestTests(unittest.TestCase):
    """package.json and the files it points at."""

    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads((EXTENSION / "package.json").read_text(encoding="utf-8"))

    def test_manifest_is_valid_json_and_names_this_version(self):
        self.assertEqual(self.manifest["name"], "thunder-lang")
        cmake = (support.ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        version = re.search(r"project\s*\(\s*\w+[\s\S]*?VERSION\s+([0-9.]+)", cmake)
        assert version is not None
        toolchain = version.group(1).split(".")
        extension = self.manifest["version"].split(".")
        self.assertEqual(extension[:2], toolchain[:2],
                         "the extension's major.minor says which toolchain it was built against")
        self.assertGreaterEqual(int(extension[2]), int(toolchain[2]),
                                "the extension may have patch releases of its own")

    def test_every_contributed_file_exists(self):
        contributes = self.manifest["contributes"]
        files = [
            self.manifest["main"],
            contributes["languages"][0]["configuration"],
            contributes["grammars"][0]["path"],
            contributes["snippets"][0]["path"],
            contributes["jsonValidation"][0]["url"],
            contributes["viewsContainers"]["activitybar"][0]["icon"],
        ]
        for name in files:
            with self.subTest(file=name):
                self.assertTrue((EXTENSION / name).is_file(), f"{name} is missing")

    def test_every_javascript_file_parses(self):
        """A syntax error in any source file would break the extension on activation."""
        node = find_node()
        if node is None:
            self.skipTest("no Node.js found")
        sources = sorted((EXTENSION / "src").rglob("*.js")) + sorted((EXTENSION / "test").glob("*.js"))
        self.assertGreater(len(sources), 10)
        with tempfile.TemporaryDirectory() as directory:
            checker = Path(directory) / "check.js"
            checker.write_text(
                "const { readFileSync } = require('fs');\n"
                "let bad = 0;\n"
                "for (const file of process.argv.slice(2)) {\n"
                "  try { new (require('vm').Script)(readFileSync(file, 'utf8'), { filename: file }); }\n"
                "  catch (error) { bad++; console.error(file + ': ' + error.message); }\n"
                "}\n"
                "process.exit(bad === 0 ? 0 : 1);\n",
                encoding="utf-8")
            result = run_node(checker, *[str(source) for source in sources])
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_the_extension_needs_no_package_manager(self):
        self.assertNotIn("dependencies", self.manifest)
        self.assertNotIn("devDependencies", self.manifest)
        self.assertFalse((EXTENSION / "node_modules").exists())

    def test_the_problem_matcher_matches_what_thunderc_prints(self):
        """The $thunderc pattern must match the compiler's real output, not an idea of it."""
        matcher = self.manifest["contributes"]["problemMatchers"][0]
        self.assertEqual(matcher["name"], "thunderc")
        pattern = re.compile(matcher["pattern"][0]["regexp"])
        lines = [
            "e3.th:10:18: semantic error [S005]: cannot assign a value of type 'string' to 'count' of type 'int'",
            "e1.th:7:12: lexical error [L002]: unexpected character ';'; did you mean ','?",
            r"C:\work\e2.th:3:1: syntax error [P010]: two statements on one line",
        ]
        for line in lines:
            with self.subTest(line=line):
                match = pattern.match(line)
                self.assertIsNotNone(match, "the problem matcher does not match this line")
                groups = match.groupdict() if match.groupdict() else None
                self.assertTrue(match.group(1).endswith(".th"))
                self.assertTrue(match.group(2).isdigit())
                self.assertEqual(match.group(4), "error")
                self.assertRegex(match.group(5), r"^[A-Z]\d+$")
                self.assertTrue(match.group(6))
                self.assertIsNone(groups)

    @support.requires_toolchain
    def test_the_problem_matcher_matches_a_real_diagnostic(self):
        matcher = self.manifest["contributes"]["problemMatchers"][0]
        pattern = re.compile(matcher["pattern"][0]["regexp"])
        result = subprocess.run([support.tool("thunderc"), "--no-output", str(support.SAMPLES / "e3_type_error.th")],
                                capture_output=True, text=True, encoding="utf-8", env=support.environment())
        reported = [line for line in result.stderr.splitlines() if "error [" in line]
        self.assertTrue(reported, result.stderr)
        for line in reported:
            with self.subTest(line=line):
                self.assertIsNotNone(pattern.match(line), line)


@support.requires_toolchain
class GeneratedAssetTests(unittest.TestCase):
    """The grammar, the snippets and the bundled language information come from the compiler."""

    def test_the_generated_assets_are_up_to_date(self):
        result = subprocess.run([sys.executable, str(GENERATOR), "--check"],
                                capture_output=True, text=True, encoding="utf-8", env=support.environment())
        self.assertEqual(result.returncode, 0,
                         f"{result.stdout}{result.stderr}\n"
                         "Run: python3 ide/vscode/tools/generate_assets.py")

    def test_the_bundled_language_information_matches_the_compiler(self):
        for language in ("en", "ps"):
            with self.subTest(language=language):
                result = subprocess.run(
                    [support.tool("thunderc"), "--language-info", "--lang", language],
                    capture_output=True, text=True, encoding="utf-8", env=support.environment(), check=True)
                live = json.loads(result.stdout)
                bundled = json.loads(
                    (EXTENSION / "data" / f"language-info.{language}.json").read_text(encoding="utf-8"))
                self.assertEqual(live, bundled)

    def test_the_grammar_mentions_every_reserved_word(self):
        grammar = json.loads((EXTENSION / "syntaxes" / "thunder.tmLanguage.json").read_text(encoding="utf-8"))
        text = json.dumps(grammar, ensure_ascii=False)
        for language in ("en", "ps"):
            info = json.loads((EXTENSION / "data" / f"language-info.{language}.json").read_text(encoding="utf-8"))
            for keyword in info["keywords"]:
                for spelling in keyword["spellings"]:
                    with self.subTest(language=language, word=spelling):
                        self.assertIn(spelling, text)
            for builtin in info["builtins"]:
                with self.subTest(language=language, builtin=builtin["name"]):
                    # A built-in written as several words appears with \s+ between them.
                    self.assertIn(builtin["name"].split(" ")[0], text)

    def test_the_grammar_is_structurally_sound(self):
        """Every include resolves, every begin has an end, and no capture is out of range."""
        grammar = json.loads((EXTENSION / "syntaxes" / "thunder.tmLanguage.json").read_text(encoding="utf-8"))
        repository = grammar["repository"]

        def group_count(expression: str) -> int:
            count = 0
            index = 0
            while index < len(expression):
                character = expression[index]
                if character == "\\":
                    index += 2
                    continue
                if character == "[":  # a character class: brackets inside it are literal
                    index += 1
                    while index < len(expression) and expression[index] != "]":
                        index += 2 if expression[index] == "\\" else 1
                if character == "(" and not expression[index + 1:index + 2] == "?":
                    count += 1
                index += 1
            return count

        def check(rule: dict, where: str) -> None:
            if "include" in rule:
                name = rule["include"].lstrip("#")
                self.assertIn(name, repository, f"{where}: include #{name} does not exist")
            if "begin" in rule:
                self.assertIn("end", rule, f"{where}: begin without end")
            for key in ("match", "begin", "end"):
                if key not in rule:
                    continue
                self.assertIsInstance(rule[key], str)
                captures = rule.get("captures" if key == "match" else f"{key}Captures", {})
                for group in captures:
                    self.assertLessEqual(int(group), group_count(rule[key]),
                                         f"{where}: capture {group} is beyond the groups of {key}")
            for child in rule.get("patterns", []):
                check(child, where)

        for rule in grammar["patterns"]:
            check(rule, "patterns")
        for name, rule in repository.items():
            check(rule, f"repository.{name}")

    def test_the_grammar_patterns_match_real_lines(self):
        """The patterns are checked against English and Pashto lines, not only for valid syntax.

        VS Code matches them with Oniguruma, which is not available here, so they are translated
        into their Python equivalents first (the parts that differ are the character class for a
        name and the POSIX brackets). That is enough to catch an alternation that matches
        nothing - a grammar that compiles but highlights nothing would otherwise pass unnoticed.
        """
        grammar = json.loads((EXTENSION / "syntaxes" / "thunder.tmLanguage.json").read_text(encoding="utf-8"))
        word_class = r"[[:word:]\p{L}\p{M}\p{Nd}_]"

        def as_python(pattern: str) -> str:
            return (pattern.replace(word_class, r"\w")
                    .replace(r"[[:alpha:]\p{L}]", r"[^\W\d]")
                    .replace("[[:word:]]", r"\w"))

        def patterns_of(rule: dict) -> list[str]:
            return [as_python(entry["match"]) for entry in rule.get("patterns", [rule]) if "match" in entry]

        # Every pattern must at least be a regular expression.
        for name, rule in grammar["repository"].items():
            for entry in rule.get("patterns", [rule]):
                for key in ("match", "begin", "end"):
                    if key in entry:
                        with self.subTest(rule=name, key=key):
                            re.compile(as_python(entry[key]))

        expected = {
            "keyword": ["func f()", "var x = 1", "return true", "while x < 2",
                        "دنده ن()", "متغیر ن = ۱", "که ۱ < ۲", "راګرځه رښتیا"],
            "number": ["x = 0x2A", "y = 0b1010", "z = 3.5", "h = 0XfF", "ن = ۱۲۳", "ع = ۱٫۵"],
            "declaration": ["func area(r: Rectangle): int", "struct Point", "var total = 0",
                            "جوړښت ټکی", "دنده سلام()", "متغیر ټولټال = ۰"],
            "string": ['x = "text"', "ن = «متن»"],
            "comment": ["// a note", "// یادښت"],
            "type-annotation": ["x: int", "ن: عدد", "r: Rectangle"],
        }
        for name, lines in expected.items():
            rules = patterns_of(grammar["repository"][name]) or [
                as_python(entry["begin"]) for entry in grammar["repository"][name].get("patterns", [])
                if "begin" in entry]
            for line in lines:
                with self.subTest(rule=name, line=line):
                    self.assertTrue(any(re.search(pattern, line) for pattern in rules),
                                    f"the {name} patterns match nothing in {line!r}")

    def test_the_snippets_are_written_in_the_language_they_claim(self):
        snippets = json.loads((EXTENSION / "snippets" / "thunder.code-snippets").read_text(encoding="utf-8"))
        self.assertGreaterEqual(len(snippets), 20)
        words = {}
        for language in ("en", "ps"):
            info = json.loads((EXTENSION / "data" / f"language-info.{language}.json").read_text(encoding="utf-8"))
            words[language] = ({spelling for keyword in info["keywords"] for spelling in keyword["spellings"]}
                               | {builtin["name"] for builtin in info["builtins"]}
                               | {module["name"] for module in info["modules"]})
        for name, snippet in snippets.items():
            language = name.rsplit(".", 1)[1]
            with self.subTest(snippet=name):
                self.assertTrue(snippet["prefix"], "a snippet needs a prefix")
                self.assertEqual(snippet["scope"], "thunder")
                first = snippet["body"][0].lstrip().split("(")[0].split(" ")[0]
                self.assertIn(first, words[language],
                              f"{name} starts with {first!r}, which is not a word of {language}")

    def test_the_snippets_are_syntactically_programs(self):
        """A snippet is a sketch: its placeholders are names the programmer still has to fill in,
        so the compiler will complain that they are not declared. What must never happen is a
        *lexical or syntax* error - that would mean the snippet is not Thunder at all."""
        snippets = json.loads((EXTENSION / "snippets" / "thunder.code-snippets").read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as directory:
            for name, snippet in snippets.items():
                language = name.rsplit(".", 1)[1]
                body = "\n".join(snippet["body"])
                # ${1:word} -> word, $0 and $1 -> nothing (they mark where the caret goes).
                filled = re.sub(r"\$\{\d+:([^}]*)\}", r"\1", body)
                filled = re.sub(r"\$\d+", "", filled)
                if "import" in name:
                    continue  # it would name a module that does not exist here
                source = Path(directory) / f"{name.replace('.', '_')}.th"
                prefix = "// lang: ps\n" if language == "ps" else ""
                source.write_text(prefix + filled + "\n", encoding="utf-8")
                with self.subTest(snippet=name):
                    result = subprocess.run(
                        [support.tool("thunderc"), "--json", "--no-output", "--lang", language, str(source)],
                        capture_output=True, text=True, encoding="utf-8", env=support.environment())
                    report = json.loads(result.stdout)
                    broken = [diagnostic for diagnostic in report["diagnostics"]
                              if diagnostic["phase"] in ("lexical", "syntax")]
                    self.assertEqual(broken, [], f"{filled}\n{result.stderr}")


@support.requires_toolchain
class TemplateTests(unittest.TestCase):
    """The programs a new project starts from must compile and run in both languages."""

    def test_each_template_compiles_and_runs(self):
        for language, expected in (("en", "Hello, world!"), ("ps", "سلام، نړۍ!")):
            with self.subTest(language=language):
                with tempfile.TemporaryDirectory() as directory:
                    target = Path(directory)
                    shutil.copytree(EXTENSION / "templates" / language, target, dirs_exist_ok=True)
                    bytecode = target / "main.thc"
                    compiled = subprocess.run(
                        [support.tool("thunderc"), "-o", str(bytecode), str(target / "main.th")],
                        capture_output=True, text=True, encoding="utf-8", env=support.environment())
                    self.assertEqual(compiled.returncode, 0, compiled.stderr)
                    run = subprocess.run([support.tool("thundervm"), str(bytecode)],
                                         capture_output=True, text=True, encoding="utf-8",
                                         env=support.environment())
                    self.assertEqual(run.returncode, 0, run.stderr)
                    self.assertIn(expected, run.stdout)


@requires_node
class TidyTests(unittest.TestCase):
    """The extension's "tidy layout" must agree with the IDE's, character for character."""

    def test_tidy_matches_the_python_implementation_on_every_sample(self):
        from thunder_ide.core import edits

        sources = sorted(support.SAMPLES.glob("*.th")) + sorted(support.PASHTO_SAMPLES.glob("*.th"))
        self.assertGreater(len(sources), 40)
        texts = [source.read_text(encoding="utf-8") for source in sources]
        # A few layouts the samples do not contain.
        texts += ["", "\n\n\n", "a\t\tb   \n\n\n\nc", "   \n  x  \n", "func f()\n\tprintln(1)\n"]
        with tempfile.TemporaryDirectory() as directory:
            payload = Path(directory) / "input.json"
            payload.write_text(json.dumps(texts), encoding="utf-8")
            script = Path(directory) / "tidy.js"
            script.write_text(
                "const { tidy } = require(process.argv[2]);\n"
                "const texts = JSON.parse(require('fs').readFileSync(process.argv[3], 'utf8'));\n"
                "process.stdout.write(JSON.stringify(texts.map(tidy)));\n",
                encoding="utf-8")
            result = run_node(script, str(EXTENSION / "src" / "edits.js"), str(payload))
            self.assertEqual(result.returncode, 0, result.stderr)
            tidied = json.loads(result.stdout)
        for source, text, javascript in zip(sources + [None] * 5, texts, tidied):
            with self.subTest(file=source.name if source else text[:12]):
                self.assertEqual(edits.tidy(text), javascript)


@support.requires_toolchain
class SampleProjectTests(unittest.TestCase):
    """samples/project: the multi-file project the VS Code extension is tried on.

    It is not a lesson, so the IDE must not offer it and no golden transcript covers it - but it
    still has to compile and run, or the first thing anyone tries the extension on is broken.
    """

    PROJECT = support.ROOT / "samples" / "project"

    def test_the_project_has_the_shape_the_extension_expects(self):
        for name in ("thunder.json", "main.th", "README.md",
                     "lib/geometry.th", "lib/statistics.th",
                     ".vscode/tasks.json", ".vscode/launch.json",
                     "pashto/thunder.json", "pashto/main.th", "pashto/lib/shapes.th"):
            with self.subTest(file=name):
                self.assertTrue((self.PROJECT / name).is_file(), f"{name} is missing")
        project = json.loads((self.PROJECT / "thunder.json").read_text(encoding="utf-8"))
        self.assertEqual(project["entry"], "main.th")
        self.assertEqual(project["modulePaths"], ["lib"])
        self.assertTrue(project["arguments"], "the arguments show up in args()")
        pashto = json.loads((self.PROJECT / "pashto" / "thunder.json").read_text(encoding="utf-8"))
        self.assertEqual(pashto["language"], "ps")
        # The tasks and launch configurations must be the extension's own kinds.
        tasks = json.loads((self.PROJECT / ".vscode" / "tasks.json").read_text(encoding="utf-8"))
        self.assertTrue(all(task["type"] == "thunder" for task in tasks["tasks"]))
        launch = json.loads((self.PROJECT / ".vscode" / "launch.json").read_text(encoding="utf-8"))
        self.assertTrue(all(entry["type"] == "thunder" for entry in launch["configurations"]))

    def test_the_ide_does_not_offer_it_as_a_sample(self):
        from thunder_ide.core import samples

        for language in ("en", "ps"):
            names = [sample.name for sample in samples.list_samples(language)]
            with self.subTest(language=language):
                self.assertTrue(names, "the lessons are still there")
                self.assertNotIn("main.th", names, "the project is not a lesson")
                self.assertTrue(all(name[0].isdigit() or name.startswith("e") for name in names),
                                f"unexpected entries in the {language} sample list: {names}")

    def test_each_project_compiles_and_runs(self):
        for folder, expected in ((self.PROJECT, ["total area:   51", "average area: 17.0", "0x33"]),
                                 (self.PROJECT / "pashto", ["ټول مساحت: ۵۱", "۴×۳"])):
            with self.subTest(project=folder.name):
                with tempfile.TemporaryDirectory() as directory:
                    bytecode = Path(directory) / "main.thc"
                    compiled = subprocess.run(
                        [support.tool("thunderc"), "-o", str(bytecode), str(folder / "main.th")],
                        capture_output=True, text=True, encoding="utf-8", env=support.environment())
                    self.assertEqual(compiled.returncode, 0, compiled.stderr)
                    run = subprocess.run([support.tool("thundervm"), str(bytecode), "kitchen", "study"],
                                         capture_output=True, text=True, encoding="utf-8",
                                         env=support.environment())
                    self.assertEqual(run.returncode, 0, run.stderr)
                    for line in expected:
                        self.assertIn(line, run.stdout)

    def test_the_pashto_project_is_written_in_pashto(self):
        """The repository's rule for Pashto programs: no Latin names, no ASCII commas."""
        for path in sorted((self.PROJECT / "pashto").rglob("*.th")):
            text = path.read_text(encoding="utf-8")
            with self.subTest(file=path.name):
                self.assertTrue(text.startswith("// ژبه: ps"), "it must say which language it is in")
                for number, line in enumerate(text.splitlines(), start=1):
                    # Comments and the contents of strings may be anything; the code may not.
                    code = re.sub(r'"[^"]*"', '""', line.split("//")[0])
                    self.assertNotIn(",", code, f"line {number} uses an ASCII comma: {line}")
                    self.assertIsNone(re.search(r"[A-Za-z]", code),
                                      f"line {number} uses a Latin name: {line}")


@requires_node
class DirectionTests(unittest.TestCase):
    """The extension's right-to-left layout must be the IDE's, line for line.

    `ide/vscode/src/direction.js` is a port of `ide/thunder_ide/core/direction.py`: it decides
    where the Unicode isolate marks go so that a Pashto program is read token by token in its
    own direction. Two implementations of one algorithm drift apart unless something compares
    them, so this runs both over every line of every sample - and over the characters whose
    bidirectional class the port has to know exactly.
    """

    def _compare(self, lines):
        """Runs direction.js over (text, right_to_left) pairs and returns its answers."""
        with tempfile.TemporaryDirectory() as directory:
            payload = Path(directory) / "lines.json"
            payload.write_text(json.dumps(lines, ensure_ascii=False), encoding="utf-8")
            script = Path(directory) / "layout.js"
            script.write_text(
                "const { analyseLine } = require(process.argv[2]);\n"
                "const lines = JSON.parse(require('fs').readFileSync(process.argv[3], 'utf8'));\n"
                "process.stdout.write(JSON.stringify(lines.map(([text, rtl]) => {\n"
                "    const layout = analyseLine(text, rtl);\n"
                "    return [layout.display, layout.offsets,\n"
                "            layout.tokens.map(t => [t.start, t.end, t.opposite])];\n"
                "})));\n",
                encoding="utf-8")
            result = run_node(script, str(EXTENSION / "src" / "direction.js"), str(payload))
            self.assertEqual(result.returncode, 0, result.stderr)
            return json.loads(result.stdout)

    def test_every_sample_line_is_laid_out_exactly_as_the_ide_lays_it_out(self):
        from thunder_ide.core import direction

        lines: list[list] = []
        for folder, right_to_left in ((support.PASHTO_SAMPLES, True), (support.SAMPLES, False),
                                      (support.ROOT / "samples" / "project", False)):
            for path in sorted(folder.rglob("*.th")):
                pashto = path.read_text(encoding="utf-8").startswith("// ژبه")
                for line in path.read_text(encoding="utf-8").splitlines():
                    lines.append([line, pashto or right_to_left])
        # And the awkward ones the samples happen not to contain.
        lines += [
            ["متغیر ن = -۳", True],
            ["var x = -3", False],
            ["// درجه -۵ ده", True],
            ["کرښه چاپ(\"a\"، ۹٫۸۱، -۲)", True],
            ["متغیر س = «متن -۷ او name»", True],
            ["x = a-3", False],
            ["راګرځه ۱۲۳", True],
            ["", True],
            ["    ", True],
            ["var نوم = 1", False],
            ["متغیر name = \"Nasir\"", True],
        ]
        self.assertGreater(len(lines), 2000)

        answers = self._compare(lines)
        for (text, right_to_left), (display, offsets, tokens) in zip(lines, answers):
            layout = direction.analyse_line(text, right_to_left)
            with self.subTest(line=text[:60], right_to_left=right_to_left):
                self.assertEqual(layout.display, display)
                self.assertEqual(list(layout.offsets), offsets)
                self.assertEqual([(token.start, token.end, token.opposite) for token in layout.tokens],
                                 [tuple(token) for token in tokens])

    def test_the_direction_tables_still_match_pythons_unicodedata(self):
        """The R/AL and AN character ranges in direction.js are derived from unicodedata."""
        import unicodedata

        source = (EXTENSION / "src" / "direction.js").read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "classify.js"
            script.write_text(
                "const d = require(process.argv[2]);\n"
                "const points = JSON.parse(require('fs').readFileSync(process.argv[3], 'utf8'));\n"
                "process.stdout.write(JSON.stringify(points.map((cp) => {\n"
                "    const c = String.fromCodePoint(cp);\n"
                "    return [d.isStrongRightToLeft(c), d.ARABIC_NUMBER.test(c)];\n"
                "})));\n",
                encoding="utf-8")
            # Every character a Thunder program can hold: ASCII, Latin-1, the Arabic and Hebrew
            # blocks, Arabic presentation forms, and the marks the algorithm itself uses.
            points = (list(range(0x20, 0x300)) + list(range(0x590, 0x700)) + list(range(0x750, 0x780))
                      + list(range(0x8A0, 0x900)) + list(range(0xFB1D, 0xFE00))
                      + list(range(0x2000, 0x2070)))
            payload = Path(directory) / "points.json"
            payload.write_text(json.dumps(points), encoding="utf-8")
            result = run_node(script, str(EXTENSION / "src" / "direction.js"), str(payload))
            self.assertEqual(result.returncode, 0, result.stderr)
            answers = json.loads(result.stdout)

        self.assertIn("derived from unicodedata", source)
        version = re.search(r'UNICODE_VERSION = "([0-9.]+)"', source).group(1)
        for code_point, (strong, arabic_number) in zip(points, answers):
            character = chr(code_point)
            # An unassigned code point has no character to classify - and newer Pythons give
            # it a *default* bidi class (U+0590 is "R" in Python 3.15), so it is skipped.
            if unicodedata.category(character) == "Cn":
                continue
            # The tables were derived from one Unicode version. Another Python may know a newer
            # one, which can assign new characters in these blocks; the characters every
            # version knows (those of Unicode 3.2, Pashto's letters among them) are compared.
            if unicodedata.unidata_version != version and unicodedata.ucd_3_2_0.category(character) == "Cn":
                continue
            bidi = unicodedata.bidirectional(character)
            with self.subTest(character=f"U+{code_point:04X}", bidi=bidi):
                self.assertEqual(strong, bidi in ("R", "AL"))
                self.assertEqual(arabic_number, bidi == "AN")


@requires_node
class JavaScriptSuiteTests(unittest.TestCase):
    """The extension's own tests (ide/vscode/test), run with the toolchain this suite built."""

    def test_the_extension_suite_passes(self):
        result = run_node(EXTENSION / "test" / "run.js", cwd=EXTENSION)
        output = result.stdout + result.stderr
        print(output, flush=True)
        self.assertEqual(result.returncode, 0, output)
        self.assertRegex(output, r"\d+ passed, 0 failed")
        passed = int(re.search(r"(\d+) passed", output).group(1))
        self.assertGreater(passed, 100, "the extension's suite should not shrink")
        if support.bin_dir() is not None:
            self.assertNotIn("skipped: thunderc", output, "the toolchain was built, so nothing should be skipped")


class RightToLeftEditorBrowserTests(unittest.TestCase):
    """The right-to-left editor in a real browser engine (Chromium, through Qt WebEngine).

    The editor's decisions are tested in Node (ide/vscode/test/rtleditor.test.js); what only a
    browser can show is tested here - that a Pashto line really reads from the right on screen,
    that a Latin name inside it is not reversed, that a negative number keeps its sign on the
    left, and that typing, Enter, Backspace, Tab, undo, completion, a change from another editor
    and the compiler's problems behave as they should. The scenarios are in
    tests/python/rtleditor_browser.py, which runs in a process of its own.
    """

    @classmethod
    def setUpClass(cls):
        import importlib.util

        if importlib.util.find_spec("PySide6") is None or importlib.util.find_spec("PySide6.QtWebEngineWidgets") is None:
            raise unittest.SkipTest("Qt WebEngine (PySide6) is not installed")
        environment = dict(os.environ, QT_QPA_PLATFORM="offscreen")
        completed = subprocess.run([sys.executable, str(Path(__file__).with_name("rtleditor_browser.py"))],
                                   capture_output=True, text=True, encoding="utf-8", errors="replace",
                                   env=environment, timeout=300)
        lines = [line for line in completed.stdout.splitlines() if line.startswith("{")]
        if not lines:
            # The browser engine could not start here (no graphics stack, say): that is about
            # the machine, not the editor.
            raise unittest.SkipTest("the browser engine did not start: " + completed.stderr.strip()[-300:])
        cls.results = json.loads(lines[-1])

    def test_every_scenario_passes(self):
        self.assertGreaterEqual(len(self.results), 14)
        for name, result in self.results.items():
            with self.subTest(scenario=name):
                self.assertTrue(result["ok"], json.dumps(result["detail"], ensure_ascii=False))


class SourceControlTests(unittest.TestCase):
    """No part of the extension may be invisible to git.

    This has gone wrong twice with patterns borrowed from Visual Studio's .gitignore template:
    ``[Ww]in32/`` once hid the Win32 display plug-in's *source*, and ``[Dd]ebug/`` hid
    ``ide/vscode/src/debug/`` - the debug adapter, the trace model and the launcher - which
    would have reached GitHub as an extension that cannot debug anything.
    """

    @unittest.skipIf(shutil.which("git") is None, "git is not installed")
    def test_every_file_of_the_extension_is_visible_to_git(self):
        inside_repository = subprocess.run(["git", "rev-parse", "--is-inside-work-tree"],
                                           cwd=support.ROOT, capture_output=True, text=True)
        if inside_repository.returncode != 0:
            self.skipTest("not a git working tree")
        files = [path for path in EXTENSION.rglob("*")
                 if path.is_file() and "__pycache__" not in path.parts and "node_modules" not in path.parts
                 and path.suffix != ".vsix"]   # a package the installers build on demand
        self.assertGreater(len(files), 40)
        result = subprocess.run(["git", "check-ignore", "--stdin"], cwd=support.ROOT,
                                input="\n".join(str(path) for path in files),
                                capture_output=True, text=True)
        ignored = [line for line in result.stdout.splitlines() if line.strip()]
        self.assertEqual(ignored, [], "these files would never reach the repository")


class PackageTests(unittest.TestCase):
    """The .vsix the installers build must be a package VS Code can install.

    `vsce` (the official packer) needs Node.js and npm; the toolchain needs neither, so
    ide/vscode/tools/make_vsix.py writes the Open Packaging Conventions archive itself. These
    tests check the archive's shape, and - when VS Code is installed - that VS Code really does
    install it (into a throwaway extensions directory, never the user's own).
    """

    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.mkdtemp(prefix="thunder-vsix-")
        result = subprocess.run([sys.executable, str(EXTENSION / "tools" / "make_vsix.py"),
                                 "--output", cls.directory],
                                capture_output=True, text=True, encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
        packages = list(Path(cls.directory).glob("*.vsix"))
        assert len(packages) == 1, packages
        cls.package = packages[0]
        cls.manifest = json.loads((EXTENSION / "package.json").read_text(encoding="utf-8"))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.directory, ignore_errors=True)

    def test_the_package_is_named_after_the_extension_and_its_version(self):
        self.assertEqual(self.package.name, f"{self.manifest['name']}-{self.manifest['version']}.vsix")

    def test_the_package_holds_the_three_things_a_vsix_must_hold(self):
        with zipfile.ZipFile(self.package) as archive:
            names = archive.namelist()
            self.assertIn("extension.vsixmanifest", names)
            self.assertIn("[Content_Types].xml", names)
            self.assertIn("extension/package.json", names)
            self.assertIn("extension/src/extension.js", names)
            self.assertIn("extension/syntaxes/thunder.tmLanguage.json", names)
            self.assertIn("extension/templates/ps/main.th", names)
            # The right-to-left editor: its page, its model, the direction rules and its font.
            for name in ("media/rtleditor/editor.js", "media/rtleditor/model.js", "media/rtleditor/editor.css",
                         "media/fonts/Vazirmatn[wght].ttf", "media/fonts/Vazirmatn-OFL.txt", "src/direction.js"):
                self.assertIn(f"extension/{name}", names)
            # The tests, the generators and the installers are not part of a package.
            self.assertFalse([name for name in names if name.startswith("extension/test/")])
            self.assertFalse([name for name in names if name.startswith("extension/tools/")])
            self.assertFalse([name for name in names if name.startswith("extension/scripts/")])
            # Everything it ships must be the file on disk, byte for byte. (Bytes, not text:
            # on Windows git checks the file out with CRLF, and reading it as text would hide that.)
            packaged = archive.read("extension/package.json")
            self.assertEqual(packaged, (EXTENSION / "package.json").read_bytes())

    def test_the_vsix_manifest_identifies_the_extension(self):
        with zipfile.ZipFile(self.package) as archive:
            xml = archive.read("extension.vsixmanifest").decode("utf-8")
        root = ElementTree.fromstring(xml)
        namespace = {"v": "http://schemas.microsoft.com/developer/vsx-schema/2011"}
        identity = root.find("./v:Metadata/v:Identity", namespace)
        self.assertIsNotNone(identity, xml)
        self.assertEqual(identity.get("Id"), self.manifest["name"])
        self.assertEqual(identity.get("Version"), self.manifest["version"])
        self.assertEqual(identity.get("Publisher"), self.manifest["publisher"])
        target = root.find("./v:Installation/v:InstallationTarget", namespace)
        self.assertEqual(target.get("Id"), "Microsoft.VisualStudio.Code")
        properties = {entry.get("Id"): entry.get("Value")
                      for entry in root.findall("./v:Metadata/v:Properties/v:Property", namespace)}
        self.assertEqual(properties["Microsoft.VisualStudio.Code.Engine"], self.manifest["engines"]["vscode"])
        assets = {entry.get("Type"): entry.get("Path")
                  for entry in root.findall("./v:Assets/v:Asset", namespace)}
        self.assertEqual(assets["Microsoft.VisualStudio.Code.Manifest"], "extension/package.json")

    def test_every_packaged_file_extension_has_a_content_type(self):
        with zipfile.ZipFile(self.package) as archive:
            names = archive.namelist()
            xml = archive.read("[Content_Types].xml").decode("utf-8")
        root = ElementTree.fromstring(xml)
        namespace = {"c": "http://schemas.openxmlformats.org/package/2006/content-types"}
        declared = {entry.get("Extension").lower()
                    for entry in root.findall("./c:Default", namespace)}
        for name in names:
            suffix = Path(name).suffix.lower()
            if suffix:
                with self.subTest(file=name):
                    self.assertIn(suffix.lstrip("."), {extension.lstrip(".") for extension in declared})

    @unittest.skipIf(find_code() is None, "VS Code is not installed")
    def test_vs_code_installs_the_package(self):
        """The real test: VS Code unpacks it and then lists it as installed."""
        code = find_code()
        with tempfile.TemporaryDirectory() as extensions:
            install = subprocess.run(
                [code, "--extensions-dir", extensions, "--install-extension", str(self.package), "--force"],
                capture_output=True, text=True, encoding="utf-8", timeout=600)
            self.assertEqual(install.returncode, 0, install.stdout + install.stderr)
            listed = subprocess.run([code, "--extensions-dir", extensions, "--list-extensions", "--show-versions"],
                                    capture_output=True, text=True, encoding="utf-8", timeout=600)
            identifier = f"{self.manifest['publisher']}.{self.manifest['name']}"
            self.assertIn(f"{identifier}@{self.manifest['version']}", listed.stdout)
            # VS Code keeps its own index of what is installed; a package it installed is in it.
            index = json.loads((Path(extensions) / "extensions.json").read_text(encoding="utf-8"))
            entry = next(item for item in index if item["identifier"]["id"] == identifier)
            self.assertEqual(entry["metadata"]["source"], "vsix")


class InstallScriptTests(unittest.TestCase):
    """Installing the extension must work without npm, a package manager or a network."""

    @unittest.skipIf(os.name == "nt", "install.sh is the POSIX installer")
    def test_install_sh_copies_links_and_removes_the_extension(self):
        script = EXTENSION / "scripts" / "install.sh"
        self.assertTrue(os.access(script, os.X_OK), "install.sh should be executable")
        with tempfile.TemporaryDirectory() as directory:
            installed = subprocess.run(["bash", str(script), "--copy", "--target", directory],
                                       capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(installed.returncode, 0, installed.stderr)
            destination = Path(directory) / "thunder.thunder-lang"
            self.assertTrue((destination / "package.json").is_file())
            self.assertTrue((destination / "src" / "extension.js").is_file())
            self.assertTrue((destination / "syntaxes" / "thunder.tmLanguage.json").is_file())
            self.assertFalse((destination / "test").exists(), "the tests are not part of an installation")

            linked = subprocess.run(["bash", str(script), "--link", "--target", directory],
                                    capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(linked.returncode, 0, linked.stderr)
            self.assertTrue(destination.is_symlink())

            removed = subprocess.run(["bash", str(script), "--uninstall", "--target", directory],
                                     capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(removed.returncode, 0, removed.stderr)
            self.assertFalse(destination.exists())

    @unittest.skipIf(os.name == "nt", "install.sh is the POSIX installer")
    @unittest.skipIf(find_code() is None, "VS Code is not installed")
    def test_install_sh_packages_and_installs_through_vs_code(self):
        """The default route: build a .vsix and let VS Code install it."""
        script = EXTENSION / "scripts" / "install.sh"
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(["bash", str(script), "--target", directory],
                                    capture_output=True, text=True, encoding="utf-8", timeout=900)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Installed thunder.thunder-lang@", result.stdout)
            self.assertTrue((Path(directory) / "extensions.json").is_file())
        for leftover in EXTENSION.glob("*.vsix"):
            leftover.unlink()

    @unittest.skipIf(os.name != "nt" and shutil.which("pwsh") is None, "PowerShell is not available")
    def test_install_ps1_copies_and_removes_the_extension(self):
        script = EXTENSION / "scripts" / "install.ps1"
        shell = "powershell" if os.name == "nt" else "pwsh"
        with tempfile.TemporaryDirectory() as directory:
            installed = subprocess.run(
                [shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script),
                 "-Copy", "-Target", directory],
                capture_output=True, text=True, encoding="utf-8", timeout=900)
            self.assertEqual(installed.returncode, 0, installed.stdout + installed.stderr)
            destination = Path(directory) / "thunder.thunder-lang"
            self.assertTrue((destination / "package.json").is_file())
            removed = subprocess.run(
                [shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script),
                 "-Target", directory, "-Uninstall"],
                capture_output=True, text=True, encoding="utf-8", timeout=900)
            self.assertEqual(removed.returncode, 0, removed.stdout + removed.stderr)
            self.assertFalse(destination.exists())

    @unittest.skipIf(os.name != "nt", "the packaged route on Windows needs PowerShell and VS Code")
    @unittest.skipIf(find_code() is None, "VS Code is not installed")
    def test_install_ps1_packages_and_installs_through_vs_code(self):
        script = EXTENSION / "scripts" / "install.ps1"
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script),
                 "-Target", directory],
                capture_output=True, text=True, encoding="utf-8", timeout=900)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Installed thunder.thunder-lang@", result.stdout)
            self.assertTrue((Path(directory) / "extensions.json").is_file())
        for leftover in EXTENSION.glob("*.vsix"):
            leftover.unlink()


if __name__ == "__main__":
    unittest.main()
