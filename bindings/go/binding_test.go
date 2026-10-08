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
