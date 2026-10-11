; Generated from queries/highlights.scm by script/editor-queries.py.
; When patterns overlap, the one that finishes matching first takes
; precedence, so avoid overlapping a specific pattern with a generic one.
; Capture nodes used in predicates with a real highlight, not a private
; `@_name`: the highlighter drops the rest of a match after a capture that
; isn't a highlight.
(comment) @comment

(input_line) @keyword.directive

; Headings
(part_heading
  name: (command_name) @keyword
  title: (line_argument) @markup.heading.1)

(top_heading
  name: (command_name) @keyword
  title: (line_argument) @markup.heading.1)

(chapter_heading
  name: (command_name) @keyword
  title: (line_argument) @markup.heading.1)

(section_heading
  name: (command_name) @keyword
  title: (line_argument) @markup.heading.2)

(subsection_heading
  name: (command_name) @keyword
  title: (line_argument) @markup.heading.3)

(subsubsection_heading
  name: (command_name) @keyword
  title: (line_argument) @markup.heading.4)

((heading
  name: (command_name) @keyword
  title: (line_argument) @markup.heading.1)
  (#any-of? @keyword "@majorheading" "@chapheading"))

((heading
  name: (command_name) @keyword
  title: (line_argument) @markup.heading.2)
  (#eq? @keyword "@heading"))

((heading
  name: (command_name) @keyword
  title: (line_argument) @markup.heading.3)
  (#eq? @keyword "@subheading"))

((heading
  name: (command_name) @keyword
  title: (line_argument) @markup.heading.4)
  (#eq? @keyword "@subsubheading"))

; Nodes, cross-references and menus
(node_line
  name: (command_name) @keyword
  node: (argument) @markup.link.label)

(node_line
  [
    next: (argument)
    previous: (argument)
    up: (argument)
  ] @markup.link)

(inline_command
  node: (argument) @markup.link.label)

(inline_command
  [
    url: (argument)
    address: (argument)
  ] @markup.link.url)

(menu_entry
  node: (menu_node) @markup.link.label)

; A float's label is a cross-reference target, like an anchor.
(block
  label: (argument) @markup.link.label)

(menu_entry
  label: (menu_label) @markup.link)

; Definitions
; Two exclusive patterns, as each node can only be highlighted by one match.
((definition_line
  name: (command_name) @keyword
  entity: (_)? @variable)
  (#match? @keyword "^@def(type)?(vr|var|opt|cv|ivar)x?$"))

((definition_line
  name: (command_name) @keyword
  entity: (_)? @function)
  (#not-match? @keyword "^@def(type)?(vr|var|opt|cv|ivar)x?$"))

(definition_line
  [
    type: (_)
    class: (_)
  ] @type)

(definition_line
  category: (_) @attribute)

(definition_arguments) @variable.parameter

(line_continuation) @punctuation.special

; Blocks
((block
  name: (command_name) @keyword.conditional)
  (#match? @keyword.conditional "^@if"))

((block_end) @keyword.conditional
  (#match? @keyword.conditional "^@end[ \t]+if"))

((block
  name: (command_name) @keyword
  (paragraph) @markup.raw.block)
  (#any-of? @keyword
    "@example" "@smallexample" "@lisp" "@smalllisp" "@display" "@smalldisplay" "@format"
    "@smallformat"))

((block
  name: (command_name) @keyword
  (paragraph) @markup.quote)
  (#any-of? @keyword "@quotation" "@smallquotation"))

(raw_block
  body: (raw_text) @markup.raw.block)

; Not the blocks matched above: the highlighter drops a match whose node
; another match has already highlighted, with all its other captures.
((block
  name: (command_name) @keyword)
  (#not-match? @keyword
    "^@(if.*|example|smallexample|lisp|smalllisp|display|smalldisplay|format|smallformat|quotation|smallquotation)$"))

(list
  name: (command_name) @keyword)

(menu
  name: (command_name) @keyword)

(raw_block
  name: (command_name) @keyword)

(macro_definition
  name: (command_name) @keyword
  macro: (text) @function)

(macro_definition
  parameter: (text) @variable.parameter)

((block_end) @keyword
  (#not-match? @keyword "^@end[ \t]+if"))

(unmatched_end) @keyword

; Lists
(item
  name: (command_name) @markup.list)

(itemx
  name: (command_name) @markup.list)

(tab
  name: (command_name) @markup.list)

; Line commands. The generic pattern excludes the commands with their own
; pattern, which it would otherwise beat to the name.
((line_command
  name: (command_name) @keyword)
  (#not-any-of? @keyword "@include" "@verbatiminclude" "@setfilename"))

((line_command
  name: (command_name) @keyword
  argument: (line_argument) @string.special.path)
  (#any-of? @keyword "@include" "@verbatiminclude" "@setfilename"))

(subentry
  name: (command_name) @keyword)

(line_command
  variable: (text) @variable)

(line_command
  value: (raw_text) @string)

(bye
  name: (command_name) @keyword)

(bye
  (ignored) @comment)

; Inline markup
((inline_command
  name: (command_name) @function
  arguments: (brace_arguments) @markup.raw)
  (#any-of? @function
    "@code" "@samp" "@kbd" "@key" "@file" "@command" "@option" "@env" "@t" "@indicateurl"))

(inline_command
  body: (raw_text) @markup.raw)

((inline_command
  name: (command_name) @function
  arguments: (brace_arguments) @markup.italic)
  (#any-of? @function "@emph" "@i" "@dfn" "@cite" "@slanted"))

((inline_command
  name: (command_name) @function
  arguments: (brace_arguments) @markup.strong)
  (#any-of? @function "@strong" "@b"))

((inline_command
  name: (command_name) @function
  arguments: (brace_arguments) @variable.parameter)
  (#eq? @function "@var"))

; Commands without braces that start a paragraph.
((inline_command
  name: (command_name) @keyword)
  (#any-of? @keyword "@noindent" "@indent"))

; `@value{NAME}` refers to a variable set by `@set NAME`.
((inline_command
  name: (command_name) @function
  arguments: (brace_arguments) @variable)
  (#eq? @function "@value"))

; Accents, like the symbol commands they mostly are: `@'e`, `@u{o}`
((inline_command
  name: (command_name) @string.escape)
  (#any-of? @string.escape
    "@'" "@\"" "@^" "@`" "@~" "@=" "@," "@H" "@dotaccent" "@dotless" "@ogonek" "@ringaccent"
    "@tieaccent" "@u" "@ubaraccent" "@udotaccent" "@v"))

; Everything else. Excludes the commands above: this pattern finishes
; matching first, so it would claim their names and drop their matches.
((inline_command
  name: (command_name) @function)
  (#not-any-of? @function
    "@code" "@samp" "@kbd" "@key" "@file" "@command" "@option" "@env" "@t" "@indicateurl" "@emph"
    "@i" "@dfn" "@cite" "@slanted" "@strong" "@b" "@var" "@value" "@noindent" "@indent" "@'" "@\""
    "@^" "@`" "@~" "@=" "@," "@H" "@dotaccent" "@dotless" "@ogonek" "@ringaccent" "@tieaccent" "@u"
    "@ubaraccent" "@udotaccent" "@v"))

; Commands that error recovery left without their usual parent. A generic
; (command_name) pattern would win over the specific ones above, since a
; pattern's captures apply as soon as it finishes matching.
(ERROR
  (command_name) @function)

(symbol_command) @string.escape

[
  "{"
  "}"
] @punctuation.bracket

[
  ","
  ":"
  "::"
  "."
] @punctuation.delimiter
