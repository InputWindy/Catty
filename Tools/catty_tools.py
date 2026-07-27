# Shared helpers for create_project.py / generateProject.py / package.py / clean.py
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import threading
from pathlib import Path
from typing import Any


# Tools/catty_tools.py → repo root is parent of Tools/
ENGINE_ROOT = Path(__file__).resolve().parents[1]
TEMPLATE_DIR = ENGINE_ROOT / "Build" / "Templates" / "GameProject"
CPROJECT_VERSION = 1
ENGINE_PYTHON_DIR = (ENGINE_ROOT / "Tools" / "python").resolve()


def ensure_engine_python() -> None:
	"""
	Require the engine-local interpreter under Tools/python (from setup.bat).
	Refuse system / PATH Python so tool scripts stay reproducible.
	Set CATTY_ALLOW_SYSTEM_PYTHON=1 only for emergency debugging.
	"""
	if os.environ.get("CATTY_ALLOW_SYSTEM_PYTHON") == "1":
		return

	exe = Path(sys.executable).resolve()
	try:
		exe.relative_to(ENGINE_PYTHON_DIR)
		return
	except ValueError:
		pass

	msg = (
		"This Catty tool must run with the engine-local Python, not a system install.\n\n"
		f"Expected under:\n  {ENGINE_PYTHON_DIR}\n\n"
		f"Current interpreter:\n  {exe}\n\n"
		"Fix:\n"
		"  1) Run setup.bat in the Catty engine root\n"
		"  2) Launch via *.bat / Tools\\catty_python.bat / Tools\\catty_pythonw.bat\n"
	)
	try:
		sys.stderr.write("[ERROR] " + msg.replace("\n", "\n[ERROR] ") + "\n")
		sys.stderr.flush()
	except Exception:
		pass

	# pythonw has no console — show a dialog so the failure is visible.
	if sys.platform == "win32":
		try:
			import ctypes

			ctypes.windll.user32.MessageBoxW(0, msg, "Catty — wrong Python", 0x10)
		except Exception:
			pass

	raise SystemExit(1)


# Enforce on every import of catty_tools (all tool entry scripts go through here,
# except reflect_codegen which imports this module for the same check).
ensure_engine_python()


