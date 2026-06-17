-- Lightweight "go to definition" for Grav (no LSP required).
--
-- Classifies the identifier under the cursor as either a *member* (preceded by
-- `.` or `->`, e.g. the `name` in `self.name`, `ptr->name`, `a.b.name`) or a
-- *plain name* (variable / parameter / function / type), then jumps to the most
-- likely declaration — current buffer first (nearest match), then project-wide.
--
-- Bound by ftplugin/grav.lua to `gd`, `<C-]>`, and Ctrl-click.

local M = {}

-- Names that are never user definitions worth jumping to.
local SKIP = {
  ["self"] = true, ["this"] = true, ["true"] = true, ["false"] = true,
  ["null"] = true, ["int"] = true, ["float"] = true, ["bool"] = true,
  ["string"] = true, ["void"] = true, ["return"] = true, ["if"] = true,
  ["else"] = true, ["while"] = true, ["for"] = true, ["do"] = true,
  ["switch"] = true, ["case"] = true, ["default"] = true, ["match"] = true,
  ["break"] = true, ["continue"] = true, ["new"] = true, ["as"] = true,
  ["is"] = true, ["in"] = true, ["await"] = true, ["print"] = true,
  ["try"] = true, ["catch"] = true, ["finally"] = true, ["throw"] = true,
  ["sizeof"] = true, ["str"] = true, ["input"] = true, ["argc"] = true,
  ["argv"] = true, ["typename"] = true, ["isInstance"] = true,
}

-- Returns the identifier under the cursor and whether it is a member access.
local function ident_under_cursor()
  local line = vim.api.nvim_get_current_line()
  local col0 = vim.api.nvim_win_get_cursor(0)[2] -- 0-based byte column
  local n = #line
  if n == 0 then return nil end
  local pos = math.min(col0 + 1, n)
  if not line:sub(pos, pos):match("[%w_]") then
    while pos > 1 and not line:sub(pos, pos):match("[%w_]") do pos = pos - 1 end
  end
  if not line:sub(pos, pos):match("[%w_]") then return nil end
  local s, e = pos, pos
  while s > 1 and line:sub(s - 1, s - 1):match("[%w_]") do s = s - 1 end
  while e < n and line:sub(e + 1, e + 1):match("[%w_]") do e = e + 1 end
  local word = line:sub(s, e)
  local prefix = line:sub(1, s - 1)
  local is_member = prefix:match("%->%s*$") ~= nil or prefix:match("%.%s*$") ~= nil
  return word, is_member
end

-- Ordered Vim-regex patterns for `word`'s declaration, highest priority first.
local function patterns(word, is_member)
  local w = vim.fn.escape(word, "\\/.*$^~[]")
  if is_member then
    -- A member resolves to a class/struct field or a method.
    return {
      "\\<fn\\s\\+" .. w .. "\\>",   -- method:  fn name(...)
      "^\\s*\\(public\\|private\\|protected\\|readonly\\|static\\)\\?\\s*" .. w .. "\\s*:", -- field
      "\\<" .. w .. "\\s*:",          -- field (looser)
    }
  end
  return {
    "\\<let\\s\\+" .. w .. "\\>",                                        -- local
    "\\<const\\s\\+" .. w .. "\\>",                                      -- const
    "\\<fn\\s\\+" .. w .. "\\>",                                         -- function/method
    "\\<\\(class\\|struct\\|enum\\|interface\\|type\\|namespace\\)\\s\\+" .. w .. "\\>", -- type
    "\\<" .. w .. "\\s*:",                                               -- parameter / field
  }
end

local function search_buffer(pat)
  local l = vim.fn.search(pat, "bcnW") -- backward, accept match at cursor, no move
  if l == 0 then l = vim.fn.search(pat, "nW") end -- else forward
  return l
end

local function project_root()
  if vim.fs and vim.fs.root then
    local r = vim.fs.root(0, { ".git", "CMakeLists.txt", "README.md" })
    if r then return r end
  end
  return vim.fn.expand("%:p:h")
end

function M.goto_def()
  local word, is_member = ident_under_cursor()
  if not word or word == "" or SKIP[word] then return end
  local pats = patterns(word, is_member)

  -- Phase 1: the current buffer (handles locals, params, and same-file types).
  for _, p in ipairs(pats) do
    local l = search_buffer(p)
    if l ~= 0 then
      vim.cmd("normal! m'") -- record a jump so <C-o> returns
      vim.api.nvim_win_set_cursor(0, { l, 0 })
      vim.cmd("normal! ^")
      return
    end
  end

  -- Phase 2: project-wide, pattern by pattern (priority preserved).
  local glob = vim.fn.fnameescape(project_root()) .. "/**/*.grav"
  for _, p in ipairs(pats) do
    pcall(function()
      vim.cmd(string.format("silent! vimgrep /%s/gj %s", p, glob))
    end)
    local qf = vim.fn.getqflist()
    if #qf == 1 then
      vim.cmd("cfirst")
      return
    elseif #qf > 1 then
      vim.cmd("copen")
      return
    end
  end

  vim.notify("grav: no definition found for '" .. word .. "'", vim.log.levels.INFO)
end

-- `gr` — list every occurrence of the symbol under the cursor (project-wide).
function M.references()
  local word = ident_under_cursor()
  if not word or word == "" then return end
  local w = vim.fn.escape(word, "\\/.*$^~[]")
  local glob = vim.fn.fnameescape(project_root()) .. "/**/*.grav"
  pcall(function()
    vim.cmd(string.format("silent! vimgrep /\\<%s\\>/gj %s", w, glob))
  end)
  if #vim.fn.getqflist() == 0 then
    vim.notify("grav: no references to '" .. word .. "'", vim.log.levels.INFO)
  else
    vim.cmd("copen")
  end
end

-- `gO` — an outline of the current file (every top-level / member declaration).
function M.symbols()
  local pat = "\\<\\(fn\\|class\\|struct\\|enum\\|interface\\|type\\|namespace\\|constructor\\)\\>"
  vim.fn.setloclist(0, {}, " ", { title = "Grav symbols" })
  pcall(function() vim.cmd(string.format("silent! lvimgrep /%s/gj %%", pat)) end)
  if #vim.fn.getloclist(0) == 0 then
    vim.notify("grav: no symbols found", vim.log.levels.INFO)
  else
    vim.cmd("lopen")
  end
end

-- `K` — peek the definition line of the symbol under the cursor in a float.
function M.peek()
  local word, is_member = ident_under_cursor()
  if not word or word == "" or SKIP[word] then return end
  for _, p in ipairs(patterns(word, is_member)) do
    local l = search_buffer(p)
    if l ~= 0 then
      local text = vim.fn.trim(vim.fn.getline(l))
      vim.lsp.util.open_floating_preview({ "L" .. l .. ": " .. text }, "grav",
        { border = "rounded", focusable = false })
      return
    end
  end
  vim.notify("grav: no definition for '" .. word .. "'", vim.log.levels.INFO)
end

return M
