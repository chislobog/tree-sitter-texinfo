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

; Comments

((comment) @injection.content
  (#set! injection.language "comment"))
