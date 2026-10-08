/**
 * @file Texinfo grammar for tree-sitter
 * @author Janko Lisica <janko.lisica@gmail.com>
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

export default grammar({
  name: "texinfo",

  rules: {
    // TODO: add the actual grammar rules
    source_file: $ => "hello"
  }
});
