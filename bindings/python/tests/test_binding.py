from unittest import TestCase

from tree_sitter import Language, Parser, Query
import tree_sitter_texinfo


class TestLanguage(TestCase):
    def test_can_load_grammar(self):
        try:
            Parser(Language(tree_sitter_texinfo.language()))
        except Exception:
            self.fail("Error loading GNU Texinfo grammar")

    def test_parses_a_document(self):
        parser = Parser(Language(tree_sitter_texinfo.language()))
        tree = parser.parse(b"@node Top\n@top Title\n\nSome @code{x}.\n")
        self.assertFalse(tree.root_node.has_error, str(tree.root_node))
        self.assertEqual(tree.root_node.children[0].type, "top")

    def test_has_its_queries(self):
        # In a wheel, and in an editable install, which has no copy of them.
        language = Language(tree_sitter_texinfo.language())
        for query in ("HIGHLIGHTS_QUERY", "INJECTIONS_QUERY", "TAGS_QUERY"):
            text = getattr(tree_sitter_texinfo, query)
            self.assertTrue(text, f"no {query}")
            Query(language, text)
