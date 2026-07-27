# Shared helpers for create_project.py / generateProject.py / package.py / clean.py
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


# Tools/catty_tools.py → repo root is parent of Tools/
ENGINE_ROOT = Path(__file__).resolve().parents[1]
TEMPLATE_DIR = ENGINE_ROOT / "Build" / "Templates" / "GameProject"
CPROJECT_VERSION = 1


def is_valid_project_name(name: str) -> bool:
	return bool(re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", name or ""))


def find_cmake() -> str:
	exe = shutil.which("cmake")
	if not exe:
		raise RuntimeError("cmake not found in PATH. Install CMake and restart the terminal.")
	return exe


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


def run_cmake_generate(source_dir: Path, binary_dir: Path, engine_root: Path | None = None) -> None:
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
	print("[Catty] ", " ".join(cmd))
	subprocess.check_call(cmd)


def generate_from_cproject(cproject_path: Path) -> Path:
	cproject_path = cproject_path.resolve()
	data = read_cproject(cproject_path)
	project_dir = cproject_path.parent
	project_name = str(data["ProjectName"])
	engine_root = resolve_engine_directory(cproject_path, data)
	intermediate = project_dir / "Intermediate"

	print(f"[Catty] Project : {project_name}")
	print(f"[Catty] Dir     : {project_dir}")
	print(f"[Catty] Engine  : {engine_root}")

	run_cmake_generate(project_dir, intermediate, engine_root=engine_root)
	sln = emit_sibling_sln(intermediate, project_dir, project_name)
	print(f"[Catty] Solution: {sln}")
	return sln


def generate_engine_workspace(engine_root: Path | None = None) -> Path:
	engine_root = (engine_root or ENGINE_ROOT).resolve()
	source_dir = engine_root / "Build"
	if not (source_dir / "CMakeLists.txt").is_file():
		raise FileNotFoundError(f"Engine CMake entry missing: {source_dir / 'CMakeLists.txt'}")
	intermediate = engine_root / "Intermediate"
	run_cmake_generate(source_dir, intermediate, engine_root=None)
	sln = emit_sibling_sln(intermediate, engine_root, "CattyWorkspace")
	print(f"[Catty] Workspace solution: {sln}")
	return sln


def run_package(
	project_dir: Path,
	config: str = "Release",
	platform: str = "Win64",
) -> None:
	cmake = find_cmake()
	project_dir = project_dir.resolve()
	intermediate = project_dir / "Intermediate"
	if not (intermediate / "CMakeCache.txt").is_file():
		raise RuntimeError("Project not generated yet. Run generateProject on the .cproject first.")

	packaged = project_dir / "Packaged" / platform
	if packaged.exists():
		shutil.rmtree(packaged, ignore_errors=True)
	packaged.mkdir(parents=True, exist_ok=True)

	subprocess.check_call([cmake, "--build", str(intermediate), "--config", config])
	subprocess.check_call(
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
		]
	)
	print(f"[Catty] Packaged → {packaged}")


def install_windows_cproject_association() -> None:
	if sys.platform != "win32":
		raise RuntimeError("File association is only implemented for Windows.")

	# Prefer Tools bat so Explorer double-click stays aligned with internal layout.
	generate_bat = ENGINE_ROOT / "Tools" / "generateProject.bat"
	prog_id = "Catty.CProject"
	command = f"\"{generate_bat}\" \"%1\""

	commands = [
		["reg", "add", rf"HKCU\Software\Classes\.cproject", "/ve", "/d", prog_id, "/f"],
		["reg", "add", rf"HKCU\Software\Classes\{prog_id}", "/ve", "/d", "Catty Project", "/f"],
		[
			"reg",
			"add",
			rf"HKCU\Software\Classes\{prog_id}\shell\open\command",
			"/ve",
			"/d",
			command,
			"/f",
		],
	]
	for cmd in commands:
		subprocess.check_call(cmd)
	print("[Catty] Associated .cproject → Tools/generateProject.bat (current user)")
	print(f"[Catty] Command: {command}")


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
					subprocess.call(["cmd", "/c", "rmdir", "/s", "/q", str(path)], shell=False)
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
