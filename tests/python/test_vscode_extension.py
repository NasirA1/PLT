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
        self.assertEqual(self.manifest["version"], version.group(1),
                         "the extension's version should follow the toolchain's")

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
                 if path.is_file() and "__pycache__" not in path.parts and "node_modules" not in path.parts]
        self.assertGreater(len(files), 40)
        result = subprocess.run(["git", "check-ignore", "--stdin"], cwd=support.ROOT,
                                input="\n".join(str(path) for path in files),
                                capture_output=True, text=True)
        ignored = [line for line in result.stdout.splitlines() if line.strip()]
        self.assertEqual(ignored, [], "these files would never reach the repository")


class InstallScriptTests(unittest.TestCase):
    """Installing the extension must work without npm, a package or a network."""

    @unittest.skipIf(os.name == "nt", "install.sh is the POSIX installer")
    def test_install_sh_copies_and_removes_the_extension(self):
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

    @unittest.skipIf(os.name != "nt" and shutil.which("pwsh") is None, "PowerShell is not available")
    def test_install_ps1_copies_and_removes_the_extension(self):
        script = EXTENSION / "scripts" / "install.ps1"
        shell = "powershell" if os.name == "nt" else "pwsh"
        with tempfile.TemporaryDirectory() as directory:
            installed = subprocess.run(
                [shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script), "-Target", directory],
                capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(installed.returncode, 0, installed.stdout + installed.stderr)
            destination = Path(directory) / "thunder.thunder-lang"
            self.assertTrue((destination / "package.json").is_file())
            removed = subprocess.run(
                [shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script),
                 "-Target", directory, "-Uninstall"],
                capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(removed.returncode, 0, removed.stdout + removed.stderr)
            self.assertFalse(destination.exists())


if __name__ == "__main__":
    unittest.main()
