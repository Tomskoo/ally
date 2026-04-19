#!/usr/bin/env python3
"""Convert a Neovim theme plugin into an ally-compatible YAML theme file.

Handles the 3-layer resolution chain used by themes like monokai-pro.nvim:
  palette/<variant>.lua  ->  scheme.lua (scheme.base)  ->  treesitter.lua

Usage:
  python3 tools/nvim_theme_to_yaml.py \
    --plugin-dir ~/.local/share/nvim/lazy/monokai-pro.nvim \
    --variant pro \
    --output ~/.config/ally/themes/monokai-pro.yaml
"""

import argparse
import os
import re
import sys
from pathlib import Path


def find_plugin_name(plugin_dir: Path) -> str:
    """Discover the plugin name by scanning lua/<name>/."""
    lua_dir = plugin_dir / "lua"
    if not lua_dir.is_dir():
        sys.exit(f"Error: {lua_dir} does not exist")
    candidates = [d.name for d in lua_dir.iterdir() if d.is_dir() and not d.name.startswith(".")]
    if len(candidates) == 1:
        return candidates[0]
    # Filter out common integration dirs that aren't the main plugin
    integration_dirs = {"lualine", "lightline", "barbecue", "bufferline", "galaxyline", "feline"}
    filtered = [c for c in candidates if c not in integration_dirs]
    if len(filtered) == 1:
        return filtered[0]
    sys.exit(f"Error: ambiguous plugin dirs in {lua_dir}: {candidates}. Specify --plugin-name.")


def parse_lua_table(text: str) -> dict[str, str]:
    """Parse a simple Lua table of string values: { key = "value", ... }."""
    return dict(re.findall(r'(\w+)\s*=\s*"(#[0-9a-fA-F]{6})"', text))


def parse_scheme_base(text: str) -> dict[str, str]:
    """Parse scheme.base = { color = p.palette_key, ... } from scheme.lua."""
    # Find the scheme.base block
    match = re.search(r'scheme\.base\s*=\s*\{([^}]+)\}', text, re.DOTALL)
    if not match:
        sys.exit("Error: could not find scheme.base block in scheme.lua")
    block = match.group(1)
    return dict(re.findall(r'(\w+)\s*=\s*p\.(\w+)', block))


def parse_treesitter_highlights(text: str) -> dict[str, str]:
    """Parse treesitter highlight mappings: ["@capture"] = { fg = c.base.color }."""
    results = {}
    for match in re.finditer(
        r'\["@([\w.]+)"\]\s*=\s*\{[^}]*?fg\s*=\s*c\.base\.(\w+)', text
    ):
        capture, color = match.group(1), match.group(2)
        results[capture] = color
    return results


def find_file(base: Path, *candidates: str) -> Path:
    """Try multiple relative paths under base, return the first that exists."""
    for c in candidates:
        p = base / c
        if p.exists():
            return p
    sys.exit(f"Error: none of these files found under {base}: {list(candidates)}")


