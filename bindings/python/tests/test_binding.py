from unittest import TestCase

from tree_sitter import Language, Parser
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
