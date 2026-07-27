#!/usr/bin/env python3
"""
Catty reflect codegen — scan CATTY_REFLECT_* annotations and emit catalogs.

Usage:
  Tools\\catty_python.bat Tools\\reflect_codegen.py
  Tools\\reflect_codegen.bat
  Tools\\reflect_codegen.bat --check

Does NOT replace refl-cpp compile-time metadata. This tool builds a
repo-wide inventory for tooling / docs / future Lua export glue.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
ENGINE_ROOT = TOOLS_DIR.parent

_DEFAULT_ROOTS = [ENGINE_ROOT / "Catty" / "Source"]
_DEFAULT_OUT_H = ENGINE_ROOT / "Catty" / "Source" / "Public" / "Catty" / "Core" / "ReflectCatalog.gen.h"
_DEFAULT_OUT_JSON = ENGINE_ROOT / "Catty" / "Source" / "Generated" / "ReflectCatalog.gen.json"
_DEFAULT_OUT_MD = ENGINE_ROOT / "Doc" / "Engine" / "ReflectCatalog.md"

_SOURCE_SUFFIXES = {".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".c", ".cc", ".cpp", ".cxx"}

# Skip generated / third-party noise under a scan root.
_SKIP_DIR_NAMES = {".git", "Intermediate", "Binaries", "ThirdParty", "_deps", "Generated"}


@dataclass
class FMember:
	Name: str
	Kind: str  # "property" | "function"
	Attrs: str = ""


@dataclass
class FTypeEntry:
	TypeName: str
	Members: list[FMember] = field(default_factory=list)
	Attrs: str = ""
	SourceRel: str = ""
	SourceLine: int = 0


def _strip_comments(text: str) -> str:
	"""Remove // and /* */ comments without a full C++ lexer."""
	out: list[str] = []
	i = 0
	n = len(text)
	while i < n:
		c = text[i]
		nxt = text[i + 1] if i + 1 < n else ""
		if c == '"' or c == "'":
			quote = c
			out.append(c)
			i += 1
			while i < n:
				ch = text[i]
				out.append(ch)
				if ch == "\\" and i + 1 < n:
					out.append(text[i + 1])
					i += 2
					continue
				if ch == quote:
					i += 1
					break
				i += 1
			continue
		if c == "/" and nxt == "/":
			i += 2
			while i < n and text[i] not in "\r\n":
				i += 1
			continue
		if c == "/" and nxt == "*":
			i += 2
			while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
				# Keep newlines so line numbers stay usable for BEGIN matches on raw text.
				if text[i] in "\r\n":
					out.append(text[i])
				i += 1
			i = min(i + 2, n)
			continue
		out.append(c)
		i += 1
	return "".join(out)


def _find_matching(text: str, open_idx: int, open_ch: str, close_ch: str) -> int:
	depth = 0
	i = open_idx
	n = len(text)
	while i < n:
		ch = text[i]
		if ch == open_ch:
			depth += 1
		elif ch == close_ch:
			depth -= 1
			if depth == 0:
				return i
		i += 1
	return -1


def _split_top_level_args(arg_text: str) -> list[str]:
	args: list[str] = []
	buf: list[str] = []
	angle = paren = bracket = 0
	i = 0
	n = len(arg_text)
	while i < n:
		ch = arg_text[i]
		if ch == "<":
			angle += 1
		elif ch == ">" and angle > 0:
			angle -= 1
		elif ch == "(":
			paren += 1
		elif ch == ")" and paren > 0:
			paren -= 1
		elif ch == "[":
			bracket += 1
		elif ch == "]" and bracket > 0:
			bracket -= 1
		elif ch == "," and angle == 0 and paren == 0 and bracket == 0:
			part = "".join(buf).strip()
			if part:
				args.append(part)
			buf = []
			i += 1
			continue
		buf.append(ch)
		i += 1
	part = "".join(buf).strip()
	if part:
		args.append(part)
	return args


def _line_number(text: str, index: int) -> int:
	return text.count("\n", 0, index) + 1


def _is_preprocessor_define(text: str, match_start: int) -> bool:
	"""Skip `#define CATTY_REFLECT_*` lines (macro definitions, not usages)."""
	line_start = text.rfind("\n", 0, match_start) + 1
	prefix = text[line_start:match_start].lstrip()
	return prefix.startswith("#")


