-- Grav LSP client glue for Neovim's built-in LSP.
--
-- Starts the dependency-free Grav language server when a .grav buffer opens,
-- giving go-to-definition, references, rename, document symbols, hover,
-- completion, inlay hints and compiler diagnostics. The server ships with this
-- plugin (lua/grav/server/, copied there by editors/sync.sh), so it works in any
-- project; inside the Grav repository the checkout's editors/lsp/server.js is
-- used instead. Falls back silently to the regex-based `grav.goto` heuristics
-- when node can't be found.
--
-- Override the server path with g:grav_lsp_server or $GRAV_LSP_SERVER.

local M = {}

-- Locate the server: explicit override, env var, the Grav repo's
-- editors/lsp/server.js above the current file, else the copy bundled here.
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
  local here = debug.getinfo(1, "S").source:sub(2)
  local bundled = vim.fs.dirname(here) .. "/server/server.js"
  if vim.fn.filereadable(bundled) == 1 then
    return bundled
  end
  return nil
end

-- node: on PATH, else the newest one nvm installed (a Neovim started outside a
-- shell does not have nvm's PATH).
local function find_node()
  if vim.fn.executable("node") == 1 then return "node" end
  local best, key = nil, nil
  for _, bin in ipairs(vim.fn.glob(vim.fn.expand("~/.nvm/versions/node/*/bin/node"), false, true)) do
    local v = bin:match("/v?([%d.]+)/bin/node$") or "0"
    local parts = vim.tbl_map(tonumber, vim.split(v, ".", { plain = true }))
    local k = (parts[1] or 0) * 1e6 + (parts[2] or 0) * 1e3 + (parts[3] or 0)
    if not key or k > key then best, key = bin, k end
  end
  return best
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
  map("<leader>cf", function() vim.lsp.buf.format({ bufnr = bufnr }) end, "Grav: format (grav fmt)")
  map("<leader>ws", vim.lsp.buf.workspace_symbol, "Grav: workspace symbols (incl. std)")
  vim.keymap.set("i", "<C-k>", vim.lsp.buf.signature_help, { buffer = bufnr, silent = true, desc = "Grav: signature help" })
  -- Completion from the server (members, namespaces, import paths, keywords).
  vim.bo[bufnr].omnifunc = "v:lua.vim.lsp.omnifunc"
  if vim.lsp.completion and vim.lsp.completion.enable then
    pcall(vim.lsp.completion.enable, true, _, bufnr, { autotrigger = true })
  end
  -- Inlay hints: the types of bindings that do not write one (Neovim 0.10+).
  -- Turn them off with vim.g.grav_inlay_hints = false.
  if vim.lsp.inlay_hint and vim.lsp.inlay_hint.enable and vim.g.grav_inlay_hints ~= false then
    pcall(vim.lsp.inlay_hint.enable, true, { bufnr = bufnr })
  end
end

function M.start(bufnr)
  bufnr = bufnr or vim.api.nvim_get_current_buf()
  local node = find_node()
  if not node then return false end
  local server = find_server(bufnr)
  if not server then return false end

  local root = nil
  if vim.fs.root then
    root = vim.fs.root(bufnr, { "grav.toml", ".git", "CMakeLists.txt" })
  end
  root = root or vim.fn.getcwd()

  local ok = pcall(vim.lsp.start, {
    name = "grav",
    cmd = { node, server },
    root_dir = root,
    on_attach = on_attach,
    init_options = {
      compilerPath = vim.fn.expand((vim.env.GRAV_DIR or "~/.local/bin") .. "/grav"),
    },
  }, { bufnr = bufnr })
  return ok
end

return M
