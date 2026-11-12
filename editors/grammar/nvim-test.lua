-- Checks that Neovim's syntax file (editors/nvim/syntax/grav.vim) agrees with
-- the TextMate grammar on the fixtures in editors/grammar/tests/: each asserted
-- scope must come out in Neovim as a highlight group that stands for it (the
-- group a grav* syntax group links to, e.g. gravTypeRef -> Type).
--
--   nvim --headless --clean -u NONE -l editors/grammar/nvim-test.lua
--
-- Prints the disagreements and "nvim syntax: N of M assertions agree"; exits 1
-- when one disagrees.
local here = debug.getinfo(1, "S").source:sub(2):match("(.*/)")
local root = here .. "../nvim/"
vim.opt.rtp:prepend(root)
vim.cmd("syntax on")

-- grav* group -> the group it links to, from the syntax file's `hi def link` lines
local links = {}
for line in io.lines(root .. "syntax/grav.vim") do
  local from, to = line:match("^hi def link%s+(%S+)%s+(%S+)")
  if from then links[from] = to end
end

-- A scope (most specific first) and the highlight groups that may show it; nil: anything.
local wants = {
  { "comment", { "Comment" } },
  { "keyword.control.import", { "Keyword", "Include" } },
  { "keyword.control.async", { "Keyword" } },
  { "keyword.control", { "Conditional", "Repeat", "Statement", "Exception", "Keyword", "Label" } },
  { "keyword.other.test", { "Keyword" } },
  { "keyword.other.operator", { "Keyword" } },
  { "keyword.operator.type", { "Operator" } },
  { "keyword.operator.new", { "Keyword", "Operator" } },
  { "keyword.operator", { "Operator" } },
  { "storage.type.primitive", { "Type" } },
  { "storage.type.function", { "Keyword" } },
  { "storage.type.variable", { "StorageClass" } },
  { "storage.type", { "Keyword", "Type", "StorageClass" } },
  { "storage.modifier", { "StorageClass", "Keyword" } },
  { "entity.name.function.decorator", { "PreProc" } },
  { "punctuation.definition.decorator", { "PreProc" } },
  { "entity.name.function", { "Function" } },
  { "support.function.builtin", { "Function", "Type" } },   -- long(x): vim keeps the type's color
  { "entity.name.type", { "Type", "Structure" } },
  { "entity.other.inherited-class", { "Type" } },
  { "entity.name.label", { "Label" } },
  { "entity.name.namespace", { "Identifier" } },
  { "variable.other.enummember", { "Constant" } },
  { "variable.parameter.named", { "Identifier" } },
  { "variable.language", { "Constant", "Keyword" } },
  { "constant.language", { "Constant", "Boolean" } },
  { "constant.numeric", { "Number", "Float", "Constant" } },   -- (C syntax: cNumber -> Constant)
  { "constant.character.escape", { "SpecialChar" } },
  { "constant.character", { "Character" } },
  { "constant.other.format-spec", { "SpecialChar" } },
  { "punctuation.separator.format", { "SpecialChar" } },
  { "punctuation.section.interpolation", { "SpecialChar" } },
  { "punctuation.section.embedded", { "Special" } },
  { "string", { "String", "Include", "Title" } },
  { "variable", nil }, { "punctuation", nil }, { "meta", nil }, { "source", nil },
}
local function allowed(scope)
  for _, w in ipairs(wants) do
    if scope == w[1] or scope:sub(1, #w[1] + 1) == w[1] .. "." then return w[2], true end
  end
  return nil, false
end

local function groupAt(lnum, col) -- the link target of the innermost grav* group at a position
  local stack = vim.fn.synstack(lnum, col + 1)
  for i = #stack, 1, -1 do
    local name = vim.fn.synIDattr(stack[i], "name")
    if links[name] then return links[name], name end
    if name ~= "" and not name:match("^grav") then                       -- C syntax inside %{ %}
      return vim.fn.synIDattr(vim.fn.synIDtrans(stack[i]), "name"), name
    end
  end
  return "", ""
end

local total, bad = 0, {}
for _, file in ipairs(vim.fn.glob(here .. "tests/*.grav", false, true)) do
  vim.cmd("edit " .. vim.fn.fnameescape(file))
  vim.bo.filetype = "grav"
  vim.cmd("syntax sync fromstart")
  local lines = vim.api.nvim_buf_get_lines(0, 0, -1, false)
  local src = nil
  for i, line in ipairs(lines) do
    local carets, rest = line:match("^%s*//%s*(%^+)%s+(.+)$")
    local back, brest = line:match("^(%s*)//%s*<%-%s+(.+)$")
    if carets or back then
      local from = carets and (line:find("%^") - 1) or #back
      local to = carets and (from + #carets) or (from + 1)
      local scope = (carets and rest or brest):match("^(%S+)")
      local groups, known = allowed(scope)
      total = total + 1
      if groups then
        for col = from, to - 1 do
          local got, raw = groupAt(src, col)
          if not vim.tbl_contains(groups, got) then
            table.insert(bad, string.format("%s:%d:%d: %s should be %s, is %s", vim.fn.fnamemodify(file, ":t"), src, col + 1,
              scope, table.concat(groups, "|"), raw ~= "" and (raw .. " -> " .. got) or "(nothing)"))
            break
          end
        end
      elseif not known then
        table.insert(bad, string.format("%s:%d: no Neovim group is known for %s (add it to nvim-test.lua)", vim.fn.fnamemodify(file, ":t"), src, scope))
      end
    elseif not line:match("^%s*//%s*$") then
      src = i
    end
  end
end
for _, b in ipairs(bad) do io.stdout:write("FAIL " .. b .. "\n") end
io.stdout:write(string.format("nvim syntax: %d of %d assertions agree\n", total - #bad, total))
os.exit(#bad > 0 and 1 or 0)