def is_valid_project_name(name: str) -> bool:
	return bool(re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", name or ""))


def find_cmake() -> str:
	exe = shutil.which("cmake")
	if not exe:
		raise RuntimeError("cmake not found in PATH. Install CMake and restart the terminal.")
	# Prefer a real .exe — launching cmake.bat from pythonw can flash a console.
	path = Path(exe)
	if path.suffix.lower() in {".bat", ".cmd"}:
		sibling = path.with_suffix(".exe")
		if sibling.is_file():
			return str(sibling)
	return str(path.resolve()) if path.exists() else exe


def _subprocess_no_window_kwargs() -> dict[str, Any]:
	"""Avoid flashing a console when spawning tools from pythonw / GUI."""
	if sys.platform != "win32":
		return {}
	startupinfo = subprocess.STARTUPINFO()
	startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
	startupinfo.wShowWindow = subprocess.SW_HIDE
	return {
		"creationflags": subprocess.CREATE_NO_WINDOW,  # type: ignore[attr-defined]
		"startupinfo": startupinfo,
	}


def open_in_file_manager(path: Path) -> None:
	"""Open a folder/file in the OS file manager without a console flash."""
	path = path.resolve()
	if sys.platform == "win32":
		# explorer.exe is a GUI subsystem binary; still pass no-window flags for safety.
		subprocess.Popen(["explorer", str(path)], **_subprocess_no_window_kwargs())
	elif sys.platform == "darwin":
		subprocess.Popen(["open", str(path)])
	else:
		subprocess.Popen(["xdg-open", str(path)])


class OperationCancelled(Exception):
	"""Raised when a long-running tool command is aborted by the user."""


def run_command(
	cmd: list[str],
	*,
	log: Any = print,
	cancel_event: threading.Event | None = None,
	proc_holder: list[Any] | None = None,
) -> None:
	"""
	Run a process, stream combined stdout/stderr line-by-line to log/print,
	and never attach a new console window on Windows.

	If cancel_event is set (or the process is killed from outside), raises OperationCancelled.
	proc_holder, when provided, receives the live Popen so the UI can kill it on abort.
	"""
	if cancel_event is not None and cancel_event.is_set():
		raise OperationCancelled("Cancelled before start")

	log("[Catty] $ " + " ".join(cmd))
	proc = subprocess.Popen(
		cmd,
		stdout=subprocess.PIPE,
		stderr=subprocess.STDOUT,
		text=True,
		encoding="utf-8",
		errors="replace",
		bufsize=1,
		**_subprocess_no_window_kwargs(),
	)
	if proc_holder is not None:
		proc_holder.clear()
		proc_holder.append(proc)

	assert proc.stdout is not None
	try:
		for line in proc.stdout:
			if cancel_event is not None and cancel_event.is_set():
				_kill_process(proc)
				raise OperationCancelled("Cancelled")
			text = line.rstrip("\r\n")
			if text:
				log(text)
		rc = proc.wait()
		if cancel_event is not None and cancel_event.is_set():
			raise OperationCancelled("Cancelled")
		if rc != 0:
			raise RuntimeError(f"Command failed (exit {rc}): {' '.join(cmd)}")
	finally:
		if proc_holder is not None and proc_holder and proc_holder[0] is proc:
			proc_holder.clear()


def _kill_process(proc: subprocess.Popen[Any]) -> None:
	try:
		proc.kill()
	except OSError:
		pass
	try:
		proc.wait(timeout=5)
	except Exception:
		pass


def read_cproject(path: Path) -> dict[str, Any]:
	with path.open("r", encoding="utf-8") as f:
		data = json.load(f)
	if "ProjectName" not in data:
		raise ValueError(f"Invalid .cproject (missing ProjectName): {path}")
	return data


def write_cproject(path: Path, data: dict[str, Any]) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	with path.open("w", encoding="utf-8", newline="\n") as f:
		json.dump(data, f, indent=2, ensure_ascii=False)
		f.write("\n")


def resolve_engine_directory(cproject_path: Path, data: dict[str, Any]) -> Path:
	raw = data.get("EngineDirectory") or str(ENGINE_ROOT)
	engine = Path(raw)
	if not engine.is_absolute():
		engine = (cproject_path.parent / engine).resolve()
	else:
		engine = engine.resolve()
	if not (engine / "Catty").is_dir():
		raise FileNotFoundError(f"Catty engine not found under: {engine}")
	return engine


def engine_path_for_cproject(engine_root: Path, project_dir: Path) -> str:
	try:
		rel = os.path.relpath(engine_root, project_dir)
		return rel.replace("\\", "/")
	except ValueError:
		return str(engine_root.resolve()).replace("\\", "/")


def render_template_text(text: str, mapping: dict[str, str]) -> str:
	out = text
	for key, value in mapping.items():
		out = out.replace("{{" + key + "}}", value)
	return out


def copy_template(project_dir: Path, mapping: dict[str, str]) -> None:
	if not TEMPLATE_DIR.is_dir():
		raise FileNotFoundError(f"Template missing: {TEMPLATE_DIR}")

	for src in TEMPLATE_DIR.rglob("*"):
		if src.is_dir():
			continue
		rel = src.relative_to(TEMPLATE_DIR)
		rel_parts = [render_template_text(part, mapping) for part in rel.parts]
		dst = project_dir.joinpath(*rel_parts)
		dst.parent.mkdir(parents=True, exist_ok=True)
		if src.suffix.lower() in {".png", ".bin", ".exe"}:
			shutil.copy2(src, dst)
		else:
			text = src.read_text(encoding="utf-8")
			dst.write_text(render_template_text(text, mapping), encoding="utf-8", newline="\n")


def create_project(
	project_name: str,
	parent_dir: Path,
	engine_root: Path,
	description: str = "",
	author: str = "",
) -> Path:
	if not is_valid_project_name(project_name):
		raise ValueError("Project name must start with a letter and contain only A-Z, a-z, 0-9, _")

	parent_dir = parent_dir.expanduser().resolve()
	engine_root = engine_root.expanduser().resolve()
	project_dir = parent_dir / project_name
	if project_dir.exists() and any(project_dir.iterdir()):
		raise FileExistsError(f"Target folder is not empty: {project_dir}")

	project_dir.mkdir(parents=True, exist_ok=True)
	mapping = {
		"PROJECT_NAME": project_name,
		"APP_CLASS": f"F{project_name}App",
		"DESCRIPTION": description,
		"AUTHOR": author,
	}
	copy_template(project_dir, mapping)

	cproject = {
		"FileVersion": CPROJECT_VERSION,
		"EngineAssociation": "Catty",
		"EngineDirectory": engine_path_for_cproject(engine_root, project_dir),
		"ProjectName": project_name,
		"Description": description,
		"Author": author,
		"Modules": [
			{
				"Name": project_name,
				"Type": "Runtime",
			}
		],
	}
	cproject_path = project_dir / f"{project_name}.cproject"
	write_cproject(cproject_path, cproject)
	return cproject_path


def _rewrite_sln_paths(sln_text: str) -> str:
	def repl(match: re.Match[str]) -> str:
		prefix = match.group(1)
		path = match.group(2)
		suffix = match.group(3)
		norm = path.replace("/", "\\")
		if os.path.isabs(path) or norm.lower().startswith("intermediate\\"):
			return match.group(0)
		return f'{prefix}Intermediate\\{norm}{suffix}'

	return re.sub(
		r'(Project\("[^"]+"\)\s*=\s*"[^"]+",\s*")([^"]+)(")',
		repl,
		sln_text,
	)


# VS / CMake noise that should not appear in the sibling .sln tree.
_SLN_STRIP_PROJECT_NAMES = {
	"ALL_BUILD",
	"ZERO_CHECK",
	"INSTALL",
	"PACKAGE",
	"RUN_TESTS",
	"Nightly",
	"NightlyMemoryCheck",
	"Experimental",
	"Continuous",
	"CMakePredefinedTargets",
	"Packaging",
}


def _strip_sln_noise_projects(sln_text: str) -> str:
	"""Remove CMakePredefinedTargets / Packaging folders and related projects from a .sln."""
	# Collect GUIDs for projects we want to drop (by display name).
	drop_guids: set[str] = set()
	project_re = re.compile(
		r'Project\("\{[^}]+\}"\)\s*=\s*"([^"]+)",\s*"[^"]*",\s*"(\{[^}]+\})"',
		re.IGNORECASE,
	)
	for match in project_re.finditer(sln_text):
		name = match.group(1)
		guid = match.group(2).upper()
		if name in _SLN_STRIP_PROJECT_NAMES:
			drop_guids.add(guid)

	if not drop_guids:
		return sln_text

	# Drop whole Project ... EndProject blocks whose project GUID is in drop_guids.
	block_re = re.compile(
		r'Project\("\{[^}]+\}"\)\s*=\s*"[^"]+",\s*"[^"]*",\s*"(\{[^}]+\})"\s*\r?\n'
		r'.*?EndProject\s*\r?\n?',
		re.IGNORECASE | re.DOTALL,
	)

	def keep_block(match: re.Match[str]) -> str:
		guid = match.group(1).upper()
		return "" if guid in drop_guids else match.group(0)

	text = block_re.sub(keep_block, sln_text)

	# Drop NestedProjects / ProjectConfigurationPlatforms lines that mention dropped GUIDs.
	out_lines: list[str] = []
	for line in text.splitlines(keepends=True):
		upper = line.upper()
		if any(g in upper for g in drop_guids):
			# Keep section headers / EndGlobalSection lines intact.
			stripped = line.strip()
			if stripped.startswith("GlobalSection(") or stripped.startswith("EndGlobalSection"):
				out_lines.append(line)
			continue
		out_lines.append(line)

	return "".join(out_lines)


def emit_sibling_sln(intermediate_dir: Path, project_dir: Path, project_name: str) -> Path:
	candidates = sorted(intermediate_dir.glob("*.sln"))
	if not candidates:
		raise FileNotFoundError(f"No .sln generated under {intermediate_dir}")

	src = None
	for c in candidates:
		if c.stem.lower() == project_name.lower():
			src = c
			break
	if src is None:
		src = candidates[0]

	text = src.read_text(encoding="utf-8", errors="replace")
	text = _rewrite_sln_paths(text)
	text = _strip_sln_noise_projects(text)
	dst = project_dir / f"{project_name}.sln"
	dst.write_text(text, encoding="utf-8", newline="\n")
	return dst


def run_cmake_generate(
	source_dir: Path,
	binary_dir: Path,
	engine_root: Path | None = None,
	*,
	log: Any = print,
	cancel_event: threading.Event | None = None,
	proc_holder: list[Any] | None = None,
) -> None:
	cmake = find_cmake()
	cmd = [
		cmake,
		"-S",
		str(source_dir),
		"-B",
		str(binary_dir),
		"-G",
		"Visual Studio 17 2022",
		"-A",
		"x64",
	]
	if engine_root is not None:
		cmd.append(f"-DCATTY_ENGINE_ROOT={engine_root}")
	run_command(cmd, log=log, cancel_event=cancel_event, proc_holder=proc_holder)


def generate_from_cproject(
	cproject_path: Path,
	*,
	log: Any = print,
	cancel_event: threading.Event | None = None,
	proc_holder: list[Any] | None = None,
) -> Path:
	cproject_path = cproject_path.resolve()
	data = read_cproject(cproject_path)
	project_dir = cproject_path.parent
	project_name = str(data["ProjectName"])
	engine_root = resolve_engine_directory(cproject_path, data)
	intermediate = project_dir / "Intermediate"

	log(f"[Catty] Project : {project_name}")
	log(f"[Catty] Dir     : {project_dir}")
	log(f"[Catty] Engine  : {engine_root}")

	run_cmake_generate(
		project_dir,
		intermediate,
		engine_root=engine_root,
		log=log,
		cancel_event=cancel_event,
		proc_holder=proc_holder,
	)
	if cancel_event is not None and cancel_event.is_set():
		raise OperationCancelled("Cancelled")
	sln = emit_sibling_sln(intermediate, project_dir, project_name)
	log(f"[Catty] Solution: {sln}")
	return sln


def generate_engine_workspace(
	engine_root: Path | None = None,
	*,
	log: Any = print,
	cancel_event: threading.Event | None = None,
	proc_holder: list[Any] | None = None,
) -> Path:
	engine_root = (engine_root or ENGINE_ROOT).resolve()
	source_dir = engine_root / "Build"
	if not (source_dir / "CMakeLists.txt").is_file():
		raise FileNotFoundError(f"Engine CMake entry missing: {source_dir / 'CMakeLists.txt'}")
	intermediate = engine_root / "Intermediate"
	run_cmake_generate(
		source_dir,
		intermediate,
		engine_root=None,
		log=log,
		cancel_event=cancel_event,
		proc_holder=proc_holder,
	)
	if cancel_event is not None and cancel_event.is_set():
		raise OperationCancelled("Cancelled")
	sln = emit_sibling_sln(intermediate, engine_root, "CattyWorkspace")
	log(f"[Catty] Workspace solution: {sln}")
	return sln


def run_package(
	project_dir: Path,
	config: str = "Release",
	platform: str = "Win64",
	*,
	log: Any = print,
	cancel_event: threading.Event | None = None,
	proc_holder: list[Any] | None = None,
) -> None:
	cmake = find_cmake()
	project_dir = project_dir.resolve()
	intermediate = project_dir / "Intermediate"
	if not (intermediate / "CMakeCache.txt").is_file():
		raise RuntimeError("Project not generated yet. Run generateProject on the .cproject first.")

	if cancel_event is not None and cancel_event.is_set():
		raise OperationCancelled("Cancelled")

	packaged = project_dir / "Packaged" / platform
	if packaged.exists():
		shutil.rmtree(packaged, ignore_errors=True)
	packaged.mkdir(parents=True, exist_ok=True)

	run_command(
		[cmake, "--build", str(intermediate), "--config", config],
		log=log,
		cancel_event=cancel_event,
		proc_holder=proc_holder,
	)
	if cancel_event is not None and cancel_event.is_set():
		raise OperationCancelled("Cancelled")
	run_command(
		[
			cmake,
			"--install",
			str(intermediate),
			"--prefix",
			str(packaged),
			"--config",
			config,
			"--component",
			"Runtime",
		],
		log=log,
		cancel_event=cancel_event,
		proc_holder=proc_holder,
	)
	if cancel_event is not None and cancel_event.is_set():
		raise OperationCancelled("Cancelled")
	log(f"[Catty] Packaged → {packaged}")


def install_windows_cproject_association(*, log: Any = print) -> None:
	if sys.platform != "win32":
		raise RuntimeError("File association is only implemented for Windows.")

	import winreg

	# Prefer Tools bat so Explorer double-click stays aligned with internal layout.
	generate_bat = ENGINE_ROOT / "Tools" / "generateProject.bat"
	prog_id = "Catty.CProject"
	command = f"\"{generate_bat}\" \"%1\""

	# Use winreg (no reg.exe) so pythonw GUIs do not flash 3 console windows.
	with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, r"Software\Classes\.cproject") as key:
		winreg.SetValueEx(key, None, 0, winreg.REG_SZ, prog_id)

	with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, rf"Software\Classes\{prog_id}") as key:
		winreg.SetValueEx(key, None, 0, winreg.REG_SZ, "Catty Project")

	with winreg.CreateKeyEx(
		winreg.HKEY_CURRENT_USER,
		rf"Software\Classes\{prog_id}\shell\open\command",
	) as key:
		winreg.SetValueEx(key, None, 0, winreg.REG_SZ, command)

	log("[Catty] Associated .cproject → Tools/generateProject.bat (current user)")
	log(f"[Catty] Command: {command}")


