-- LazyVim plugin spec for the Grav language.
--
-- Install: copy this file to ~/.config/nvim/lua/plugins/grav.lua
-- It provides filetype detection, build/run keymaps, and (optionally) registers
-- the gravc MCP server with mcphub.nvim.
--
-- The compiler is installed as `grav` in ~/.local/bin by the MCP server (or
-- `cp build/gravc ~/.local/bin/grav`). Override the dir with GRAV_DIR.
local gravc = vim.fn.expand((vim.env.GRAV_DIR or "~/.local/bin") .. "/grav")
-- Repo (only needed to launch the MCP server) — set GRAVC_REPO to override.
local repo = vim.fn.expand(vim.env.GRAVC_REPO or "~/CLionProjects/Practice")

return {
  -- 1) Filetype + syntax (no external plugin needed; ships with this repo's
  --    editors/nvim/{syntax,ftdetect,ftplugin}). This spec just adds tooling.
  {
    "folke/which-key.nvim",
    optional = true,
    opts = {
      spec = {
        { "<leader>m", group = "grav", icon = "" },
      },
    },
  },

  -- 2) Build / run keymaps for *.grav buffers.
  {
    "LazyVim/LazyVim",
    opts = function()
      vim.api.nvim_create_autocmd("FileType", {
        pattern = "grav",
        callback = function(ev)
          local function run(cmd)
            vim.cmd("write")
            vim.cmd("botright split | terminal " .. cmd)
            vim.cmd("startinsert")
          end
          local file = vim.api.nvim_buf_get_name(ev.buf)
          local map = function(lhs, cmd, desc)
            vim.keymap.set("n", lhs, cmd, { buffer = ev.buf, desc = desc })
          end
          -- <leader>mr : build and run
          map("<leader>mr", function()
            run(string.format("%s %q --run", gravc, file))
          end, "Grav: run")
          -- <leader>mc : transpile to C and show it
          map("<leader>mc", function()
            run(string.format("%s %q --emit c -o /dev/stdout", gravc, file))
          end, "Grav: emit C")
          -- <leader>ms : emit assembly (optimized)
          map("<leader>ms", function()
            run(string.format("%s %q -S -O2 -o /dev/stdout", gravc, file))
          end, "Grav: emit asm")
        end,
      })
    end,
  },

  -- 3) Optional: expose the gravc MCP server to nvim via mcphub.nvim.
  --    Remove this block if you don't use mcphub.
  {
    "ravitemer/mcphub.nvim",
    optional = true,
    opts = {
      servers = {
        gravc = {
          command = "python3",
          args = { repo .. "/mcp_server/server.py" },
          -- The server self-installs the latest build as ~/.local/bin/grav.
          env = { GRAVC = gravc },
        },
      },
    },
  },
}
