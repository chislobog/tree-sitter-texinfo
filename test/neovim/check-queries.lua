-- Loads each of editors/neovim/queries/ in Neovim and runs it over the given
-- files, so that what only Neovim rejects fails: its regexes are Vim's,
-- which `ts_query_ls` doesn't check. Neovim compiles a pattern's regex only
-- when its nodes match, so a pattern with a regex that no file matches
-- fails too: the files must reach it.
--
-- Usage: nvim --headless -l test/neovim/check-queries.lua RUNTIME FILE...
-- where RUNTIME has parser/texinfo.so and queries/texinfo/.
local runtime = arg[1]
vim.opt.runtimepath:prepend(runtime)

local names = { "highlights", "injections", "folds", "textobjects" }
local queries = {}
local matched = {}
for _, name in ipairs(names) do
  queries[name] = vim.treesitter.query.get("texinfo", name)
  assert(queries[name], "no " .. name .. " query")
  matched[name] = {}
end

for i = 2, #arg do
  local source = table.concat(vim.fn.readfile(arg[i]), "\n") .. "\n"
  local tree = vim.treesitter.get_string_parser(source, "texinfo"):parse()[1]
  for _, name in ipairs(names) do
    for pattern in queries[name]:iter_matches(tree:root(), source) do
      matched[name][pattern] = true
    end
  end
end

-- The predicates whose regexes Neovim compiles.
local regex = { ["match?"] = true, ["not-match?"] = true, ["vim-match?"] = true, ["lua-match?"] = true }
local unmatched = 0
for _, name in ipairs(names) do
  for pattern, predicates in pairs(queries[name].info.patterns) do
    for _, predicate in ipairs(predicates) do
      if regex[predicate[1]] and not matched[name][pattern] then
        unmatched = unmatched + 1
        print(string.format("%s: pattern %d, with a regex, matches no file", name, pattern))
        break
      end
    end
  end
end
assert(unmatched == 0, unmatched .. " patterns with a regex unchecked")
print(string.format("%d queries on %d files", #names, #arg - 1))
