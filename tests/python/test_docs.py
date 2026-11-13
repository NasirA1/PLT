"""The SDK reference catalog and where its links lead (the Help viewer follows these)."""

import tempfile
import unittest
from pathlib import Path

from tests.python import support
from thunder_ide.core.docs import load_document, load_reference, resolve_link


class LinkTests(unittest.TestCase):
    """Links are resolved by path, not by file name.

    This was a real bug: the SDK index (docs/README.md) links to the VS Code extension's
    README (ide/vscode/README.md). The viewer compared file names, found "README.md" in its
    catalog - the index itself - and reopened the page the reader was already on.
    """

    @classmethod
    def setUpClass(cls):
        cls.documents = load_reference("en")
        cls.index = cls.documents[0]

    def test_the_index_is_the_first_document(self):
        self.assertEqual(self.index.name, "README.md")
        self.assertTrue(self.index.directory.endswith("docs"))

    def test_a_link_to_another_reference_document_switches_to_it(self):
        link = resolve_link("library.md", self.index, self.documents)
        self.assertEqual(link.kind, "document")
        self.assertEqual(self.documents[link.index].name, "library.md")

    def test_a_link_with_a_heading_keeps_the_heading(self):
        link = resolve_link("language.md#lexical-structure", self.index, self.documents)
        self.assertEqual(link.kind, "document")
        self.assertEqual(link.fragment, "lexical-structure")
        self.assertEqual(resolve_link("#modules", self.index, self.documents).kind, "anchor")

    def test_a_readme_elsewhere_is_not_the_index(self):
        link = resolve_link("../ide/vscode/README.md", self.index, self.documents)
        self.assertEqual(link.kind, "markdown", "it must open the extension's README, not the index")
        self.assertEqual(Path(link.path), support.ROOT / "ide" / "vscode" / "README.md")
        document = load_document(link.path)
        self.assertIn("VS Code", document.title)
        self.assertEqual(Path(document.directory), support.ROOT / "ide" / "vscode")

    def test_the_devlog_opens_too(self):
        link = resolve_link("../devlog.md", self.index, self.documents)
        self.assertEqual(link.kind, "markdown")
        self.assertEqual(Path(link.path), support.ROOT / "devlog.md")

    def test_every_local_link_on_the_index_page_leads_somewhere(self):
        """The links at the bottom of the index were the ones that went nowhere."""
        import re

        for href in re.findall(r"\]\(([^)\s]+)\)", self.index.text):
            with self.subTest(link=href):
                link = resolve_link(href, self.index, self.documents)
                if link.kind == "external" and "://" not in link.target:
                    self.assertTrue(Path(link.target).exists(), f"{href} points at nothing")
                if link.kind in ("document", "markdown"):
                    self.assertTrue(Path(link.path).is_file() or link.kind == "document")

    def test_every_local_link_in_every_document_leads_somewhere(self):
        """The whole reference, in both languages: no link may lead nowhere."""
        import re

        for language in ("en", "ps"):
            documents = load_reference(language)
            for document in documents:
                # Code is not markdown: steps[1](3) in a code block is a call, not a link.
                prose = re.sub(r"```.*?```", "", document.text, flags=re.S)
                prose = re.sub(r"`[^`\n]*`", "", prose)
                for href in re.findall(r"\]\(([^)\s]+)\)", prose):
                    if href.startswith(("http://", "https://", "mailto:")):
                        continue
                    with self.subTest(language=language, document=document.name, link=href):
                        link = resolve_link(href, document, documents)
                        if link.kind == "anchor":
                            continue
                        target = Path(link.path or link.target)
                        self.assertTrue(target.exists() or link.kind == "document", f"{href} points at nothing")

    def test_web_links_and_other_files_go_to_the_system(self):
        self.assertEqual(resolve_link("https://example.org", self.index, self.documents).kind, "external")
        image = resolve_link("images/ide_input.png", self.index, self.documents)
        self.assertEqual(image.kind, "external")
        self.assertTrue(image.target.endswith("ide_input.png"))

    def test_a_translation_falls_back_to_the_english_original(self):
        """docs/ps/README.md may link to a document that has no Pashto translation yet."""
        with tempfile.TemporaryDirectory() as directory:
            pashto_index = load_document(Path(directory) / "missing.md")
            pashto_index = pashto_index.__class__("README.md", "index", "", directory)
            link = resolve_link("library.md", pashto_index, self.documents)
            self.assertEqual(link.kind, "document")
            self.assertEqual(self.documents[link.index].name, "library.md")


if __name__ == "__main__":
    unittest.main()