_RE_BEGIN = re.compile(r"\bCATTY_REFLECT_BEGIN\s*\(")
_RE_AUTO = re.compile(r"\bCATTY_REFLECT_AUTO\s*\(")
_RE_PROP = re.compile(r"\bCATTY_REFLECT_PROPERTY\s*\(")
_RE_FUNC = re.compile(r"\bCATTY_REFLECT_FUNCTION\s*\(")
_RE_AUTO_TYPE = re.compile(r"\bCATTY_REFLECT_TYPE\s*\(")
_RE_AUTO_FIELD = re.compile(r"\bCATTY_REFLECT_FIELD\s*\(")
_RE_AUTO_FUNC = re.compile(r"\bCATTY_REFLECT_FUNC\s*\(")
_RE_END = re.compile(r"\bCATTY_REFLECT_END\b")


def _parse_macro_call(text: str, match: re.Match[str]) -> tuple[str, int]:
	"""Return (args_inside, index_after_closing_paren)."""
	open_paren = text.find("(", match.start())
	close = _find_matching(text, open_paren, "(", ")")
	if close < 0:
		raise ValueError(f"Unclosed macro at index {match.start()}")
	return text[open_paren + 1 : close], close + 1


def _parse_begin_block(text: str, begin_match: re.Match[str]) -> FTypeEntry:
	args_text, after_begin = _parse_macro_call(text, begin_match)
	args = _split_top_level_args(args_text)
	if not args:
		raise ValueError("CATTY_REFLECT_BEGIN missing type name")
	type_name = args[0]
	attrs = ", ".join(args[1:]) if len(args) > 1 else ""

	end_match = _RE_END.search(text, after_begin)
	if not end_match:
		raise ValueError(f"CATTY_REFLECT_END missing for {type_name}")
	body = text[after_begin : end_match.start()]

	members: list[FMember] = []
	for m in _RE_PROP.finditer(body):
		prop_args, _ = _parse_macro_call(body, m)
		parts = _split_top_level_args(prop_args)
		if not parts:
			continue
		members.append(FMember(Name=parts[0], Kind="property", Attrs=", ".join(parts[1:])))
	for m in _RE_FUNC.finditer(body):
		func_args, _ = _parse_macro_call(body, m)
		parts = _split_top_level_args(func_args)
		if not parts:
			continue
		members.append(FMember(Name=parts[0], Kind="function", Attrs=", ".join(parts[1:])))

	return FTypeEntry(
		TypeName=type_name,
		Members=members,
		Attrs=attrs,
		SourceLine=_line_number(text, begin_match.start()),
	)


def _parse_auto_block(text: str, auto_match: re.Match[str]) -> FTypeEntry:
	args_text, _ = _parse_macro_call(text, auto_match)
	type_name = ""
	type_attrs = ""
	members: list[FMember] = []

	for m in _RE_AUTO_TYPE.finditer(args_text):
		t_args, _ = _parse_macro_call(args_text, m)
		parts = _split_top_level_args(t_args)
		if not parts:
			continue
		type_name = parts[0]
		type_attrs = ", ".join(parts[1:])
		break
	if not type_name:
		raise ValueError("CATTY_REFLECT_AUTO missing CATTY_REFLECT_TYPE(...)")

	for m in _RE_AUTO_FIELD.finditer(args_text):
		f_args, _ = _parse_macro_call(args_text, m)
		parts = _split_top_level_args(f_args)
		if parts:
			members.append(FMember(Name=parts[0], Kind="property", Attrs=", ".join(parts[1:])))
	for m in _RE_AUTO_FUNC.finditer(args_text):
		f_args, _ = _parse_macro_call(args_text, m)
		parts = _split_top_level_args(f_args)
		if parts:
			members.append(FMember(Name=parts[0], Kind="function", Attrs=", ".join(parts[1:])))

	return FTypeEntry(
		TypeName=type_name,
		Members=members,
		Attrs=type_attrs,
		SourceLine=_line_number(text, auto_match.start()),
	)


def scan_file(path: Path, repo_root: Path) -> list[FTypeEntry]:
	raw = path.read_text(encoding="utf-8")
	text = _strip_comments(raw)
	entries: list[FTypeEntry] = []
	try:
		rel = str(path.resolve().relative_to(repo_root.resolve())).replace("\\", "/")
	except ValueError:
		rel = str(path).replace("\\", "/")

	for m in _RE_BEGIN.finditer(text):
		if _is_preprocessor_define(text, m.start()):
			continue
		entry = _parse_begin_block(text, m)
		entry.SourceRel = rel
		entries.append(entry)
	for m in _RE_AUTO.finditer(text):
		if _is_preprocessor_define(text, m.start()):
			continue
		entry = _parse_auto_block(text, m)
		entry.SourceRel = rel
		entries.append(entry)
	return entries