# Entire trees wiped by clean (including any README/.gitkeep inside).
_WIPE_DIR_NAMES = (
	"Intermediate",
	"Binaries",
	"Packaged",
	"Cached",
	"Saved",
	"out",
	"cmake-build-debug",
	"cmake-build-release",
	".vs",
)

_DELETE_NAME_GLOBS = (
	"*.sln",
	"*.vcxproj",
	"*.vcxproj.filters",
	"*.vcxproj.user",
	"CMakeUserPresets.json",
	"compile_commands.json",
)


def _rm_tree(path: Path) -> bool:
	if not path.exists():
		return False
	try:
		if path.is_file() or path.is_symlink():
			path.unlink(missing_ok=True)
		else:
			shutil.rmtree(path, ignore_errors=True)
			if path.exists():
				# Windows file locks: best-effort second pass via cmd
				if sys.platform == "win32":
					subprocess.call(
						["cmd", "/c", "rmdir", "/s", "/q", str(path)],
						shell=False,
						**_subprocess_no_window_kwargs(),
					)
		return not path.exists()
	except OSError:
		return False


def _iter_pycache(root: Path):
	if not root.is_dir():
		return
	for p in root.rglob("__pycache__"):
		yield p
	for p in root.rglob("*.pyc"):
		yield p


