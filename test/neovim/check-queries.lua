-- Loads each of editors/neovim/queries/ in Neovim and runs it over the given
-- files, so that what only Neovim rejects fails: its regexes are Vim's,
-- which `ts_query_ls` doesn't check. Neovim evaluates a pattern's
-- predicates only when its nodes match, so the files should match every
-- pattern; the highlight and tags tests do.
--
-- Usage: nvim --headless -l test/neovim/check-queries.lua RUNTIME FILE...
-- where RUNTIME has parser/texinfo.so and queries/texinfo/.
local runtime = arg[1]
vim.opt.runtimepath:prepend(runtime)

local names = { "highlights", "injections", "folds", "textobjects" }
for i = 2, #arg do
  local source = table.concat(vim.fn.readfile(arg[i]), "\n") .. "\n"
  local tree = vim.treesitter.get_string_parser(source, "texinfo"):parse()[1]
  for _, name in ipairs(names) do
    local query = vim.treesitter.query.get("texinfo", name)
    assert(query, "no " .. name .. " query")
    for _ in query:iter_matches(tree:root(), source) do
    end
  end
end
print(string.format("%d queries on %d files", #names, #arg - 1))
