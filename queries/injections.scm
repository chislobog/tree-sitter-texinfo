; Raw blocks for other languages. Texinfo commands inside them are not
; parsed (see TODO.md).
((raw_block
  name: (command_name) @_name
  body: (raw_text) @injection.content)
  (#any-of? @_name "@tex" "@latex")
  (#set! injection.language "latex"))

((raw_block
  name: (command_name) @_name
  body: (raw_text) @injection.content)
  (#eq? @_name "@html")
  (#set! injection.language "html"))

((raw_block
  name: (command_name) @_name
  body: (raw_text) @injection.content)
  (#any-of? @_name "@xml" "@docbook")
  (#set! injection.language "xml"))

; Macro bodies are Texinfo; `\param\` placeholders stay text.
(macro_definition
  body: (raw_text) @injection.content
  (#set! injection.language "texinfo"))

; `@inlineraw{FORMAT, TEXT}`: the format names the language, `tex` aside.
((inline_command
  name: (command_name) @_name
  format: (argument) @_format
  body: (raw_text) @injection.content)
  (#eq? @_name "@inlineraw")
  (#any-of? @_format "tex" "latex")
  (#set! injection.language "latex"))

((inline_command
  name: (command_name) @_name
  format: (argument) @injection.language
  body: (raw_text) @injection.content)
  (#eq? @_name "@inlineraw")
  (#not-any-of? @injection.language "tex" "latex"))

; Math. The contents are made of text and command nodes, so include them.
((inline_command
  name: (command_name) @_name
  arguments: (brace_arguments) @injection.content)
  (#eq? @_name "@math")
  (#set! injection.language "latex")
  (#set! injection.include-children))

((block
  name: (command_name) @_name
  (paragraph) @injection.content)
  (#eq? @_name "@displaymath")
  (#set! injection.language "latex")
  (#set! injection.include-children))

; Comments
((comment) @injection.content
  (#set! injection.language "comment"))
