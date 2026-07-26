#!/usr/bin/env python3
"""
Generate Visual Studio solution for a Catty project.

Usage:
  python generateProject.py                  # engine workspace (Catty + Test0)
  python generateProject.py path\\Game.cproject
  (double-click .cproject after association in setup.py)
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "Build" / "python"))

from catty_tools import (  # noqa: E402
	ENGINE_ROOT,
	generate_engine_workspace,
	generate_from_cproject,
)


def main(argv: list[str]) -> int:
	try:
		if len(argv) >= 2:
			target = Path(argv[1]).expanduser().resolve()
			if target.suffix.lower() != ".cproject":
				print(f"[ERROR] Expected a .cproject file, got: {target}")
				return 1
			if not target.is_file():
				print(f"[ERROR] File not found: {target}")
				return 1
			sln = generate_from_cproject(target)
		else:
			print("[Catty] No .cproject given — generating engine workspace SLN")
			sln = generate_engine_workspace(ENGINE_ROOT)

		print("[Catty] Done. Open the .sln in Visual Studio:")
		print(f"        {sln}")
		return 0
	except Exception as ex:  # noqa: BLE001
		print(f"[ERROR] {ex}")
		return 1


if __name__ == "__main__":
	raise SystemExit(main(sys.argv))