def collect_clean_targets(project_dir: Path) -> list[Path]:
	"""Paths that are safe to delete (generated / local only)."""
	project_dir = project_dir.resolve()
	targets: list[Path] = []

	for name in _WIPE_DIR_NAMES:
		# Never delete tracked Build/ tooling (Windows case-insensitive).
		if name.lower() == "build":
			continue
		p = project_dir / name
		if p.exists():
			targets.append(p)

	for pattern in _DELETE_NAME_GLOBS:
		for p in project_dir.glob(pattern):
			targets.append(p)

	for p in _iter_pycache(project_dir / "Tools"):
		targets.append(p)

	# De-dupe while preserving order
	seen: set[Path] = set()
	unique: list[Path] = []
	for t in targets:
		rp = t.resolve() if t.exists() else t
		if rp in seen:
			continue
		seen.add(rp)
		unique.append(t)
	return unique


def clean_project_tree(project_dir: Path, *, dry_run: bool = False) -> list[Path]:
	"""
	Fully remove generated/temp trees under project_dir (no README placeholders left behind).
	Does not touch Catty/Test0/Build/Tools/Doc/source.
	"""
	project_dir = project_dir.resolve()
	targets = collect_clean_targets(project_dir)
	if dry_run:
		return targets

	removed: list[Path] = []
	for t in targets:
		ok = _rm_tree(t)
		if ok or not t.exists():
			removed.append(t)
		else:
			print(f"[WARN] Still locked (skipped): {t}")

	return removed
