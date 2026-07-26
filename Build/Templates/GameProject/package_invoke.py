#!/usr/bin/env python3
"""Project-local launcher: resolve EngineDirectory from *.cproject and open package UI."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path


def main() -> int:
	here = Path(__file__).resolve().parent
	cprojects = sorted(here.glob("*.cproject"))
	if not cprojects:
		print(f"[ERROR] No .cproject in {here}")
		return 1

	cproject = cprojects[0]
	data = json.loads(cproject.read_text(encoding="utf-8"))
	eng = Path(str(data.get("EngineDirectory", "")))
	if not eng.is_absolute():
		eng = (cproject.parent / eng).resolve()
	else:
		eng = eng.resolve()

	ui = eng / "Tools" / "package_ui.py"
	print(f"[Catty] Project : {cproject}")
	print(f"[Catty] Engine  : {eng}")
	print(f"[Catty] UI      : {ui}")

	if not ui.is_file():
		print(f"[ERROR] Missing package UI: {ui}")
		return 1

	return subprocess.call([sys.executable, str(ui), str(cproject)])


if __name__ == "__main__":
	raise SystemExit(main())