def iter_source_files(roots: list[Path]) -> list[Path]:
	files: list[Path] = []
	seen: set[Path] = set()
	for root in roots:
		root = root.resolve()
		if root.is_file():
			if root.suffix.lower() in _SOURCE_SUFFIXES:
				files.append(root)
			continue
		if not root.is_dir():
			continue
		for path in root.rglob("*"):
			if not path.is_file():
				continue
			if path.suffix.lower() not in _SOURCE_SUFFIXES:
				continue
			if any(part in _SKIP_DIR_NAMES for part in path.parts):
				continue
			# Do not re-scan generated catalogs.
			if path.name.endswith(".gen.h") or path.name.endswith(".gen.cpp") or path.name.endswith(".gen.json"):
				continue
			rp = path.resolve()
			if rp in seen:
				continue
			seen.add(rp)
			files.append(rp)
	files.sort(key=lambda p: str(p).lower())
	return files


def scan_roots(roots: list[Path], repo_root: Path) -> list[FTypeEntry]:
	entries: list[FTypeEntry] = []
	for path in iter_source_files(roots):
		try:
			entries.extend(scan_file(path, repo_root))
		except ValueError as ex:
			raise ValueError(f"{path}: {ex}") from ex
	# Stable order: type name then source.
	entries.sort(key=lambda e: (e.TypeName.lower(), e.SourceRel, e.SourceLine))
	return entries


def _cpp_string(s: str) -> str:
	return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _safe_ident(type_name: str) -> str:
	ident = re.sub(r"[^0-9A-Za-z_]", "_", type_name)
	if ident and ident[0].isdigit():
		ident = "_" + ident
	return ident or "Anonymous"


def render_header(entries: list[FTypeEntry]) -> str:
	lines: list[str] = [
		"//*****************************************************************************",
		"// ReflectCatalog.gen.h — GENERATED by Tools/reflect_codegen.py. DO NOT EDIT.",
		"//*****************************************************************************",
		"#pragma once",
		"",
		"#include <cstddef>",
		"",
		"namespace Catty",
		"{",
		"namespace ReflectCatalog",
		"{",
		"",
		"enum class EReflectMemberKind : unsigned",
		"{",
		"\tProperty = 0,",
		"\tFunction = 1,",
		"};",
		"",
		"struct FReflectMemberInfo",
		"{",
		"\tconst char* Name;",
		"\tEReflectMemberKind Kind;",
		"};",
		"",
		"struct FReflectTypeInfo",
		"{",
		"\tconst char* TypeName;",
		"\tconst char* SourceFile;",
		"\tint SourceLine;",
		"\tconst FReflectMemberInfo* Members;",
		"\tstd::size_t MemberCount;",
		"};",
		"",
	]

	for entry in entries:
		ident = _safe_ident(entry.TypeName)
		if not entry.Members:
			continue
		lines.append(f"inline constexpr FReflectMemberInfo GMembers_{ident}[] =")
		lines.append("{")
		for mem in entry.Members:
			kind = "EReflectMemberKind::Property" if mem.Kind == "property" else "EReflectMemberKind::Function"
			lines.append(f"\t{{ {_cpp_string(mem.Name)}, {kind} }},")
		lines.append("};")
		lines.append("")

	lines.append("inline constexpr FReflectTypeInfo GTypes[] =")
	lines.append("{")
	if not entries:
		lines.append("\t// (no CATTY_REFLECT_* types found)")
	for entry in entries:
		ident = _safe_ident(entry.TypeName)
		count = len(entry.Members)
		member_expr = f"GMembers_{ident}" if count > 0 else "nullptr"
		lines.append("\t{")
		lines.append(f"\t\t{_cpp_string(entry.TypeName)},")
		lines.append(f"\t\t{_cpp_string(entry.SourceRel)},")
		lines.append(f"\t\t{entry.SourceLine},")
		lines.append(f"\t\t{member_expr},")
		lines.append(f"\t\t{count}")
		lines.append("\t},")
	lines.append("};")
	lines.append("")
	# Avoid divide-by-zero when GTypes is empty: use explicit count.
	lines.append(f"inline constexpr std::size_t GTypeCount = {len(entries)};")
	lines.append("")
	lines.append("} // namespace ReflectCatalog")
	lines.append("} // namespace Catty")
	lines.append("")
	return "\n".join(lines)


