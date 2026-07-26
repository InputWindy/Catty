# Shared helpers for setup.py / generateProject.py / package.py
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


ENGINE_ROOT = Path(__file__).resolve().parents[2]
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
	intermediate = engine_root / "Intermediate"
	run_cmake_generate(engine_root, intermediate, engine_root=None)
	sln = emit_sibling_sln(intermediate, engine_root, "CattyWorkspace")
	print(f"[Catty] Workspace solution: {sln}")
	return sln


def run_package(project_dir: Path, config: str = "Release") -> None:
	cmake = find_cmake()
	intermediate = project_dir / "Intermediate"
	if not (intermediate / "CMakeCache.txt").is_file():
		raise RuntimeError("Project not generated yet. Run generateProject on the .cproject first.")
	subprocess.check_call([cmake, "--build", str(intermediate), "--config", config])
	subprocess.check_call(
		[cmake, "--build", str(intermediate), "--config", config, "--target", "Package"]
	)
	print(f"[Catty] Packaged → {project_dir / 'Packaged'}")


def install_windows_cproject_association() -> None:
	if sys.platform != "win32":
		raise RuntimeError("File association is only implemented for Windows.")

	generate_py = ENGINE_ROOT / "generateProject.py"
	python = Path(sys.executable).resolve()
	prog_id = "Catty.CProject"
	command = f"\"{python}\" \"{generate_py}\" \"%1\""

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
	print("[Catty] Associated .cproject → generateProject.py (current user)")
	print(f"[Catty] Command: {command}")
