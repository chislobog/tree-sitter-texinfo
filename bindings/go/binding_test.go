package tree_sitter_texinfo_test

import (
	"testing"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"
	tree_sitter_texinfo "github.com/chislobog/tree-sitter-texinfo/bindings/go"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_texinfo.Language())
	if language == nil {
		t.Errorf("Error loading GNU Texinfo grammar")
	}
}

func TestParsesADocument(t *testing.T) {
	parser := tree_sitter.NewParser()
	defer parser.Close()
	language := tree_sitter.NewLanguage(tree_sitter_texinfo.Language())
	if err := parser.SetLanguage(language); err != nil {
		t.Fatalf("Error loading GNU Texinfo grammar: %v", err)
	}
	tree := parser.Parse([]byte("@node Top\n@top Title\n\nSome @code{x}.\n"), nil)
	defer tree.Close()
	root := tree.RootNode()
	if root.HasError() {
		t.Errorf("Parse error: %s", root.ToSexp())
	}
	if kind := root.Child(0).Kind(); kind != "top" {
		t.Errorf("First node %q, not top", kind)
	}
}
