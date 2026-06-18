-- Grav LSP client glue for Neovim's built-in LSP.
--
-- Starts the dependency-free Grav language server (editors/lsp/server.js) when a
-- .grav buffer opens, giving go-to-definition, references, rename, document
-- symbols, hover, and compiler diagnostics. Falls back silently to the
-- regex-based `grav.goto` heuristics when node or the server can't be found.
--
-- Override the server path with g:grav_lsp_server or $GRAV_LSP_SERVER.

local M = {}

-- Locate editors/lsp/server.js: explicit override, env var, or by walking up
-- from the current file (works whenever you edit inside the Grav repo).
local function find_server(bufnr)
  if vim.g.grav_lsp_server and vim.fn.filereadable(vim.g.grav_lsp_server) == 1 then
    return vim.g.grav_lsp_server
  end
  local env = vim.env.GRAV_LSP_SERVER
  if env and env ~= "" and vim.fn.filereadable(env) == 1 then
    return env
  end
  local dir = vim.fs.dirname(vim.api.nvim_buf_get_name(bufnr))
  while dir and dir ~= "/" do
    local cand = dir .. "/editors/lsp/server.js"
    if vim.fn.filereadable(cand) == 1 then
      return cand
    end
    local parent = vim.fs.dirname(dir)
    if parent == dir then break end
    dir = parent
  end
  return nil
end

local function on_attach(_, bufnr)
  local function map(lhs, fn, desc)
    vim.keymap.set("n", lhs, fn, { buffer = bufnr, silent = true, desc = desc })
  end
  -- These override the grav.goto fallbacks with real LSP requests.
  map("gd", vim.lsp.buf.definition, "Grav: go to definition")
  map("<C-]>", vim.lsp.buf.definition, "Grav: go to definition")
  map("gr", vim.lsp.buf.references, "Grav: references")
  map("gO", vim.lsp.buf.document_symbol, "Grav: document symbols")
  map("grn", vim.lsp.buf.rename, "Grav: rename")
  map("K", vim.lsp.buf.hover, "Grav: hover")
end

function M.start(bufnr)
  bufnr = bufnr or vim.api.nvim_get_current_buf()
  if vim.fn.executable("node") == 0 then return false end
  local server = find_server(bufnr)
  if not server then return false end

  local root = nil
  if vim.fs.root then
    root = vim.fs.root(bufnr, { ".git", "CMakeLists.txt", "lib" })
  end
  root = root or vim.fn.getcwd()

  local ok = pcall(vim.lsp.start, {
    name = "grav",
    cmd = { "node", server },
    root_dir = root,
    on_attach = on_attach,
    init_options = {
      compilerPath = vim.fn.expand((vim.env.GRAV_DIR or "~/.local/bin") .. "/grav"),
    },
  }, { bufnr = bufnr })
  return ok
end

return M
