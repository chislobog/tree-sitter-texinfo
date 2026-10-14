import assert from "node:assert";
import { test } from "node:test";
import Parser from "tree-sitter";

test("can load grammar", async () => {
  const parser = new Parser();
  const { default: language } = await import("./index.js");
  parser.setLanguage(language);
});

test("parses a document", async () => {
  const parser = new Parser();
  const { default: language } = await import("./index.js");
  parser.setLanguage(language);
  const tree = parser.parse("@node Top\n@top Title\n\nSome @code{x}.\n");
  assert.ok(!tree.rootNode.hasError, tree.rootNode.toString());
  assert.equal(tree.rootNode.firstChild.type, "top");
});