def render_json(entries: list[FTypeEntry]) -> str:
	payload = {
		"generator": "Tools/reflect_codegen.py",
		"typeCount": len(entries),
		"types": [asdict(e) for e in entries],
	}
	return json.dumps(payload, indent=2, ensure_ascii=False) + "\n"


def render_markdown(entries: list[FTypeEntry]) -> str:
	lines = [
		"# Catty Reflect Catalog",
		"",
		"> Generated by `Tools/reflect_codegen.py`. Do not edit by hand.",
		"",
		f"**Types:** {len(entries)}",
		"",
	]
	if not entries:
		lines.append("_No `CATTY_REFLECT_*` annotations found._")
		lines.append("")
		return "\n".join(lines)

	lines.append("| Type | Properties | Functions | Source |")
	lines.append("|------|------------|-----------|--------|")
	for e in entries:
		props = ", ".join(m.Name for m in e.Members if m.Kind == "property") or "—"
		funcs = ", ".join(m.Name for m in e.Members if m.Kind == "function") or "—"
		src = f"`{e.SourceRel}:{e.SourceLine}`"
		lines.append(f"| `{e.TypeName}` | {props} | {funcs} | {src} |")
	lines.append("")
	return "\n".join(lines)


def write_if_changed(path: Path, content: str) -> bool:
	path.parent.mkdir(parents=True, exist_ok=True)
	if path.is_file():
		old = path.read_text(encoding="utf-8")
		if old == content:
			return False
	path.write_text(content, encoding="utf-8", newline="\n")
	return True


def run_check(path: Path, content: str) -> bool:
	if not path.is_file():
		print(f"[CHECK] missing: {path}")
		return False
	old = path.read_text(encoding="utf-8")
	if old != content:
		print(f"[CHECK] stale: {path}")
		return False
	print(f"[CHECK] ok: {path}")
	return True


def main(argv: list[str]) -> int:
	parser = argparse.ArgumentParser(description="Catty CATTY_REFLECT_* scanner / codegen")
	parser.add_argument(
		"--root",
		action="append",
		dest="roots",
		default=None,
		help="Source root to scan (repeatable). Default: Catty/Source",
	)
	parser.add_argument("--out-h", type=Path, default=_DEFAULT_OUT_H, help="Output ReflectCatalog.gen.h")
	parser.add_argument("--out-json", type=Path, default=_DEFAULT_OUT_JSON, help="Output ReflectCatalog.gen.json")
	parser.add_argument("--out-md", type=Path, default=_DEFAULT_OUT_MD, help="Output ReflectCatalog.md")
	parser.add_argument("--no-md", action="store_true", help="Skip markdown catalog")
	parser.add_argument("--check", action="store_true", help="Verify outputs are up to date (no write)")
	parser.add_argument("--repo-root", type=Path, default=ENGINE_ROOT, help="Repo root for relative paths")
	args = parser.parse_args(argv)

	roots = [Path(r) for r in args.roots] if args.roots else list(_DEFAULT_ROOTS)
	roots = [r if r.is_absolute() else (args.repo_root / r) for r in roots]

	try:
		entries = scan_roots(roots, args.repo_root)
	except ValueError as ex:
		print(f"[ERROR] {ex}", file=sys.stderr)
		return 1

	header = render_header(entries)
	js = render_json(entries)
	md = render_markdown(entries)

	print(f"[Catty] reflect scan: {len(entries)} type(s) from {len(roots)} root(s)")

	if args.check:
		ok = True
		ok = run_check(args.out_h, header) and ok
		ok = run_check(args.out_json, js) and ok
		if not args.no_md:
			ok = run_check(args.out_md, md) and ok
		return 0 if ok else 2

	changed = False
	if write_if_changed(args.out_h, header):
		print(f"[Catty] wrote {args.out_h}")
		changed = True
	else:
		print(f"[Catty] up-to-date {args.out_h}")
	if write_if_changed(args.out_json, js):
		print(f"[Catty] wrote {args.out_json}")
		changed = True
	else:
		print(f"[Catty] up-to-date {args.out_json}")
	if not args.no_md:
		if write_if_changed(args.out_md, md):
			print(f"[Catty] wrote {args.out_md}")
			changed = True
		else:
			print(f"[Catty] up-to-date {args.out_md}")

	if not changed:
		print("[Catty] reflect codegen: nothing changed")
	return 0


if __name__ == "__main__":
	raise SystemExit(main(sys.argv[1:]))
