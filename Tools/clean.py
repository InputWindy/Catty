#!/usr/bin/env python3
"""
One-click clean: remove generated/temp files, keep project essentials.

Deletes without prompting by default. Pass --ask to confirm first.
Wipes Intermediate/Binaries/Packaged/Cached/Saved entirely (no leftover README).
Does NOT remove Tools/python (engine-local interpreter from setup.bat).

Usage:
  Tools\\clean.bat
  Tools\\clean.bat --ask
  Tools\\clean.bat --dry-run
  Tools\\clean.bat path\\Game.cproject
  Tools\\clean.bat path\\GameFolder --ask
"""

from __future__ import annotations

import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from catty_tools import (  # noqa: E402
	ENGINE_ROOT,
	clean_project_tree,
	collect_clean_targets,
)


def _resolve_target(argv: list[str]) -> tuple[Path, bool, bool]:
	# Default: proceed without asking. Use --ask to force a confirmation prompt.
	ask = False
	dry = False
	path_arg: Path | None = None

	for arg in argv[1:]:
		key = arg.lower()
		if key in {"-y", "--yes"}:
			ask = False  # explicit, but already the default
		elif key in {"--ask", "-i", "--interactive"}:
			ask = True
		elif key in {"-n", "--dry-run", "--dryrun"}:
			dry = True
		elif key in {"-h", "--help"}:
			print(__doc__)
			raise SystemExit(0)
		else:
			path_arg = Path(arg).expanduser().resolve()

	if path_arg is None:
		return ENGINE_ROOT, ask, dry

	if path_arg.is_file() and path_arg.suffix.lower() == ".cproject":
		return path_arg.parent, ask, dry
	if path_arg.is_dir():
		return path_arg, ask, dry
	raise FileNotFoundError(f"Not a project dir or .cproject: {path_arg}")


def main(argv: list[str]) -> int:
	try:
		root, ask, dry = _resolve_target(argv)
		targets = collect_clean_targets(root)

		print(f"[Catty] Clean root: {root}")
		if not targets:
			print("[Catty] Nothing to clean.")
			return 0

		print(f"[Catty] {len(targets)} path(s) to remove:")
		for t in targets:
			kind = "dir " if t.is_dir() else "file"
			print(f"  - [{kind}] {t}")

		if dry:
			print("[Catty] Dry-run only (no changes).")
			return 0

		if ask:
			answer = input("Proceed? [Y/n]: ").strip().lower()
			if answer in {"n", "no"}:
				print("[Catty] Cancelled.")
				return 1

		removed = clean_project_tree(root, dry_run=False)
		print(f"[Catty] Removed {len(removed)} path(s).")
		print("[Catty] Kept: source, Build/, Tools/, Doc/, bats, configs.")
		return 0
	except Exception as ex:  # noqa: BLE001
		print(f"[ERROR] {ex}")
		return 1


if __name__ == "__main__":
	raise SystemExit(main(sys.argv))