def build_yaml(
    name: str,
    palette_hex: dict[str, str],
    scheme_base: dict[str, str],
    highlights: dict[str, str],
) -> str:
    """Build the ally YAML theme string."""
    # Resolve base colors to hex: base_name -> hex
    base_to_hex = {}
    for base_name, palette_key in scheme_base.items():
        if palette_key in palette_hex:
            base_to_hex[base_name] = palette_hex[palette_key]

    # Also add raw palette entries that are direct hex (background, text, etc.)
    for key, val in palette_hex.items():
        if key not in base_to_hex:
            base_to_hex[key] = val

    # Build palette section with fg/bg aliases
    palette_out: dict[str, str] = {}

    # Map fg/bg from theme conventions
    fg_candidates = ["white", "text"]
    bg_candidates = ["background", "dark", "black"]
    for c in fg_candidates:
        if c in base_to_hex:
            palette_out["fg"] = base_to_hex[c]
            break
    for c in bg_candidates:
        if c in base_to_hex:
            palette_out["bg"] = base_to_hex[c]
            break

    # Add all base colors (skip ones already aliased as fg/bg)
    for base_name in sorted(base_to_hex):
        if base_name not in palette_out and base_name not in ("text", "background"):
            palette_out[base_name] = base_to_hex[base_name]

    # Build highlights section: capture -> palette key
    highlights_out: dict[str, str] = {}
    for capture in sorted(highlights):
        color_name = highlights[capture]
        # Map "white" -> "fg" in highlights for readability
        if color_name == "white" and "fg" in palette_out:
            highlights_out[capture] = "fg"
        else:
            highlights_out[capture] = color_name

    # Render YAML
    lines = [f'name: "{name}"', "", "palette:"]
    # fg and bg first, then alphabetical
    key_order = []
    if "fg" in palette_out:
        key_order.append("fg")
    if "bg" in palette_out:
        key_order.append("bg")
    key_order.extend(k for k in sorted(palette_out) if k not in ("fg", "bg"))

    max_key_len = max(len(k) for k in key_order) if key_order else 0
    for k in key_order:
        lines.append(f'  {k + ":":<{max_key_len + 1}} "{palette_out[k]}"')

    lines.extend(["", "highlights:"])

    # Group highlights by category (first segment)
    if highlights_out:
        max_cap_len = max(len(c) for c in highlights_out)
        prev_group = None
        for cap in sorted(highlights_out):
            group = cap.split(".")[0]
            if prev_group is not None and group != prev_group:
                lines.append("")  # blank line between groups
            prev_group = group
            lines.append(f"  {cap + ':':<{max_cap_len + 1}} {highlights_out[cap]}")

    lines.append("")  # trailing newline
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Convert a Neovim theme plugin to ally YAML format"
    )
    parser.add_argument(
        "--plugin-dir",
        required=True,
        type=Path,
        help="Path to the nvim theme plugin (e.g. ~/.local/share/nvim/lazy/monokai-pro.nvim)",
    )
    parser.add_argument(
        "--variant",
        default="pro",
        help="Palette variant name (default: pro)",
    )
    parser.add_argument(
        "--plugin-name",
        help="Override auto-detected plugin name (the directory under lua/)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Output YAML file path (default: stdout)",
    )
    args = parser.parse_args()

    plugin_dir = args.plugin_dir.expanduser().resolve()
    if not plugin_dir.is_dir():
        sys.exit(f"Error: {plugin_dir} is not a directory")

    plugin_name = args.plugin_name or find_plugin_name(plugin_dir)
    # Normalize plugin name for paths (monokai-pro.nvim -> monokai-pro)
    lua_name = plugin_name.replace(".", "-").replace("_", "-")
    # Try the name as-is first, then with common transformations
    lua_base = plugin_dir / "lua"
    lua_plugin_dir = None
    for candidate in [lua_name, plugin_name, plugin_name.replace("-", "_")]:
        if (lua_base / candidate).is_dir():
            lua_plugin_dir = lua_base / candidate
            break
    if lua_plugin_dir is None:
        sys.exit(f"Error: could not find plugin lua dir under {lua_base}")

    # 1. Parse palette
    palette_file = find_file(
        lua_plugin_dir,
        f"palette/{args.variant}.lua",
        f"colors/{args.variant}.lua",
    )
    palette_hex = parse_lua_table(palette_file.read_text())
    if not palette_hex:
        sys.exit(f"Error: no hex colors found in {palette_file}")

    # 2. Parse scheme base mapping
    scheme_file = find_file(
        lua_plugin_dir,
        "theme/scheme.lua",
        "scheme.lua",
    )
    scheme_base = parse_scheme_base(scheme_file.read_text())
    if not scheme_base:
        sys.exit(f"Error: no scheme.base mappings found in {scheme_file}")

    # 3. Parse treesitter highlights
    ts_file = find_file(
        lua_plugin_dir,
        "theme/plugins/treesitter.lua",
        "plugins/treesitter.lua",
        "treesitter.lua",
    )
    highlights = parse_treesitter_highlights(ts_file.read_text())
    if not highlights:
        sys.exit(f"Error: no treesitter highlights found in {ts_file}")

    # 4. Build output
    theme_name = f"{plugin_name} ({args.variant})"
    yaml_content = build_yaml(theme_name, palette_hex, scheme_base, highlights)

    if args.output:
        output_path = args.output.expanduser().resolve()
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(yaml_content)
        print(f"Wrote {output_path}", file=sys.stderr)
    else:
        print(yaml_content)


if __name__ == "__main__":
    main()
