#!/usr/bin/env python3
"""
Catty package UI — pick platform / config and ship to Packaged/<Platform>/.

Launched by:
  - Engine root: package.bat
  - Game project: package.bat (resolves EngineDirectory from .cproject)
"""

from __future__ import annotations

import subprocess
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from catty_tools import (  # noqa: E402
	ENGINE_ROOT,
	generate_engine_workspace,
	generate_from_cproject,
	read_cproject,
	resolve_engine_directory,
	run_package,
)

# UI platform id → whether packaging is implemented today
_PLATFORMS = [
	("Win64", True),
	("Linux", False),
	("Mac", False),
]

_CONFIGS = ("Release", "Debug", "RelWithDebInfo", "MinSizeRel")


def _discover_cproject(project_dir: Path) -> Path | None:
	hits = sorted(project_dir.glob("*.cproject"))
	return hits[0] if hits else None


def _default_cproject_from_argv(argv: list[str]) -> Path | None:
	if len(argv) >= 2:
		p = Path(argv[1]).expanduser().resolve()
		if p.is_file() and p.suffix.lower() == ".cproject":
			return p
		if p.is_dir():
			return _discover_cproject(p)
	# Engine workspace: no default .cproject
	return None


class PackageApp(tk.Tk):
	def __init__(self, initial_cproject: Path | None = None) -> None:
		super().__init__()
		self.title("Catty — Package")
		self.geometry("640x460")
		self.minsize(560, 400)

		self.var_cproject = tk.StringVar(value=str(initial_cproject) if initial_cproject else "")
		self.var_platform = tk.StringVar(value="Win64")
		self.var_config = tk.StringVar(value="Release")
		self.var_regen = tk.BooleanVar(value=False)
		self.var_open = tk.BooleanVar(value=True)
		self._busy = False

		self._build()
		self._refresh_summary()

	def _build(self) -> None:
		pad = {"padx": 12, "pady": 6}
		frm = ttk.Frame(self, padding=12)
		frm.pack(fill=tk.BOTH, expand=True)

		ttk.Label(frm, text="Package game / workspace", font=("Segoe UI", 12, "bold")).grid(
			row=0, column=0, columnspan=3, sticky="w", **pad
		)

		ttk.Label(frm, text=".cproject").grid(row=1, column=0, sticky="w", **pad)
		ttk.Entry(frm, textvariable=self.var_cproject).grid(row=1, column=1, sticky="ew", **pad)
		ttk.Button(frm, text="Browse…", command=self._browse_cproject).grid(row=1, column=2, sticky="e", **pad)

		ttk.Label(frm, text="(Leave empty to package the engine workspace)", foreground="#666").grid(
			row=2, column=1, columnspan=2, sticky="w", padx=12
		)

		ttk.Label(frm, text="Platform").grid(row=3, column=0, sticky="w", **pad)
		plat_box = ttk.Combobox(
			frm,
			textvariable=self.var_platform,
			values=[name for name, _ in _PLATFORMS],
			state="readonly",
			width=24,
		)
		plat_box.grid(row=3, column=1, sticky="w", **pad)
		plat_box.bind("<<ComboboxSelected>>", lambda _e: self._refresh_summary())

		ttk.Label(frm, text="Configuration").grid(row=4, column=0, sticky="w", **pad)
		cfg = ttk.Combobox(frm, textvariable=self.var_config, values=_CONFIGS, state="readonly", width=24)
		cfg.grid(row=4, column=1, sticky="w", **pad)
		cfg.bind("<<ComboboxSelected>>", lambda _e: self._refresh_summary())

		opts = ttk.Frame(frm)
		opts.grid(row=5, column=0, columnspan=3, sticky="w", **pad)
		ttk.Checkbutton(opts, text="Regenerate project files before package", variable=self.var_regen).pack(
			side=tk.LEFT, padx=(0, 16)
		)
		ttk.Checkbutton(opts, text="Open Packaged folder when done", variable=self.var_open).pack(side=tk.LEFT)

		ttk.Label(frm, text="Summary").grid(row=6, column=0, sticky="nw", **pad)
		self.txt = tk.Text(frm, height=8, wrap=tk.WORD, state=tk.DISABLED)
		self.txt.grid(row=6, column=1, columnspan=2, sticky="nsew", **pad)

		self.status = ttk.Label(frm, text="Ready", foreground="#336633")
		self.status.grid(row=7, column=0, columnspan=3, sticky="w", **pad)

		btns = ttk.Frame(frm)
		btns.grid(row=8, column=0, columnspan=3, sticky="ew", **pad)
		self.btn_run = ttk.Button(btns, text="Package", command=self._start_package)
		self.btn_run.pack(side=tk.RIGHT, padx=(8, 0))
		ttk.Button(btns, text="Close", command=self.destroy).pack(side=tk.RIGHT)

		frm.columnconfigure(1, weight=1)
		frm.rowconfigure(6, weight=1)

		self.var_cproject.trace_add("write", lambda *_: self._refresh_summary())

	def _browse_cproject(self) -> None:
		path = filedialog.askopenfilename(
			title="Select .cproject",
			filetypes=[("Catty Project", "*.cproject"), ("All", "*.*")],
			initialdir=str(ENGINE_ROOT),
		)
		if path:
			self.var_cproject.set(path)

	def _set_text(self, content: str) -> None:
		self.txt.configure(state=tk.NORMAL)
		self.txt.delete("1.0", tk.END)
		self.txt.insert("1.0", content)
		self.txt.configure(state=tk.DISABLED)

	def _refresh_summary(self) -> None:
		cproject_str = self.var_cproject.get().strip()
		platform = self.var_platform.get()
		config = self.var_config.get()
		enabled = dict(_PLATFORMS).get(platform, False)

		lines: list[str] = []
		if cproject_str:
			cp = Path(cproject_str)
			lines.append(f"Mode      : Game project")
			lines.append(f".cproject  : {cp}")
			try:
				data = read_cproject(cp)
				engine = resolve_engine_directory(cp, data)
				project_dir = cp.parent
				lines.append(f"Project   : {data.get('ProjectName', cp.stem)}")
				lines.append(f"Engine    : {engine}")
				lines.append(f"Output    : {project_dir / 'Packaged' / platform}")
			except Exception as ex:  # noqa: BLE001
				lines.append(f"Error     : {ex}")
		else:
			lines.append(f"Mode      : Engine workspace")
			lines.append(f"Engine    : {ENGINE_ROOT}")
			lines.append(f"Output    : {ENGINE_ROOT / 'Packaged' / platform}")

		lines.append(f"Platform  : {platform}" + ("" if enabled else "  (not implemented yet)"))
		lines.append(f"Config    : {config}")
		lines.append(f"Regenerate: {'yes' if self.var_regen.get() else 'no'}")
		self._set_text("\n".join(lines))

	def _set_busy(self, busy: bool, msg: str = "") -> None:
		self._busy = busy
		self.btn_run.configure(state=tk.DISABLED if busy else tk.NORMAL)
		self.status.configure(text=msg or ("Working…" if busy else "Ready"))

	def _start_package(self) -> None:
		if self._busy:
			return

		platform = self.var_platform.get()
		if not dict(_PLATFORMS).get(platform, False):
			messagebox.showerror("Catty", f"Platform '{platform}' is not implemented yet.\nUse Win64.")
			return

		cproject_str = self.var_cproject.get().strip()
		cproject: Path | None = None
		if cproject_str:
			cproject = Path(cproject_str).expanduser().resolve()
			if not cproject.is_file():
				messagebox.showerror("Catty", f".cproject not found:\n{cproject}")
				return

		config = self.var_config.get()
		regen = self.var_regen.get()
		open_folder = self.var_open.get()

		self._set_busy(True, "Packaging…")
		threading.Thread(
			target=self._run_package_worker,
			args=(cproject, platform, config, regen, open_folder),
			daemon=True,
		).start()

	def _run_package_worker(
		self,
		cproject: Path | None,
		platform: str,
		config: str,
		regen: bool,
		open_folder: bool,
	) -> None:
		try:
			if cproject is not None:
				if regen or not (cproject.parent / "Intermediate" / "CMakeCache.txt").is_file():
					generate_from_cproject(cproject)
				project_dir = cproject.parent
				label = read_cproject(cproject).get("ProjectName", cproject.stem)
			else:
				if regen or not (ENGINE_ROOT / "Intermediate" / "CMakeCache.txt").is_file():
					generate_engine_workspace(ENGINE_ROOT)
				project_dir = ENGINE_ROOT
				label = "CattyWorkspace"

			# Platform folder name must match CattyDirectories (Win64 today).
			run_package(project_dir, config=config, platform=platform)
			out_dir = project_dir / "Packaged" / platform

			def done_ok() -> None:
				self._set_busy(False, f"Done → {out_dir}")
				messagebox.showinfo("Catty", f"Package finished for {label}\n\n{out_dir}")
				if open_folder and out_dir.is_dir():
					if sys.platform == "win32":
						subprocess.Popen(["explorer", str(out_dir)])
					elif sys.platform == "darwin":
						subprocess.Popen(["open", str(out_dir)])
					else:
						subprocess.Popen(["xdg-open", str(out_dir)])

			self.after(0, done_ok)
		except Exception as ex:  # noqa: BLE001
			err = str(ex)

			def done_err() -> None:
				self._set_busy(False, "Failed")
				messagebox.showerror("Catty", err)

			self.after(0, done_err)


def main(argv: list[str]) -> int:
	initial = _default_cproject_from_argv(argv)
	app = PackageApp(initial_cproject=initial)
	app.mainloop()
	return 0


if __name__ == "__main__":
	raise SystemExit(main(sys.argv))
