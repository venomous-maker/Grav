-- Buffer-local "go to definition" keymaps for Grav.
-- Works on plain names (variables, params, functions, types) and on member
-- accesses in chains like `self.name`, `ptr->name`, `ptr->object.name`, `a.b.c`.

local goto_cmd = "<Cmd>lua require('grav.goto').goto_def()<CR>"
local opts = { buffer = true, silent = true, desc = "Grav: go to definition" }

-- gd / Ctrl-] : jump to definition of the symbol under the cursor.
vim.keymap.set("n", "gd", goto_cmd, opts)
vim.keymap.set("n", "<C-]>", goto_cmd, opts)

-- Ctrl-Click : the <LeftMouse> moves the cursor to the click, then we jump.
vim.keymap.set("n", "<C-LeftMouse>", "<LeftMouse>" .. goto_cmd,
  { buffer = true, silent = true, desc = "Grav: go to definition (click)" })
-- <C-o> already returns to where you jumped from (built-in jumplist).

-- gr : references to the symbol (project-wide quickfix).
vim.keymap.set("n", "gr", "<Cmd>lua require('grav.goto').references()<CR>",
  vim.tbl_extend("force", opts, { desc = "Grav: references" }))
-- gO : document outline (declarations in this file).
vim.keymap.set("n", "gO", "<Cmd>lua require('grav.goto').symbols()<CR>",
  vim.tbl_extend("force", opts, { desc = "Grav: document symbols" }))
-- K : peek the definition line in a float.
vim.keymap.set("n", "K", "<Cmd>lua require('grav.goto').peek()<CR>",
  vim.tbl_extend("force", opts, { desc = "Grav: peek definition" }))

-- ]] / [[ : jump to the next / previous top-level declaration.
local section = "\\v^\\s*(export\\s+)?(async\\s+)?(abstract\\s+)?(fn|class|struct|enum|interface|type|namespace)>"
vim.keymap.set("n", "]]", function() vim.fn.search(section, "W") end,
  vim.tbl_extend("force", opts, { desc = "Grav: next declaration" }))
vim.keymap.set("n", "[[", function() vim.fn.search(section, "bW") end,
  vim.tbl_extend("force", opts, { desc = "Grav: previous declaration" }))
