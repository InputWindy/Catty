# Run via Tools/reflect_codegen.bat / catty_python.bat — engine Tools/python only.
"""
Catty reflect codegen — scan CATTY_REFLECT_CLASS / CATTY_REFLECT_* and emit:

  - ReflectCatalog (C++ / JSON / Markdown inventory)
  - Catty/Source/Generated/* (ReflectCatalog / ReflectMeta / LuaBindings — wiped by clean.bat)
  - Doc/Engine/LuaAPI.html Reflect usertype regions (TOC / body)

CATTY_REFLECT_CLASS() sits on the line above class/struct (UE-style). Codegen
parses the type body and exports every public field and public method.
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
sys.path.insert(0, str(TOOLS_DIR))

# Enforces Tools/python via ensure_engine_python() on import.
import catty_tools  # noqa: E402,F401

_DEFAULT_ROOTS = [ENGINE_ROOT / "Catty" / "Source"]
_DEFAULT_GEN_DIR = ENGINE_ROOT / "Catty" / "Source" / "Generated"
_DEFAULT_OUT_H = _DEFAULT_GEN_DIR / "ReflectCatalog.gen.h"
_DEFAULT_OUT_JSON = _DEFAULT_GEN_DIR / "ReflectCatalog.gen.json"
_DEFAULT_OUT_MD = ENGINE_ROOT / "Doc" / "Engine" / "ReflectCatalog.md"
_DEFAULT_OUT_LUA_H = _DEFAULT_GEN_DIR / "LuaBindings.gen.h"
_DEFAULT_OUT_LUA_CPP = _DEFAULT_GEN_DIR / "LuaBindings.gen.cpp"
_DEFAULT_OUT_META_H = _DEFAULT_GEN_DIR / "ReflectMeta.gen.h"
_DEFAULT_OUT_LUA_API = ENGINE_ROOT / "Doc" / "Engine" / "LuaAPI.html"

_SOURCE_SUFFIXES = {".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".c", ".cc", ".cpp", ".cxx"}
_HEADER_SUFFIXES = {".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp"}

# Skip generated / third-party noise under a scan root.
_SKIP_DIR_NAMES = {".git", "Intermediate", "Binaries", "ThirdParty", "_deps", "Generated"}

_RE_LUA_SKIP = re.compile(r"\b(?:CATTY_LUA_SKIP|FLuaSkip|LuaSkip)\b")
_RE_LUA_NAME = re.compile(
	r"(?:CATTY_LUA_NAME|FLuaName|LuaName)\s*(?:\{|\()\s*\"([^\"]*)\""
)
# Members whose signatures mention these types are kept in ReflectMeta but skipped for sol2.
_RE_LUA_HARD_TYPE = re.compile(
	r"\b("
	r"FObjectRef|FObjectWeakRef|FObject\b|FPackage\b|FResource\b|"
	r"FJsonValue|FResourceId|FGCManager|"
	r"std\s*::\s*vector|vector\s*<"
	r")"
)


def _is_lua_friendly_decl(decl_head: str) -> bool:
	"""True if a member signature looks bindable by sol2 without extra converters."""
	if not decl_head:
		return True
	return _RE_LUA_HARD_TYPE.search(decl_head) is None


@dataclass
class FMember:
	Name: str
	Kind: str  # "property" | "function"
	Attrs: str = ""
	LuaName: str = ""
	bLuaExport: bool = True


@dataclass
class FTypeEntry:
	TypeName: str
	Members: list[FMember] = field(default_factory=list)
	Attrs: str = ""
	SourceRel: str = ""
	SourceLine: int = 0
	LuaName: str = ""
	bLuaExport: bool = True
	IncludePath: str = ""  # e.g. "Catty/Math/Point.h" or "Core/ReflectSmoke.h"
	# "class" = CATTY_REFLECT_CLASS (emit ReflectMeta.gen.h); "manual" = BEGIN/AUTO in source
	SourceKind: str = "manual"
	bEmitReflMeta: bool = False
	Bases: list[str] = field(default_factory=list)


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


def _parse_lua_attrs(attrs: str) -> tuple[bool, str]:
	"""Return (bLuaExport, LuaNameOverride). Default: export, empty name."""
	if not attrs:
		return True, ""
	if _RE_LUA_SKIP.search(attrs):
		return False, ""
	m = _RE_LUA_NAME.search(attrs)
	name = m.group(1) if m else ""
	return True, name


def _short_type_name(qualified: str) -> str:
	return qualified.rsplit("::", 1)[-1]


# UE type prefixes (FObject / UClass / AActor / EEnum / TArray / SWidget / IInterface).
_RE_UE_TYPE_PREFIX = re.compile(r"^[FUATESI](?=[A-Z])")
# UE bool member prefix (bInitialized).
_RE_UE_BOOL_PREFIX = re.compile(r"^b(?=[A-Z])")


def _pascal_to_snake(name: str) -> str:
	"""PascalCase / camelCase → snake_case (XMLHttp → xml_http)."""
	if not name:
		return name
	s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name)
	s = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", s)
	return s.lower()


def _cpp_ident_to_lua(name: str, *, b_is_type: bool) -> str:
	"""
	Map C++ / UE identifiers to idiomatic Lua names.
	Types: FObject → object, FResourceManager → resource_manager
	Members: GetName → get_name, bIsReady → is_ready
	"""
	ident = name.strip()
	if not ident:
		return ident
	if b_is_type:
		ident = _RE_UE_TYPE_PREFIX.sub("", ident)
	else:
		ident = _RE_UE_BOOL_PREFIX.sub("", ident)
	return _pascal_to_snake(ident)


def _apply_lua_fields(entry: FTypeEntry) -> None:
	b_export, lua_name = _parse_lua_attrs(entry.Attrs)
	entry.bLuaExport = b_export
	entry.LuaName = lua_name or _cpp_ident_to_lua(_short_type_name(entry.TypeName), b_is_type=True)
	for mem in entry.Members:
		m_export, m_name = _parse_lua_attrs(mem.Attrs)
		mem.bLuaExport = m_export
		mem.LuaName = m_name or _cpp_ident_to_lua(mem.Name, b_is_type=False)

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

	entry = FTypeEntry(
		TypeName=type_name,
		Members=members,
		Attrs=attrs,
		SourceLine=_line_number(text, begin_match.start()),
	)
	_apply_lua_fields(entry)
	return entry


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

	entry = FTypeEntry(
		TypeName=type_name,
		Members=members,
		Attrs=type_attrs,
		SourceLine=_line_number(text, auto_match.start()),
	)
	_apply_lua_fields(entry)
	return entry


def resolve_include_path(source_rel: str, repo_root: Path) -> str:
	"""
	Map annotation SourceRel to a #include path usable from Catty Private/Public.
	Returns empty string if Lua binding cannot include the type (e.g. .cpp-only, no header).
	"""
	rel = source_rel.replace("\\", "/")
	abs_path = (repo_root / rel).resolve()
	if not abs_path.is_file():
		# Still try prefix stripping for offline / missing trees.
		header_rel = rel
	else:
		header_path = abs_path
		if header_path.suffix.lower() not in _HEADER_SUFFIXES:
			found = None
			for ext in (".h", ".hpp", ".hxx", ".hh"):
				cand = header_path.with_suffix(ext)
				if cand.is_file():
					found = cand
					break
			if found is None:
				return ""
			header_path = found
		try:
			header_rel = str(header_path.relative_to(repo_root.resolve())).replace("\\", "/")
		except ValueError:
			header_rel = rel

	for prefix in (
		"Catty/Source/Public/",
		"Catty/Source/Private/",
		"Source/Public/",
		"Source/Private/",
	):
		if header_rel.startswith(prefix):
			return header_rel[len(prefix) :]
	return ""


def scan_file(path: Path, repo_root: Path) -> list[FTypeEntry]:
	from reflect_class_scan import scan_reflect_classes  # noqa: WPS433

	raw = path.read_text(encoding="utf-8")
	text = _strip_comments(raw)
	entries: list[FTypeEntry] = []
	try:
		rel = str(path.resolve().relative_to(repo_root.resolve())).replace("\\", "/")
	except ValueError:
		rel = str(path).replace("\\", "/")

	# Preferred: CATTY_REFLECT_CLASS() above type (auto public export).
	for scanned in scan_reflect_classes(text):
		members = [FMember(Name=m.Name, Kind=m.Kind) for m in scanned.Members]
		entry = FTypeEntry(
			TypeName=scanned.TypeName,
			Members=members,
			Attrs=scanned.Attrs,
			SourceLine=scanned.SourceLine,
			SourceRel=rel,
			IncludePath=resolve_include_path(rel, repo_root),
			SourceKind="class",
			bEmitReflMeta=True,
			Bases=list(scanned.Bases),
		)
		_apply_lua_fields(entry)
		# Keep ReflectMeta complete; drop sol2-unfriendly signatures from Lua only.
		for mem, scanned_mem in zip(entry.Members, scanned.Members):
			if mem.bLuaExport and scanned_mem.Kind == "function" and not _is_lua_friendly_decl(scanned_mem.DeclHead):
				mem.bLuaExport = False
		entries.append(entry)

	class_names = {e.TypeName for e in entries}

	# Legacy / selective: CATTY_REFLECT_BEGIN … END / AUTO (skipped if CLASS already covers type).
	for m in _RE_BEGIN.finditer(text):
		if _is_preprocessor_define(text, m.start()):
			continue
		entry = _parse_begin_block(text, m)
		if entry.TypeName in class_names:
			continue
		entry.SourceRel = rel
		entry.IncludePath = resolve_include_path(rel, repo_root)
		entry.SourceKind = "manual"
		entry.bEmitReflMeta = False
		entries.append(entry)
	for m in _RE_AUTO.finditer(text):
		if _is_preprocessor_define(text, m.start()):
			continue
		entry = _parse_auto_block(text, m)
		if entry.TypeName in class_names:
			continue
		entry.SourceRel = rel
		entry.IncludePath = resolve_include_path(rel, repo_root)
		entry.SourceKind = "manual"
		entry.bEmitReflMeta = False
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
			# Do not re-scan generated catalogs / bindings.
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
		"\tbool bLuaExport;",
		"\tconst char* LuaName;",
		"};",
		"",
		"struct FReflectTypeInfo",
		"{",
		"\tconst char* TypeName;",
		"\tconst char* SourceFile;",
		"\tint SourceLine;",
		"\tconst FReflectMemberInfo* Members;",
		"\tstd::size_t MemberCount;",
		"\tbool bLuaExport;",
		"\tconst char* LuaName;",
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
			lua_flag = "true" if mem.bLuaExport else "false"
			lines.append(
				f"\t{{ {_cpp_string(mem.Name)}, {kind}, {lua_flag}, {_cpp_string(mem.LuaName)} }},"
			)
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
		lua_flag = "true" if entry.bLuaExport else "false"
		lines.append("\t{")
		lines.append(f"\t\t{_cpp_string(entry.TypeName)},")
		lines.append(f"\t\t{_cpp_string(entry.SourceRel)},")
		lines.append(f"\t\t{entry.SourceLine},")
		lines.append(f"\t\t{member_expr},")
		lines.append(f"\t\t{count},")
		lines.append(f"\t\t{lua_flag},")
		lines.append(f"\t\t{_cpp_string(entry.LuaName)}")
		lines.append("\t},")
	lines.append("};")
	lines.append("")
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

	lines.append("| Type | Lua | Properties | Functions | Source |")
	lines.append("|------|-----|------------|-----------|--------|")
	for e in entries:
		props = ", ".join(m.Name for m in e.Members if m.Kind == "property") or "—"
		funcs = ", ".join(m.Name for m in e.Members if m.Kind == "function") or "—"
		src = f"`{e.SourceRel}:{e.SourceLine}`"
		if e.bLuaExport and e.IncludePath:
			lua = f"`catty.{e.LuaName}`"
		elif e.bLuaExport:
			lua = "skip (no header include)"
		else:
			lua = "—"
		lines.append(f"| `{e.TypeName}` | {lua} | {props} | {funcs} | {src} |")
	lines.append("")
	return "\n".join(lines)


def _lua_export_entries(entries: list[FTypeEntry]) -> list[FTypeEntry]:
	"""Types that should get sol2 usertypes."""
	out: list[FTypeEntry] = []
	for e in entries:
		if not e.bLuaExport:
			continue
		if not e.IncludePath:
			print(f"[WARN] Lua skip {e.TypeName}: no includable header for {e.SourceRel}")
			continue
		exported_members = [m for m in e.Members if m.bLuaExport]
		if not exported_members:
			print(f"[WARN] Lua skip {e.TypeName}: no exportable members")
			continue
		# Shallow copy with filtered members for rendering.
		out.append(
			FTypeEntry(
				TypeName=e.TypeName,
				Members=exported_members,
				Attrs=e.Attrs,
				SourceRel=e.SourceRel,
				SourceLine=e.SourceLine,
				LuaName=e.LuaName,
				bLuaExport=True,
				IncludePath=e.IncludePath,
				Bases=list(e.Bases),
			)
		)
	return out


def render_lua_header() -> str:
	return "\n".join(
		[
			"//*****************************************************************************",
			"// LuaBindings.gen.h — GENERATED by Tools/reflect_codegen.py. DO NOT EDIT.",
			"//*****************************************************************************",
			"#pragma once",
			"",
			"#define SOL_ALL_SAFETIES_ON 1",
			"#include <sol/sol.hpp>",
			"",
			"namespace Catty",
			"{",
			"",
			"/** Register CATTY_REFLECT_* types as sol2 usertypes on the `catty` table. */",
			"void RegisterGeneratedLuaBindings(sol::state& Lua);",
			"",
			"} // namespace Catty",
			"",
		]
	)


def render_lua_cpp(entries: list[FTypeEntry]) -> str:
	lua_types = _lua_export_entries(entries)
	includes: list[str] = []
	seen_inc: set[str] = set()
	for e in lua_types:
		inc = e.IncludePath.replace("\\", "/")
		if inc not in seen_inc:
			seen_inc.add(inc)
			includes.append(inc)

	lines: list[str] = [
		"//*****************************************************************************",
		"// LuaBindings.gen.cpp — GENERATED by Tools/reflect_codegen.py. DO NOT EDIT.",
		"//*****************************************************************************",
		"",
		'#include "LuaBindings.gen.h"',
		"",
		"#define SOL_ALL_SAFETIES_ON 1",
		"#include <sol/sol.hpp>",
		"",
	]
	for inc in includes:
		lines.append(f'#include "{inc}"')
	if includes:
		lines.append("")

	lines.extend(
		[
			"namespace Catty",
			"{",
			"",
			"void RegisterGeneratedLuaBindings(sol::state& Lua)",
			"{",
			"\tsol::object CattyObj = Lua[\"catty\"];",
			"\tsol::table CattyTable;",
			"\tif (CattyObj.valid() && CattyObj.is<sol::table>())",
			"\t{",
			"\t\tCattyTable = CattyObj;",
			"\t}",
			"\telse",
			"\t{",
			"\t\tCattyTable = Lua.create_named_table(\"catty\");",
			"\t}",
			"",
		]
	)

	if not lua_types:
		lines.append("\t(void)CattyTable;")
		lines.append("\t// No CATTY_REFLECT_* types marked for Lua export.")
	else:
		for e in lua_types:
			# sol2 requires key/value pairs after the type name. Prefer
			# call_constructor+no_constructor and base_classes+bases<> — a bare
			# sol::no_constructor / sol::bases<>() breaks the even-arg static_assert.
			arg_chunks: list[str] = [
				"sol::call_constructor, sol::no_constructor",
			]
			if e.Bases:
				bases_csv = ", ".join(e.Bases)
				arg_chunks.append(f"sol::base_classes, sol::bases<{bases_csv}>()")
			for mem in e.Members:
				arg_chunks.append(f"{_cpp_string(mem.LuaName)}, &{e.TypeName}::{mem.Name}")

			lines.append(f"\t// {e.TypeName} @ {e.SourceRel}:{e.SourceLine}")
			lines.append(f"\tCattyTable.new_usertype<{e.TypeName}>(")
			lines.append(f"\t\t{_cpp_string(e.LuaName)},")
			for i, chunk in enumerate(arg_chunks):
				comma = "," if i + 1 < len(arg_chunks) else ""
				lines.append(f"\t\t{chunk}{comma}")
			lines.append("\t);")
			lines.append("")

	lines.extend(
		[
			"}",
			"",
			"} // namespace Catty",
			"",
		]
	)
	return "\n".join(lines)


def render_meta_header(entries: list[FTypeEntry]) -> str:
	"""
	Emit refl-cpp REFL_* blocks for CATTY_REFLECT_CLASS types as a header.
	Must be a header so is_reflectable_v / ReflectType work in other TUs.
	Manual BEGIN/END types already register in their own TUs — skipped here.
	"""
	meta_types = [e for e in entries if e.bEmitReflMeta]
	includes: list[str] = []
	seen: set[str] = set()
	for e in meta_types:
		if not e.IncludePath:
			print(f"[WARN] ReflectMeta skip {e.TypeName}: no includable header")
			continue
		inc = e.IncludePath.replace("\\", "/")
		if inc not in seen:
			seen.add(inc)
			includes.append(inc)

	lines: list[str] = [
		"//*****************************************************************************",
		"// ReflectMeta.gen.h — GENERATED by Tools/reflect_codegen.py. DO NOT EDIT.",
		"// refl-cpp metadata for CATTY_REFLECT_CLASS() types (public members).",
		"//*****************************************************************************",
		"#pragma once",
		"",
		'#include "Catty/Core/Reflect.h"',
		"",
	]
	for inc in includes:
		lines.append(f'#include "{inc}"')
	if includes:
		lines.append("")

	if not meta_types:
		lines.append("// No CATTY_REFLECT_CLASS types found.")
		lines.append("")
		return "\n".join(lines)

	for e in meta_types:
		if not e.IncludePath:
			continue
		lines.append(f"// {e.TypeName} @ {e.SourceRel}:{e.SourceLine}")
		# Merge user attrs with discovered bases<> for refl-cpp.
		attr_parts: list[str] = []
		if e.Bases:
			attr_parts.append(f"bases<{', '.join(e.Bases)}>")
		if e.Attrs.strip():
			# Keep non-Lua attrs only roughly — full string may include CATTY_LUA_SKIP which is valid ReflectAttr.
			attr_parts.append(e.Attrs.strip())
		if attr_parts:
			lines.append(f"CATTY_REFLECT_BEGIN({e.TypeName}, {', '.join(attr_parts)})")
		else:
			lines.append(f"CATTY_REFLECT_BEGIN({e.TypeName})")
		for mem in e.Members:
			if mem.Kind == "property":
				lines.append(f"\tCATTY_REFLECT_PROPERTY({mem.Name})")
			else:
				lines.append(f"\tCATTY_REFLECT_FUNCTION({mem.Name})")
		lines.append("CATTY_REFLECT_END")
		lines.append("")

	return "\n".join(lines)


def _html_escape(s: str) -> str:
	return (
		s.replace("&", "&amp;")
		.replace("<", "&lt;")
		.replace(">", "&gt;")
		.replace('"', "&quot;")
	)


def _usertype_anchor(lua_name: str) -> str:
	"""HTML id for catty.<LuaName> (safe for fragments)."""
	safe = re.sub(r"[^0-9A-Za-z_.\-]", "_", lua_name)
	return f"catty.{safe}"


def render_lua_api_toc(lua_types: list[FTypeEntry]) -> str:
	lines = [
		'\t<div class="sec">Usertypes</div>',
		"\t<ul>",
	]
	if not lua_types:
		lines.append('\t\t<li><span style="color:#a9b4c0;padding:5px 8px;display:block;">（无导出类型）</span></li>')
	else:
		for e in lua_types:
			anchor = _usertype_anchor(e.LuaName)
			lines.append(
				f'\t\t<li><a href="#{_html_escape(anchor)}">{_html_escape(e.LuaName)}</a></li>'
			)
	lines.append("\t</ul>")
	return "\n".join(lines)


def render_lua_api_body(lua_types: list[FTypeEntry]) -> str:
	lines: list[str] = [
		'<section class="card" id="usertypes">',
		"\t<h2>Usertypes</h2>",
		'\t<div class="body">',
	]
	if not lua_types:
		lines.extend(
			[
				'\t\t<div class="note">当前没有 <code>CATTY_REFLECT_*</code> Lua 导出类型。</div>',
			]
		)
	else:
		for e in lua_types:
			anchor = _usertype_anchor(e.LuaName)
			props = [m for m in e.Members if m.Kind == "property"]
			funcs = [m for m in e.Members if m.Kind == "function"]

			lines.extend(
				[
					f'\t\t<article class="fn" id="{_html_escape(anchor)}">',
					'\t\t\t<div class="fn-head">',
					f'\t\t\t\t<h3 class="fn-name">catty.{_html_escape(e.LuaName)}</h3>',
					'\t\t\t\t<span class="tag tag-reflect">usertype</span>',
					"\t\t\t</div>",
					'\t\t\t<div class="fn-body">',
					f'\t\t\t\t<code class="sig">{_html_escape(e.TypeName)} · no_constructor</code>',
				]
			)

			if props:
				lines.extend(
					[
						"\t\t\t\t<h4>Properties</h4>",
						"\t\t\t\t<table>",
						"\t\t\t\t\t<thead><tr><th>Lua</th><th>C++</th></tr></thead>",
						"\t\t\t\t\t<tbody>",
					]
				)
				for m in props:
					lines.append(
						"\t\t\t\t\t\t<tr>"
						f"<td><code>{_html_escape(m.LuaName)}</code></td>"
						f"<td><code>{_html_escape(m.Name)}</code></td></tr>"
					)
				lines.extend(["\t\t\t\t\t</tbody>", "\t\t\t\t</table>"])

			if funcs:
				lines.extend(
					[
						"\t\t\t\t<h4>Functions</h4>",
						"\t\t\t\t<table>",
						"\t\t\t\t\t<thead><tr><th>Lua</th><th>C++</th></tr></thead>",
						"\t\t\t\t\t<tbody>",
					]
				)
				for m in funcs:
					lines.append(
						"\t\t\t\t\t\t<tr>"
						f"<td><code>{_html_escape(m.LuaName)}</code></td>"
						f"<td><code>{_html_escape(m.Name)}</code></td></tr>"
					)
				lines.extend(["\t\t\t\t\t</tbody>", "\t\t\t\t</table>"])

			if not props and not funcs:
				lines.append("\t\t\t\t<p>无导出成员。</p>")

			lines.extend(["\t\t\t</div>", "\t\t</article>", ""])

	lines.extend(["\t</div>", "</section>", ""])
	return "\n".join(lines)


def _replace_marked_region(text: str, begin_tag: str, end_tag: str, inner: str) -> str:
	"""Replace content between HTML comment markers (markers kept)."""
	begin = f"<!-- {begin_tag} -->"
	end = f"<!-- {end_tag} -->"
	i0 = text.find(begin)
	i1 = text.find(end)
	if i0 < 0 or i1 < 0 or i1 < i0:
		raise ValueError(f"LuaAPI.html missing markers {begin_tag} / {end_tag}")
	# Preserve a trailing newline before END for readability.
	body = inner
	if body and not body.endswith("\n"):
		body += "\n"
	return text[: i0 + len(begin)] + "\n" + body + text[i1:]


def patch_lua_api_html(html_path: Path, entries: list[FTypeEntry]) -> str:
	"""Return full LuaAPI.html with Reflect usertype regions refreshed."""
	if not html_path.is_file():
		raise FileNotFoundError(f"Lua API doc missing: {html_path}")
	lua_types = _lua_export_entries(entries)
	text = html_path.read_text(encoding="utf-8")
	text = _replace_marked_region(
		text,
		"@@CATTY_LUA_REFLECT_TOC_BEGIN@@",
		"@@CATTY_LUA_REFLECT_TOC_END@@",
		render_lua_api_toc(lua_types),
	)
	text = _replace_marked_region(
		text,
		"@@CATTY_LUA_REFLECT_BODY_BEGIN@@",
		"@@CATTY_LUA_REFLECT_BODY_END@@",
		render_lua_api_body(lua_types),
	)
	return text


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
	parser.add_argument("--out-lua-h", type=Path, default=_DEFAULT_OUT_LUA_H, help="Output LuaBindings.gen.h")
	parser.add_argument("--out-lua-cpp", type=Path, default=_DEFAULT_OUT_LUA_CPP, help="Output LuaBindings.gen.cpp")
	parser.add_argument("--out-meta-h", type=Path, default=_DEFAULT_OUT_META_H, help="Output ReflectMeta.gen.h")
	parser.add_argument(
		"--out-lua-api",
		type=Path,
		default=_DEFAULT_OUT_LUA_API,
		help="Patch Doc/Engine/LuaAPI.html Reflect usertype regions",
	)
	parser.add_argument("--no-md", action="store_true", help="Skip markdown catalog")
	parser.add_argument("--no-lua", action="store_true", help="Skip Lua binding generation")
	parser.add_argument("--no-meta", action="store_true", help="Skip ReflectMeta.gen.h")
	parser.add_argument("--no-lua-api", action="store_true", help="Skip syncing LuaAPI.html")
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
	lua_h = render_lua_header()
	lua_cpp = render_lua_cpp(entries)
	meta_h = render_meta_header(entries)
	lua_count = len(_lua_export_entries(entries))
	meta_count = sum(1 for e in entries if e.bEmitReflMeta)

	lua_api_html: str | None = None
	if not args.no_lua_api:
		try:
			lua_api_html = patch_lua_api_html(args.out_lua_api, entries)
		except (FileNotFoundError, ValueError) as ex:
			print(f"[ERROR] {ex}", file=sys.stderr)
			return 1

	print(
		f"[Catty] reflect scan: {len(entries)} type(s), "
		f"{meta_count} CLASS meta, {lua_count} Lua usertype(s) from {len(roots)} root(s)"
	)

	if args.check:
		ok = True
		ok = run_check(args.out_h, header) and ok
		ok = run_check(args.out_json, js) and ok
		if not args.no_md:
			ok = run_check(args.out_md, md) and ok
		if not args.no_meta:
			ok = run_check(args.out_meta_h, meta_h) and ok
		if not args.no_lua:
			ok = run_check(args.out_lua_h, lua_h) and ok
			ok = run_check(args.out_lua_cpp, lua_cpp) and ok
		if lua_api_html is not None:
			ok = run_check(args.out_lua_api, lua_api_html) and ok
		return 0 if ok else 2

	changed = False

	def _write(path: Path, content: str) -> None:
		nonlocal changed
		if write_if_changed(path, content):
			print(f"[Catty] wrote {path}")
			changed = True
		else:
			print(f"[Catty] up-to-date {path}")

	_write(args.out_h, header)
	_write(args.out_json, js)
	if not args.no_md:
		_write(args.out_md, md)
	if not args.no_meta:
		_write(args.out_meta_h, meta_h)
	if not args.no_lua:
		_write(args.out_lua_h, lua_h)
		_write(args.out_lua_cpp, lua_cpp)
	if lua_api_html is not None:
		_write(args.out_lua_api, lua_api_html)

	if not changed:
		print("[Catty] reflect codegen: nothing changed")
	return 0


if __name__ == "__main__":
	raise SystemExit(main(sys.argv[1:]))