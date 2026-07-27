#!/usr/bin/env python3
"""Catty new-project UI (createProject.bat)."""

from __future__ import annotations

import sys
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from catty_tools import (  # noqa: E402
	ENGINE_ROOT,
	create_project,
	generate_from_cproject,
	install_windows_cproject_association,
	is_valid_project_name,
)


class CreateProjectApp(tk.Tk):
	def __init__(self) -> None:
		super().__init__()
		self.title("Catty — New Project")
		self.geometry("640x420")
		self.minsize(560, 380)
		self.resizable(True, True)

		self.var_name = tk.StringVar(value="MyGame")
		self.var_parent = tk.StringVar(value=str(Path.home() / "Documents" / "CattyProjects"))
		self.var_engine = tk.StringVar(value=str(ENGINE_ROOT))
		self.var_author = tk.StringVar(value="")
		self.var_desc = tk.StringVar(value="")
		self.var_gen_sln = tk.BooleanVar(value=True)
		self.var_open_folder = tk.BooleanVar(value=True)

		self._build()
		self._auto_associate_cproject()

	def _build(self) -> None:
		pad = {"padx": 12, "pady": 6}
		frm = ttk.Frame(self, padding=12)
		frm.pack(fill=tk.BOTH, expand=True)

		ttk.Label(frm, text="Create a new Catty game project", font=("Segoe UI", 12, "bold")).grid(
			row=0, column=0, columnspan=3, sticky="w", **pad
		)

		ttk.Label(frm, text="Project Name").grid(row=1, column=0, sticky="w", **pad)
		ttk.Entry(frm, textvariable=self.var_name).grid(row=1, column=1, columnspan=2, sticky="ew", **pad)

		ttk.Label(frm, text="Parent Folder").grid(row=2, column=0, sticky="w", **pad)
		ttk.Entry(frm, textvariable=self.var_parent).grid(row=2, column=1, sticky="ew", **pad)
		ttk.Button(frm, text="Browse…", command=self._browse_parent).grid(row=2, column=2, sticky="e", **pad)

		ttk.Label(frm, text="Engine Root").grid(row=3, column=0, sticky="w", **pad)
		ttk.Entry(frm, textvariable=self.var_engine).grid(row=3, column=1, sticky="ew", **pad)
		ttk.Button(frm, text="Browse…", command=self._browse_engine).grid(row=3, column=2, sticky="e", **pad)

		ttk.Label(frm, text="Author").grid(row=4, column=0, sticky="w", **pad)
		ttk.Entry(frm, textvariable=self.var_author).grid(row=4, column=1, columnspan=2, sticky="ew", **pad)

		ttk.Label(frm, text="Description").grid(row=5, column=0, sticky="nw", **pad)
		self.txt_desc = tk.Text(frm, height=4, wrap=tk.WORD)
		self.txt_desc.grid(row=5, column=1, columnspan=2, sticky="nsew", **pad)

		opts = ttk.Frame(frm)
		opts.grid(row=6, column=0, columnspan=3, sticky="w", **pad)
		ttk.Checkbutton(opts, text="Generate .sln after create", variable=self.var_gen_sln).pack(side=tk.LEFT, padx=(0, 16))
		ttk.Checkbutton(opts, text="Open project folder", variable=self.var_open_folder).pack(side=tk.LEFT)

		hint = (
			"Creates Parent/Name/ with Name.cproject (JSON, like .uproject).\n"
			"Double-click the .cproject to regenerate Name.sln beside it, then open the .sln in VS.\n"
			".cproject file association is applied automatically on Windows when this window opens.\n"
			"Requires engine setup.bat (local Tools/python) beforehand."
		)
		ttk.Label(frm, text=hint, foreground="#555").grid(row=7, column=0, columnspan=3, sticky="w", **pad)

		btns = ttk.Frame(frm)
		btns.grid(row=8, column=0, columnspan=3, sticky="ew", **pad)
		ttk.Button(btns, text="Re-associate .cproject", command=self._associate).pack(side=tk.LEFT)
		ttk.Button(btns, text="Create Project", command=self._create).pack(side=tk.RIGHT, padx=(8, 0))
		ttk.Button(btns, text="Close", command=self.destroy).pack(side=tk.RIGHT)

		frm.columnconfigure(1, weight=1)
		frm.rowconfigure(5, weight=1)

	def _browse_parent(self) -> None:
		path = filedialog.askdirectory(initialdir=self.var_parent.get() or str(Path.home()))
		if path:
			self.var_parent.set(path)

	def _browse_engine(self) -> None:
		path = filedialog.askdirectory(initialdir=self.var_engine.get() or str(ENGINE_ROOT))
		if path:
			self.var_engine.set(path)

	def _auto_associate_cproject(self) -> None:
		"""Register .cproject → Tools/generateProject.bat when the UI opens (Windows)."""
		if sys.platform != "win32":
			return
		try:
			install_windows_cproject_association()
		except Exception as ex:  # noqa: BLE001
			print(f"[Catty] Auto-associate skipped: {ex}")

	def _associate(self) -> None:
		try:
			install_windows_cproject_association()
			messagebox.showinfo(
				"Catty",
				"Associated .cproject with Tools/generateProject.bat for the current Windows user.\n"
				"You may need to sign out/in once for Explorer to refresh icons.",
			)
		except Exception as ex:  # noqa: BLE001
			messagebox.showerror("Catty", str(ex))

	def _create(self) -> None:
		name = self.var_name.get().strip()
		parent = Path(self.var_parent.get().strip())
		engine = Path(self.var_engine.get().strip())
		author = self.var_author.get().strip()
		desc = self.txt_desc.get("1.0", tk.END).strip()

		if not is_valid_project_name(name):
			messagebox.showerror("Catty", "Invalid project name.\nUse Letter + A-Z a-z 0-9 _")
			return
		if not parent:
			messagebox.showerror("Catty", "Parent folder is required.")
			return
		if not (engine / "Catty").is_dir():
			messagebox.showerror("Catty", f"Engine root must contain Catty/:\n{engine}")
			return
		if not (engine / "Tools" / "python" / "python.exe").is_file():
			messagebox.showerror(
				"Catty",
				f"Engine local Python missing.\nRun setup.bat in:\n{engine}",
			)
			return

		try:
			self._auto_associate_cproject()
			cproject = create_project(name, parent, engine, description=desc, author=author)
			sln_msg = ""
			if self.var_gen_sln.get():
				sln = generate_from_cproject(cproject)
				sln_msg = f"\nSLN: {sln}"
			if self.var_open_folder.get():
				path = cproject.parent
				if sys.platform == "win32":
					import os

					os.startfile(path)  # type: ignore[attr-defined]
				elif sys.platform == "darwin":
					import subprocess

					subprocess.Popen(["open", str(path)])
				else:
					import subprocess

					subprocess.Popen(["xdg-open", str(path)])
			messagebox.showinfo("Catty", f"Project created:\n{cproject}{sln_msg}")
		except Exception as ex:  # noqa: BLE001
			messagebox.showerror("Catty", str(ex))


def main() -> int:
	app = CreateProjectApp()
	app.mainloop()
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
