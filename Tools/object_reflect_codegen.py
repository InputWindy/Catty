# Run via Tools/object_reflect_codegen.bat / maho_python.bat — engine Tools/python only.
"""
UObject / struct / enum runtime reflection codegen.

Scans MAHO_OBJECT / MAHO_STRUCT / MAHO_ENUM (+ MAHO_PROPERTY / MAHO_FUNCTION)
and emits:
  - ObjectReflectTypes.gen.h
  - ObjectReflectTypes.gen.cpp
  - ObjectReflectCatalog.gen.json
  - Doc/Engine/ObjectReflectAPI.html (Registered types section)

Lua bindings are NOT generated here.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
ENGINE_ROOT = TOOLS_DIR.parent
sys.path.insert(0, str(TOOLS_DIR))

import maho_tools  # noqa: E402,F401

_DEFAULT_ROOTS = [ENGINE_ROOT / "Maho" / "Source"]
_DEFAULT_GEN_DIR = ENGINE_ROOT / "Maho" / "Source" / "Generated"
_DEFAULT_OUT_H = _DEFAULT_GEN_DIR / "ObjectReflectTypes.gen.h"
_DEFAULT_OUT_CPP = _DEFAULT_GEN_DIR / "ObjectReflectTypes.gen.cpp"
_DEFAULT_OUT_JSON = _DEFAULT_GEN_DIR / "ObjectReflectCatalog.gen.json"
_DEFAULT_OUT_HTML = ENGINE_ROOT / "Doc" / "Engine" / "ObjectReflectAPI.html"
_DEFAULT_OUT_LUA_H = _DEFAULT_GEN_DIR / "LuaReflectBindings.gen.h"
_DEFAULT_OUT_LUA_CPP = _DEFAULT_GEN_DIR / "LuaReflectBindings.gen.cpp"
_DEFAULT_OUT_LUA_HTML = ENGINE_ROOT / "Doc" / "Engine" / "LuaAPI.html"
_DEFAULT_OUT_RESOURCE_H = _DEFAULT_GEN_DIR / "ResourceTypes.gen.h"
_DEFAULT_OUT_RESOURCE_CPP = _DEFAULT_GEN_DIR / "ResourceTypes.gen.cpp"

_HTML_TOC_BEGIN = "<!-- @@MAHO_OBJECT_REFLECT_TOC_BEGIN@@ -->"
_HTML_TOC_END = "<!-- @@MAHO_OBJECT_REFLECT_TOC_END@@ -->"
_HTML_BODY_BEGIN = "<!-- @@MAHO_OBJECT_REFLECT_BODY_BEGIN@@ -->"
_HTML_BODY_END = "<!-- @@MAHO_OBJECT_REFLECT_BODY_END@@ -->"

_LUA_HTML_TOC_BEGIN = "<!-- @@MAHO_LUA_USERTYPE_TOC_BEGIN@@ -->"
_LUA_HTML_TOC_END = "<!-- @@MAHO_LUA_USERTYPE_TOC_END@@ -->"
_LUA_HTML_BODY_BEGIN = "<!-- @@MAHO_LUA_USERTYPE_BODY_BEGIN@@ -->"
_LUA_HTML_BODY_END = "<!-- @@MAHO_LUA_USERTYPE_BODY_END@@ -->"

_SOURCE_SUFFIXES = {".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp"}
_SKIP_DIR_NAMES = {".git", "Intermediate", "Binaries", "ThirdParty", "_deps", "Generated"}

_RE_OBJECT = re.compile(r"\bMAHO_OBJECT\s*\(")
_RE_STRUCT = re.compile(r"\bMAHO_STRUCT\s*\(")
_RE_ENUM = re.compile(r"\bMAHO_ENUM\s*\(")
_RE_PROPERTY = re.compile(r"\bMAHO_PROPERTY\s*\(")
_RE_FUNCTION = re.compile(r"\bMAHO_FUNCTION\s*\(")
_RE_CLASS = re.compile(r"\b(class|struct)\b")
_RE_GENERATED_BODY = re.compile(r"\bMAHO_GENERATED_BODY\s*\(")
_RE_GENERATED_STRUCT_BODY = re.compile(r"\bMAHO_GENERATED_STRUCT_BODY\s*\(")


@dataclass
class FMember:
	Name: str
	Kind: str  # property | function
	CppType: str = ""
	PropertyType: str = ""  # EPropertyType enumerator name
	ParamTypes: list[str] = field(default_factory=list)  # C++ param types
	ParamPropertyTypes: list[str] = field(default_factory=list)  # EPropertyType names
	ReturnType: str = ""  # EPropertyType name when bHasReturn
	bHasReturn: bool = False
	bSupported: bool = True
	SkipReason: str = ""


@dataclass
class FTypeEntry:
	TypeName: str  # Maho::UObject
	ShortName: str
	Bases: list[str] = field(default_factory=list)
	Super: str = ""  # qualified UObject subclass super, or ""
	Members: list[FMember] = field(default_factory=list)
	SourceRel: str = ""
	SourceLine: int = 0
	IncludePath: str = ""
	# Initial pool slots from `static constexpr int PoolSize = N;`; None = not GC-pooled.
	GCPooledSlots: int | None = None


_RE_POOL_SIZE = re.compile(
	r"\bstatic\s+constexpr\s+int\s+PoolSize\s*=\s*(\d+)\s*;"
)

@dataclass
class FEnumMember:
	Name: str
	Value: int


@dataclass
class FEnumEntry:
	TypeName: str
	ShortName: str
	bScoped: bool = True
	Underlying: str = ""
	Values: list[FEnumMember] = field(default_factory=list)
	SourceRel: str = ""
	SourceLine: int = 0
	IncludePath: str = ""


def _strip_comments(text: str) -> str:
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


def _line_number(text: str, index: int) -> int:
	return text.count("\n", 0, index) + 1


def _is_preprocessor_define(text: str, match_start: int) -> bool:
	line_start = text.rfind("\n", 0, match_start) + 1
	prefix = text[line_start:match_start].lstrip()
	return prefix.startswith("#")


def _skip_ws(text: str, i: int) -> int:
	n = len(text)
	while i < n and text[i].isspace():
		i += 1
	return i


def _is_ident_start(ch: str) -> bool:
	return ch.isalpha() or ch == "_"


def _is_ident_char(ch: str) -> bool:
	return ch.isalnum() or ch == "_"


def _read_ident(text: str, i: int) -> tuple[str, int]:
	i = _skip_ws(text, i)
	if i >= len(text) or not _is_ident_start(text[i]):
		return "", i
	j = i + 1
	while j < len(text) and _is_ident_char(text[j]):
		j += 1
	return text[i:j], j


def _skip_macro_call(text: str, match: re.Match[str]) -> int:
	open_paren = text.find("(", match.start())
	close = _find_matching(text, open_paren, "(", ")")
	if close < 0:
		raise ValueError(f"Unclosed macro at index {match.start()}")
	return close + 1


def _parse_bases(base_clause: str) -> list[str]:
	bases: list[str] = []
	for part in re.split(r",(?![^<]*>)", base_clause):
		part = part.strip()
		if not part:
			continue
		part = re.sub(r"^(public|protected|private|virtual)\s+", "", part).strip()
		part = re.sub(r"^(public|protected|private|virtual)\s+", "", part).strip()
		if part:
			bases.append(part)
	return bases


def _public_include_path(path: Path, repo_root: Path) -> str:
	text = str(path.resolve())
	idx = text.replace("\\", "/").rfind("/Public/")
	if idx >= 0:
		rel = text.replace("\\", "/")[idx + len("/Public/") :]
		return rel
	try:
		return str(path.relative_to(repo_root)).replace("\\", "/")
	except ValueError:
		return path.name


def _map_property_kind(cpp_type: str) -> tuple[str, bool, str]:
	t = re.sub(r"\s+", " ", cpp_type.strip())
	t = t.replace("const ", "").replace("volatile ", "").strip()
	t = t.rstrip("&*").strip()

	aliases = {
		"bool": ("Bool", True, ""),
		"std::int32_t": ("Int32", True, ""),
		"int32_t": ("Int32", True, ""),
		"int": ("Int32", True, ""),
		"std::uint32_t": ("UInt32", True, ""),
		"uint32_t": ("UInt32", True, ""),
		"unsigned": ("UInt32", True, ""),
		"unsigned int": ("UInt32", True, ""),
		"std::int64_t": ("Int64", True, ""),
		"int64_t": ("Int64", True, ""),
		"long long": ("Int64", True, ""),
		"std::uint64_t": ("UInt64", True, ""),
		"uint64_t": ("UInt64", True, ""),
		"unsigned long long": ("UInt64", True, ""),
		"float": ("Float", True, ""),
		"double": ("Double", True, ""),
		"std::string": ("String", True, ""),
		"string": ("String", True, ""),
		"FObjectRef": ("ObjectRef", True, ""),
		"Maho::FObjectRef": ("ObjectRef", True, ""),
		"EResourceType": ("EnumInt32", True, ""),
		"EObjectFlags": ("EnumInt32", True, ""),
		"EPackageFlags": ("EnumInt32", True, ""),
		"EResourceLoadState": ("EnumInt32", True, ""),
	}
	if t in aliases:
		kind, ok, reason = aliases[t]
		return kind, ok, reason
	if t.startswith("E") and len(t) > 1 and t[1].isupper():
		return "EnumInt32", True, ""
	return "", False, f"unsupported property type '{cpp_type}'"


def _parse_property_decl(decl: str) -> tuple[str, str] | None:
	"""Return (cpp_type, name) for a field declaration."""
	decl = decl.strip().rstrip(";").strip()
	if not decl or "(" in decl:
		return None
	if "[" in decl:
		return None
	if re.search(r"\b[A-Za-z_][A-Za-z0-9_]*\s*:\s*\d+", decl):
		return None
	if "=" in decl:
		decl = decl.split("=", 1)[0].strip()
	parts = decl.split()
	if len(parts) < 2:
		return None
	name = parts[-1]
	if name.startswith("*") or name.startswith("&"):
		return None
	cpp_type = " ".join(parts[:-1])
	if "static" in parts:
		return None
	return cpp_type, name


def _normalize_cpp_type(cpp_type: str) -> str:
	t = re.sub(r"\bconst\b", "", cpp_type)
	t = t.replace("&&", "").replace("&", "")
	t = re.sub(r"\s+", " ", t).strip()
	return t


def _parse_function_decl(
	decl: str,
) -> tuple[str, str, list[tuple[str, str]], str, bool, bool, str] | None:
	"""Return (ret_cpp, name, params, return_kind, b_has_return, b_supported, skip_reason)."""
	decl = decl.strip().rstrip(";").strip()
	decl = re.sub(r"\s*(const|override|final|noexcept|noexcept\s*\([^)]*\))\s*$", "", decl)
	decl = re.sub(r"\s*(const|override|final)\s*$", "", decl)
	m = re.match(r"^(.+?)\b([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)\s*$", decl, re.DOTALL)
	if not m:
		return None
	ret = m.group(1).strip()
	name = m.group(2)
	args = m.group(3).strip()
	if "static" in ret.split():
		return None
	ret_clean = re.sub(r"\[\[nodiscard\]\]", "", ret)
	ret_clean = re.sub(r"\b(virtual|inline|explicit)\b", "", ret_clean)
	ret_clean = re.sub(r"\s+", " ", ret_clean).strip()

	b_has_return = False
	return_kind = ""
	if ret_clean != "void":
		ret_norm = _normalize_cpp_type(ret_clean)
		kind, ok, reason = _map_property_kind(ret_norm)
		if not ok:
			return ret_clean, name, [], "", False, False, reason or f"unsupported return type '{ret_clean}'"
		b_has_return = True
		return_kind = kind
		ret_clean = ret_norm

	params: list[tuple[str, str]] = []
	if args and args != "void":
		parts: list[str] = []
		cur: list[str] = []
		depth = 0
		for ch in args:
			if ch == "<":
				depth += 1
				cur.append(ch)
			elif ch == ">":
				depth = max(0, depth - 1)
				cur.append(ch)
			elif ch == "," and depth == 0:
				parts.append("".join(cur).strip())
				cur = []
			else:
				cur.append(ch)
		tail = "".join(cur).strip()
		if tail:
			parts.append(tail)
		for part in parts:
			part = part.strip()
			if not part:
				continue
			if "=" in part:
				part = part.split("=", 1)[0].strip()
			tokens = part.split()
			if len(tokens) < 2:
				return ret_clean, name, [], return_kind, b_has_return, False, f"cannot parse param '{part}'"
			pname = tokens[-1]
			if pname.startswith("*") or pname.startswith("&"):
				return (
					ret_clean,
					name,
					[],
					return_kind,
					b_has_return,
					False,
					f"pointer/ref-only param not supported: '{part}'",
				)
			cpp_type = " ".join(tokens[:-1])
			if "*" in cpp_type:
				return (
					ret_clean,
					name,
					[],
					return_kind,
					b_has_return,
					False,
					f"pointer params not supported: '{part}'",
				)
			cpp_type_norm = _normalize_cpp_type(cpp_type)
			kind, ok, reason = _map_property_kind(cpp_type_norm)
			if not ok:
				return (
					ret_clean,
					name,
					[],
					return_kind,
					b_has_return,
					False,
					reason or f"unsupported param type '{cpp_type}'",
				)
			params.append((cpp_type_norm, kind))

	return ret_clean, name, params, return_kind, b_has_return, True, ""


def _scan_class_body(body: str, *, b_allow_functions: bool) -> list[FMember]:
	members: list[FMember] = []
	i = 0
	n = len(body)
	while i < n:
		prop_m = _RE_PROPERTY.search(body, i)
		func_m = _RE_FUNCTION.search(body, i) if b_allow_functions else None
		candidates = [m for m in (prop_m, func_m) if m]
		if not candidates:
			break
		m = min(candidates, key=lambda x: x.start())
		b_is_prop = prop_m is not None and m.start() == prop_m.start()
		if _is_preprocessor_define(body, m.start()):
			i = m.end()
			continue
		after = _skip_macro_call(body, m)
		after = _skip_ws(body, after)
		j = after
		paren = 0
		while j < n:
			ch = body[j]
			if ch == "(":
				paren += 1
			elif ch == ")" and paren > 0:
				paren -= 1
			elif ch == "{" and paren == 0:
				decl = body[after:j].strip()
				if not b_is_prop:
					parsed = _parse_function_decl(decl)
					if parsed:
						ret, name, params, ret_kind, b_ret, b_ok, reason = parsed
						members.append(
							FMember(
								Name=name,
								Kind="function",
								CppType=ret,
								ParamTypes=[p[0] for p in params],
								ParamPropertyTypes=[p[1] for p in params],
								ReturnType=ret_kind,
								bHasReturn=b_ret,
								bSupported=b_ok,
								SkipReason=reason if not b_ok else "",
							)
						)
				close = _find_matching(body, j, "{", "}")
				i = close + 1 if close >= 0 else j + 1
				break
			elif ch == ";" and paren == 0:
				decl = body[after:j].strip()
				if b_is_prop:
					parsed = _parse_property_decl(decl)
					if parsed:
						cpp_type, name = parsed
						kind, ok, reason = _map_property_kind(cpp_type)
						members.append(
							FMember(
								Name=name,
								Kind="property",
								CppType=cpp_type,
								PropertyType=kind,
								bSupported=ok,
								SkipReason=reason,
							)
						)
				else:
					parsed = _parse_function_decl(decl)
					if parsed:
						ret, name, params, ret_kind, b_ret, b_ok, reason = parsed
						members.append(
							FMember(
								Name=name,
								Kind="function",
								CppType=ret,
								ParamTypes=[p[0] for p in params],
								ParamPropertyTypes=[p[1] for p in params],
								ReturnType=ret_kind,
								bHasReturn=b_ret,
								bSupported=b_ok,
								SkipReason=reason if not b_ok else "",
							)
						)
				i = j + 1
				break
			j += 1
		else:
			i = after + 1
	return members


def _qualify_type(name: str, default_ns: str = "Maho") -> str:
	name = name.strip()
	if "::" in name:
		return name
	return f"{default_ns}::{name}"


def _short_name(qualified: str) -> str:
	return qualified.rsplit("::", 1)[-1]


def _read_type_name_after_class(text: str, i: int, rel: str, macro: str) -> tuple[str, int]:
	type_short = ""
	while True:
		i = _skip_ws(text, i)
		if text.startswith("[[", i):
			close = text.find("]]", i)
			if close < 0:
				raise ValueError("unclosed [[attribute]]")
			i = close + 2
			continue
		ident, ni = _read_ident(text, i)
		if not ident:
			raise ValueError(f"{rel}: {macro} missing type name")
		if ident == "MAHO_API" or ident.startswith("MAHO_"):
			i = ni
			continue
		if ident == "final":
			i = ni
			continue
		type_short = ident
		i = ni
		break
	return type_short, i


def _scan_reflected_type(
	text: str,
	m: re.Match[str],
	path: Path,
	repo_root: Path,
	rel: str,
	*,
	macro: str,
	b_object: bool,
) -> FTypeEntry:
	after_macro = _skip_macro_call(text, m)
	i = _skip_ws(text, after_macro)
	if text.startswith("template", i):
		raise ValueError(f"{rel}:{_line_number(text, m.start())}: {macro} on templates not supported")
	cm = _RE_CLASS.match(text, i)
	if not cm:
		raise ValueError(f"{rel}:{_line_number(text, m.start())}: {macro} must precede class/struct")
	i = _skip_ws(text, cm.end())
	type_short, i = _read_type_name_after_class(text, i, rel, macro)

	bases: list[str] = []
	i = _skip_ws(text, i)
	if i < len(text) and text[i] == ":":
		i += 1
		brace = text.find("{", i)
		if brace < 0:
			raise ValueError(f"{rel}: missing class body for {type_short}")
		bases = _parse_bases(text[i:brace])
		i = brace
	else:
		brace = text.find("{", i)
		if brace < 0:
			raise ValueError(f"{rel}: missing class body for {type_short}")
		i = brace

	body_end = _find_matching(text, i, "{", "}")
	if body_end < 0:
		raise ValueError(f"{rel}: unclosed class body for {type_short}")
	body = text[i + 1 : body_end]

	if b_object:
		if not _RE_GENERATED_BODY.search(body):
			print(f"[WARN] {rel}: {type_short} missing MAHO_GENERATED_BODY()")
	else:
		if not _RE_GENERATED_STRUCT_BODY.search(body):
			print(f"[WARN] {rel}: {type_short} missing MAHO_GENERATED_STRUCT_BODY()")

	# FGCSystem pool: class declares `static constexpr int PoolSize = N;`
	# TearDown is virtual UObject::OnPoolTearDown (codegen registers that path).
	gc_pooled_slots: int | None = None
	pool_m = _RE_POOL_SIZE.search(body)
	if pool_m:
		if not b_object:
			raise ValueError(f"{rel}: PoolSize only valid on MAHO_OBJECT types")
		gc_pooled_slots = int(pool_m.group(1))
		if gc_pooled_slots < 1:
			raise ValueError(f"{rel}: {type_short}::PoolSize must be >= 1")

	qualified = _qualify_type(type_short)
	super_q = ""
	if b_object:
		for b in bases:
			bq = _qualify_type(b)
			if bq == "Maho::UObject" or _short_name(bq).startswith("U"):
				super_q = bq
				break
		if not super_q and bases:
			super_q = _qualify_type(bases[0])
		if type_short == "UObject":
			super_q = ""

	members = _scan_class_body(body, b_allow_functions=b_object)
	for mem in members:
		if not mem.bSupported:
			print(f"[WARN] skip {qualified}::{mem.Name}: {mem.SkipReason}")

	return FTypeEntry(
		TypeName=qualified,
		ShortName=type_short,
		Bases=[_qualify_type(b) for b in bases],
		Super=super_q if (b_object and type_short != "UObject") else "",
		Members=members,
		SourceRel=rel,
		SourceLine=_line_number(text, m.start()),
		IncludePath=_public_include_path(path, repo_root),
		GCPooledSlots=gc_pooled_slots,
	)


def _eval_enum_expr(expr: str) -> int | None:
	"""Evaluate simple enum initializers: decimal literals and N << M."""
	expr = expr.strip()
	if not expr:
		return None
	m = re.match(
		r"^(\d+)\s*[uUlL]*\s*<<\s*(\d+)\s*[uUlL]*$",
		expr,
	)
	if m:
		return int(m.group(1)) << int(m.group(2))
	m = re.match(r"^(\d+)\s*[uUlL]*$", expr)
	if m:
		return int(m.group(1))
	m = re.match(r"^0[xX]([0-9a-fA-F]+)\s*[uUlL]*$", expr)
	if m:
		return int(m.group(1), 16)
	return None


def _parse_enum_body(body: str, rel: str, type_short: str) -> list[FEnumMember]:
	values: list[FEnumMember] = []
	# Split on commas at depth 0 (no nested parens expected in phase 1).
	parts: list[str] = []
	cur: list[str] = []
	depth = 0
	for ch in body:
		if ch == "(":
			depth += 1
			cur.append(ch)
		elif ch == ")":
			depth = max(0, depth - 1)
			cur.append(ch)
		elif ch == "," and depth == 0:
			parts.append("".join(cur).strip())
			cur = []
		else:
			cur.append(ch)
	tail = "".join(cur).strip()
	if tail:
		parts.append(tail)

	next_value = 0
	for part in parts:
		if not part:
			continue
		# Strip trailing attributes / comments already gone
		if "=" in part:
			name_part, expr = part.split("=", 1)
			name = name_part.strip()
			val = _eval_enum_expr(expr)
			if val is None:
				raise ValueError(
					f"{rel}: unsupported enum initializer for {type_short}::{name}: {expr.strip()}"
				)
			next_value = val
		else:
			name = part.strip()
		if not name or not _is_ident_start(name[0]):
			continue
		# Drop anything after first whitespace (shouldn't happen)
		name = name.split()[0]
		values.append(FEnumMember(Name=name, Value=next_value))
		next_value += 1
	return values


def _scan_enum(text: str, m: re.Match[str], path: Path, repo_root: Path, rel: str) -> FEnumEntry:
	after_macro = _skip_macro_call(text, m)
	i = _skip_ws(text, after_macro)
	ident, ni = _read_ident(text, i)
	if ident != "enum":
		raise ValueError(f"{rel}:{_line_number(text, m.start())}: MAHO_ENUM must precede enum")
	i = ni
	i = _skip_ws(text, i)
	b_scoped = False
	ident, ni = _read_ident(text, i)
	if ident == "class" or ident == "struct":
		b_scoped = True
		i = ni
		i = _skip_ws(text, i)
		ident, ni = _read_ident(text, i)
	if not ident:
		raise ValueError(f"{rel}: MAHO_ENUM missing enum name")
	type_short = ident
	i = ni
	underlying = ""
	i = _skip_ws(text, i)
	if i < len(text) and text[i] == ":":
		i += 1
		brace = text.find("{", i)
		if brace < 0:
			raise ValueError(f"{rel}: missing enum body for {type_short}")
		underlying = text[i:brace].strip()
		i = brace
	else:
		brace = text.find("{", i)
		if brace < 0:
			raise ValueError(f"{rel}: missing enum body for {type_short}")
		i = brace

	body_end = _find_matching(text, i, "{", "}")
	if body_end < 0:
		raise ValueError(f"{rel}: unclosed enum body for {type_short}")
	body = text[i + 1 : body_end]
	values = _parse_enum_body(body, rel, type_short)
	if not values:
		print(f"[WARN] {rel}: {type_short} has no enumerators")

	return FEnumEntry(
		TypeName=_qualify_type(type_short),
		ShortName=type_short,
		bScoped=b_scoped,
		Underlying=underlying,
		Values=values,
		SourceRel=rel,
		SourceLine=_line_number(text, m.start()),
		IncludePath=_public_include_path(path, repo_root),
	)


def scan_file(path: Path, repo_root: Path) -> tuple[list[FTypeEntry], list[FTypeEntry], list[FEnumEntry]]:
	raw = path.read_text(encoding="utf-8")
	text = _strip_comments(raw)
	objects: list[FTypeEntry] = []
	structs: list[FTypeEntry] = []
	enums: list[FEnumEntry] = []
	try:
		rel = str(path.relative_to(repo_root)).replace("\\", "/")
	except ValueError:
		rel = path.name

	for m in _RE_OBJECT.finditer(text):
		if _is_preprocessor_define(text, m.start()):
			continue
		objects.append(
			_scan_reflected_type(text, m, path, repo_root, rel, macro="MAHO_OBJECT", b_object=True)
		)

	for m in _RE_STRUCT.finditer(text):
		if _is_preprocessor_define(text, m.start()):
			continue
		structs.append(
			_scan_reflected_type(text, m, path, repo_root, rel, macro="MAHO_STRUCT", b_object=False)
		)

	for m in _RE_ENUM.finditer(text):
		if _is_preprocessor_define(text, m.start()):
			continue
		enums.append(_scan_enum(text, m, path, repo_root, rel))

	return objects, structs, enums


def scan_roots(
	roots: list[Path], repo_root: Path
) -> tuple[list[FTypeEntry], list[FTypeEntry], list[FEnumEntry]]:
	objects: list[FTypeEntry] = []
	structs: list[FTypeEntry] = []
	enums: list[FEnumEntry] = []
	seen_obj: set[str] = set()
	seen_struct: set[str] = set()
	seen_enum: set[str] = set()

	for root in roots:
		if not root.is_dir():
			print(f"[WARN] skip missing root: {root}")
			continue
		for path in sorted(root.rglob("*")):
			if not path.is_file() or path.suffix.lower() not in _SOURCE_SUFFIXES:
				continue
			if any(part in _SKIP_DIR_NAMES for part in path.parts):
				continue
			o, s, e = scan_file(path, repo_root)
			for entry in o:
				if entry.TypeName in seen_obj:
					raise ValueError(f"Duplicate MAHO_OBJECT type: {entry.TypeName}")
				seen_obj.add(entry.TypeName)
				objects.append(entry)
			for entry in s:
				if entry.TypeName in seen_struct:
					raise ValueError(f"Duplicate MAHO_STRUCT type: {entry.TypeName}")
				seen_struct.add(entry.TypeName)
				structs.append(entry)
			for entry in e:
				if entry.TypeName in seen_enum:
					raise ValueError(f"Duplicate MAHO_ENUM type: {entry.TypeName}")
				seen_enum.add(entry.TypeName)
				enums.append(entry)

	names = {e.TypeName for e in objects}
	for e in objects:
		if e.Super and e.Super not in names and e.Super != "Maho::UObject":
			if e.Super.endswith("UObject") and "Maho::UObject" in names:
				e.Super = "Maho::UObject"
			elif e.Super not in names:
				print(f"[WARN] {e.TypeName}: Super {e.Super} not a MAHO_OBJECT type")
	if objects and "Maho::UObject" not in names:
		raise ValueError("MAHO_OBJECT subclasses require Maho::UObject to be annotated")

	objects.sort(key=lambda e: (0 if e.TypeName == "Maho::UObject" else 1, e.TypeName))
	structs.sort(key=lambda e: e.TypeName)
	enums.sort(key=lambda e: e.TypeName)
	return objects, structs, enums


def _cpp_string(s: str) -> str:
	return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _mangled(type_name: str) -> str:
	return type_name.replace("::", "_")


def render_header() -> str:
	lines = [
		"//*****************************************************************************",
		"// ObjectReflectTypes.gen.h — GENERATED by Tools/object_reflect_codegen.py.",
		"//*****************************************************************************",
		"#pragma once",
		"",
		'#include <Core/Object/ObjectReflect.h>',
		"",
		"namespace Maho",
		"{",
		"",
		"class FGCSystem;",
		"",
		"/** Ensure generated object/struct/enum types are registered (idempotent). */",
		"void EnsureObjectReflectRegistered();",
		"",
		"/** Register MAHO_OBJECT types that declare PoolSize onto FGCSystem (FGCSystem::Initialize). */",
		"void RegisterGeneratedGCPooledTypes(FGCSystem& GC);",
		"",
		"} // namespace Maho",
		"",
	]
	return "\n".join(lines)


def _emit_prop_read(lines: list[str], kind: str, access: str) -> None:
	if kind == "Bool":
		lines.append(f"\tOutValue = FPropertyValue::FromBool({access});")
		lines.append("\tOutValue.Type = EPropertyType::Bool;")
	elif kind in {"Int32", "UInt32", "Int64", "EnumInt32"}:
		lines.append(f"\tOutValue = FPropertyValue::FromInt(static_cast<std::int64_t>({access}));")
		lines.append("\tOutValue.Type = EPropertyType::" + kind + ";")
	elif kind == "UInt64":
		lines.append(f"\tOutValue = FPropertyValue::FromUInt(static_cast<std::uint64_t>({access}));")
		lines.append("\tOutValue.Type = EPropertyType::UInt64;")
	elif kind in {"Float", "Double"}:
		lines.append(f"\tOutValue = FPropertyValue::FromFloat(static_cast<double>({access}));")
		lines.append("\tOutValue.Type = EPropertyType::" + kind + ";")
	elif kind == "String":
		lines.append(f"\tOutValue = FPropertyValue::FromString({access});")
		lines.append("\tOutValue.Type = EPropertyType::String;")
	elif kind == "ObjectRef":
		lines.append(f"\tOutValue = ToPropertyValue({access});")


def _emit_prop_write(lines: list[str], kind: str, cpp_type: str, access: str) -> None:
	if kind == "Bool":
		lines.append("\tif (Value.Type != EPropertyType::Bool) { return false; }")
		lines.append(f"\t{access} = Value.BoolValue;")
	elif kind in {"Int32", "EnumInt32"}:
		lines.append("\tif (Value.Type != EPropertyType::Int32 && Value.Type != EPropertyType::Int64")
		lines.append(
			"\t\t&& Value.Type != EPropertyType::UInt32 && Value.Type != EPropertyType::EnumInt32) { return false; }"
		)
		lines.append(f"\t{access} = static_cast<{cpp_type}>(Value.IntValue);")
	elif kind == "UInt32":
		lines.append("\tif (Value.Type != EPropertyType::UInt32 && Value.Type != EPropertyType::Int64")
		lines.append("\t\t&& Value.Type != EPropertyType::Int32) { return false; }")
		lines.append(f"\t{access} = static_cast<std::uint32_t>(Value.IntValue);")
	elif kind == "Int64":
		lines.append("\tif (Value.Type != EPropertyType::Int64 && Value.Type != EPropertyType::Int32")
		lines.append("\t\t&& Value.Type != EPropertyType::UInt32) { return false; }")
		lines.append(f"\t{access} = Value.IntValue;")
	elif kind == "UInt64":
		lines.append("\tif (Value.Type != EPropertyType::UInt64 && Value.Type != EPropertyType::Int64")
		lines.append("\t\t&& Value.Type != EPropertyType::Int32 && Value.Type != EPropertyType::UInt32) { return false; }")
		lines.append("\tif (Value.Type == EPropertyType::UInt64) {")
		lines.append(f"\t\t{access} = Value.UIntValue;")
		lines.append("\t} else {")
		lines.append(f"\t\t{access} = static_cast<std::uint64_t>(Value.IntValue);")
		lines.append("\t}")
	elif kind == "Float":
		lines.append("\tif (Value.Type != EPropertyType::Float && Value.Type != EPropertyType::Double) { return false; }")
		lines.append(f"\t{access} = static_cast<float>(Value.FloatValue);")
	elif kind == "Double":
		lines.append("\tif (Value.Type != EPropertyType::Double && Value.Type != EPropertyType::Float) { return false; }")
		lines.append(f"\t{access} = Value.FloatValue;")
	elif kind == "String":
		lines.append("\tif (Value.Type != EPropertyType::String) { return false; }")
		lines.append(f"\t{access} = Value.StringValue;")
	elif kind == "ObjectRef":
		lines.append("\tif (Value.Type != EPropertyType::ObjectRef) { return false; }")
		lines.append(f"\t{access} = ObjectRefFromPropertyValue(Value);")


def _getter_setter_fns(e: FTypeEntry, mem: FMember) -> tuple[str, str, str]:
	mangled = _mangled(e.TypeName)
	gname = f"Get_{mangled}_{mem.Name}"
	sname = f"Set_{mangled}_{mem.Name}"
	kind = mem.PropertyType
	lines: list[str] = []
	lines.append(f"bool FObjectReflectDetail::{gname}(const UObject* Object, FPropertyValue& OutValue)")
	lines.append("{")
	lines.append(f"\tconst auto* Self = static_cast<const {e.TypeName}*>(Object);")
	_emit_prop_read(lines, kind, f"Self->{mem.Name}")
	lines.append("\treturn true;")
	lines.append("}")
	lines.append("")
	lines.append(f"bool FObjectReflectDetail::{sname}(UObject* Object, const FPropertyValue& Value)")
	lines.append("{")
	lines.append(f"\tauto* Self = static_cast<{e.TypeName}*>(Object);")
	_emit_prop_write(lines, kind, mem.CppType, f"Self->{mem.Name}")
	lines.append("\treturn true;")
	lines.append("}")
	lines.append("")
	return f"&FObjectReflectDetail::{gname}", f"&FObjectReflectDetail::{sname}", "\n".join(lines)


def _struct_getter_setter_fns(e: FTypeEntry, mem: FMember) -> tuple[str, str, str]:
	mangled = _mangled(e.TypeName)
	gname = f"Get_{mangled}_{mem.Name}"
	sname = f"Set_{mangled}_{mem.Name}"
	kind = mem.PropertyType
	lines: list[str] = []
	lines.append(f"bool FStructReflectDetail::{gname}(const void* Struct, FPropertyValue& OutValue)")
	lines.append("{")
	lines.append(f"\tconst auto* Self = static_cast<const {e.TypeName}*>(Struct);")
	_emit_prop_read(lines, kind, f"Self->{mem.Name}")
	lines.append("\treturn true;")
	lines.append("}")
	lines.append("")
	lines.append(f"bool FStructReflectDetail::{sname}(void* Struct, const FPropertyValue& Value)")
	lines.append("{")
	lines.append(f"\tauto* Self = static_cast<{e.TypeName}*>(Struct);")
	_emit_prop_write(lines, kind, mem.CppType, f"Self->{mem.Name}")
	lines.append("\treturn true;")
	lines.append("}")
	lines.append("")
	return f"&FStructReflectDetail::{gname}", f"&FStructReflectDetail::{sname}", "\n".join(lines)


def _invoke_fn(e: FTypeEntry, mem: FMember) -> tuple[str, str]:
	mangled = _mangled(e.TypeName)
	iname = f"Invoke_{mangled}_{mem.Name}"
	lines = [
		f"bool FObjectReflectDetail::{iname}(UObject* Object, const FPropertyValue* Args, std::size_t ArgCount, FPropertyValue* OutReturn)",
		"{",
		"\t(void)Args;",
		"\t(void)ArgCount;",
		"\t(void)OutReturn;",
		f"\tauto* Self = static_cast<{e.TypeName}*>(Object);",
	]
	call_args: list[str] = []
	for idx, (cpp_type, kind) in enumerate(zip(mem.ParamTypes, mem.ParamPropertyTypes)):
		var = f"A{idx}"
		if kind == "Bool":
			lines.append(f"\tconst bool {var} = Args[{idx}].BoolValue;")
		elif kind in {"Int32", "UInt32", "Int64", "EnumInt32"}:
			lines.append(f"\tconst auto {var} = static_cast<{cpp_type}>(Args[{idx}].IntValue);")
		elif kind == "UInt64":
			lines.append(f"\tstd::uint64_t {var} = 0;")
			lines.append(f"\tif (Args[{idx}].Type == EPropertyType::UInt64)")
			lines.append("\t{")
			lines.append(f"\t\t{var} = Args[{idx}].UIntValue;")
			lines.append("\t}")
			lines.append("\telse")
			lines.append("\t{")
			lines.append(f"\t\t{var} = static_cast<std::uint64_t>(Args[{idx}].IntValue);")
			lines.append("\t}")
			if cpp_type not in {"std::uint64_t", "uint64_t"}:
				lines.append(f"\tconst auto {var}_Typed = static_cast<{cpp_type}>({var});")
				call_args.append(f"{var}_Typed")
				continue
		elif kind in {"Float", "Double"}:
			lines.append(f"\tconst auto {var} = static_cast<{cpp_type}>(Args[{idx}].FloatValue);")
		elif kind == "String":
			lines.append(f"\tconst std::string& {var} = Args[{idx}].StringValue;")
		elif kind == "ObjectRef":
			lines.append(f"\tFObjectRef {var} = ObjectRefFromPropertyValue(Args[{idx}]);")
		else:
			lines.append(f"\treturn false; // unsupported kind {kind}")
			lines.append("}")
			lines.append("")
			return f"&FObjectReflectDetail::{iname}", "\n".join(lines)
		call_args.append(var)

	args_joined = ", ".join(call_args)
	if mem.bHasReturn:
		lines.append(f"\tauto Result = Self->{mem.Name}({args_joined});")
		lines.append("\tif (OutReturn)")
		lines.append("\t{")
		rk = mem.ReturnType
		if rk == "Bool":
			lines.append("\t\t*OutReturn = FPropertyValue::FromBool(static_cast<bool>(Result));")
		elif rk in {"Int32", "UInt32", "Int64", "EnumInt32"}:
			lines.append("\t\t*OutReturn = FPropertyValue::FromInt(static_cast<std::int64_t>(Result));")
		elif rk == "UInt64":
			lines.append("\t\t*OutReturn = FPropertyValue::FromUInt(static_cast<std::uint64_t>(Result));")
		elif rk in {"Float", "Double"}:
			lines.append("\t\t*OutReturn = FPropertyValue::FromFloat(static_cast<double>(Result));")
		elif rk == "String":
			lines.append("\t\t*OutReturn = FPropertyValue::FromString(std::string(Result));")
		elif rk == "ObjectRef":
			lines.append("\t\t*OutReturn = ToPropertyValue(std::move(Result));")
		if rk != "ObjectRef":
			lines.append(f"\t\tOutReturn->Type = EPropertyType::{rk};")
		lines.append("\t}")
	else:
		lines.append(f"\tSelf->{mem.Name}({args_joined});")
	lines.append("\treturn true;")
	lines.append("}")
	lines.append("")
	return f"&FObjectReflectDetail::{iname}", "\n".join(lines)


def render_cpp(
	objects: list[FTypeEntry],
	structs: list[FTypeEntry],
	enums: list[FEnumEntry],
) -> str:
	includes: list[str] = []
	seen: set[str] = set()
	for e in list(objects) + list(structs) + list(enums):
		inc = e.IncludePath.replace("\\", "/")
		if inc and inc not in seen:
			seen.add(inc)
			includes.append(inc)

	lines: list[str] = [
		"//*****************************************************************************",
		"// ObjectReflectTypes.gen.cpp — GENERATED by Tools/object_reflect_codegen.py.",
		"//*****************************************************************************",
		"",
		'#include <ObjectReflectTypes.gen.h>',
		"",
		'#include <Core/Extension/GC/GC.h>',
		'#include <Core/Object/ObjectReflect.h>',
		"",
	]
	for inc in includes:
		lines.append(f'#include <{inc}>')
	lines.append("")
	lines.append("namespace Maho")
	lines.append("{")
	lines.append("")

	# ---- Object detail ----
	prop_meta: dict[str, list[tuple[FMember, str, str]]] = {}
	func_meta: dict[str, list[tuple[FMember, str]]] = {}

	lines.append("struct FObjectReflectDetail")
	lines.append("{")
	for e in objects:
		prop_meta[e.TypeName] = []
		func_meta[e.TypeName] = []
		for mem in e.Members:
			if mem.Kind == "property" and mem.bSupported:
				mangled = _mangled(e.TypeName)
				lines.append(
					f"\tstatic bool Get_{mangled}_{mem.Name}(const UObject* Object, FPropertyValue& OutValue);"
				)
				lines.append(
					f"\tstatic bool Set_{mangled}_{mem.Name}(UObject* Object, const FPropertyValue& Value);"
				)
			elif mem.Kind == "function" and mem.bSupported:
				mangled = _mangled(e.TypeName)
				lines.append(
					f"\tstatic bool Invoke_{mangled}_{mem.Name}(UObject* Object, const FPropertyValue* Args, std::size_t ArgCount, FPropertyValue* OutReturn);"
				)
	lines.append("};")
	lines.append("")

	for e in objects:
		for mem in e.Members:
			if mem.Kind == "property" and mem.bSupported:
				g, s, src = _getter_setter_fns(e, mem)
				lines.append(src)
				prop_meta[e.TypeName].append((mem, g, s))
			elif mem.Kind == "function" and mem.bSupported:
				iname, src = _invoke_fn(e, mem)
				lines.append(src)
				func_meta[e.TypeName].append((mem, iname))

	for e in objects:
		mangled = _mangled(e.TypeName)
		props = prop_meta[e.TypeName]
		funcs = func_meta[e.TypeName]
		if props:
			lines.append(f"static const FProperty GProperties_{mangled}[] =")
			lines.append("{")
			for mem, g, s in props:
				lines.append(
					f"\t{{ {_cpp_string(mem.Name)}, EPropertyType::{mem.PropertyType}, "
					f"EPropertyFlags::Edit, {g}, {s} }},"
				)
			lines.append("};")
			lines.append("")
		for mem, iname in funcs:
			if mem.ParamPropertyTypes:
				lines.append(f"static const EPropertyType GParams_{mangled}_{mem.Name}[] =")
				lines.append("{")
				for kind in mem.ParamPropertyTypes:
					lines.append(f"\tEPropertyType::{kind},")
				lines.append("};")
				lines.append("")
		if funcs:
			lines.append(f"static const FFunction GFunctions_{mangled}[] =")
			lines.append("{")
			for mem, iname in funcs:
				if mem.ParamPropertyTypes:
					params_expr = f"GParams_{mangled}_{mem.Name}"
					count_expr = str(len(mem.ParamPropertyTypes))
				else:
					params_expr = "nullptr"
					count_expr = "0"
				if mem.bHasReturn:
					ret_kind = f"EPropertyType::{mem.ReturnType}"
					b_ret = "true"
				else:
					ret_kind = "EPropertyType::Bool"
					b_ret = "false"
				lines.append(
					f"\t{{ {_cpp_string(mem.Name)}, {params_expr}, {count_expr}, "
					f"{ret_kind}, {b_ret}, {iname} }},"
				)
			lines.append("};")
			lines.append("")

		lines.append(f"const FObjectType& {e.TypeName}::StaticType()")
		lines.append("{")
		lines.append("\tstatic FObjectType Type =")
		lines.append("\t{")
		lines.append(f"\t\t{_cpp_string(e.TypeName)},")
		lines.append("\t\tnullptr,")
		if props:
			lines.append(f"\t\tGProperties_{mangled},")
			lines.append(f"\t\tsizeof(GProperties_{mangled}) / sizeof(GProperties_{mangled}[0]),")
		else:
			lines.append("\t\tnullptr,")
			lines.append("\t\t0,")
		if funcs:
			lines.append(f"\t\tGFunctions_{mangled},")
			lines.append(f"\t\tsizeof(GFunctions_{mangled}) / sizeof(GFunctions_{mangled}[0]),")
		else:
			lines.append("\t\tnullptr,")
			lines.append("\t\t0,")
		lines.append("\t};")
		if e.Super:
			lines.append("\tstatic bool bLinked = false;")
			lines.append("\tif (!bLinked)")
			lines.append("\t{")
			lines.append(f"\t\tType.Super = &{e.Super}::StaticType();")
			lines.append("\t\tbLinked = true;")
			lines.append("\t}")
		lines.append("\treturn Type;")
		lines.append("}")
		lines.append("")
		lines.append(f"const FObjectType& {e.TypeName}::GetObjectType() const")
		lines.append("{")
		lines.append("\treturn StaticType();")
		lines.append("}")
		lines.append("")

	# ---- Struct detail ----
	struct_prop_meta: dict[str, list[tuple[FMember, str, str]]] = {}
	lines.append("struct FStructReflectDetail")
	lines.append("{")
	for e in structs:
		struct_prop_meta[e.TypeName] = []
		for mem in e.Members:
			if mem.Kind == "property" and mem.bSupported:
				mangled = _mangled(e.TypeName)
				lines.append(
					f"\tstatic bool Get_{mangled}_{mem.Name}(const void* Struct, FPropertyValue& OutValue);"
				)
				lines.append(
					f"\tstatic bool Set_{mangled}_{mem.Name}(void* Struct, const FPropertyValue& Value);"
				)
	lines.append("};")
	lines.append("")

	for e in structs:
		for mem in e.Members:
			if mem.Kind == "property" and mem.bSupported:
				g, s, src = _struct_getter_setter_fns(e, mem)
				lines.append(src)
				struct_prop_meta[e.TypeName].append((mem, g, s))

	for e in structs:
		mangled = _mangled(e.TypeName)
		props = struct_prop_meta[e.TypeName]
		if props:
			lines.append(f"static const FStructProperty GStructProperties_{mangled}[] =")
			lines.append("{")
			for mem, g, s in props:
				lines.append(
					f"\t{{ {_cpp_string(mem.Name)}, EPropertyType::{mem.PropertyType}, "
					f"EPropertyFlags::Edit, {g}, {s} }},"
				)
			lines.append("};")
			lines.append("")

		lines.append(f"const FStructType& {e.TypeName}::StaticType()")
		lines.append("{")
		lines.append("\tstatic FStructType Type =")
		lines.append("\t{")
		lines.append(f"\t\t{_cpp_string(e.TypeName)},")
		if props:
			lines.append(f"\t\tGStructProperties_{mangled},")
			lines.append(f"\t\tsizeof(GStructProperties_{mangled}) / sizeof(GStructProperties_{mangled}[0]),")
		else:
			lines.append("\t\tnullptr,")
			lines.append("\t\t0,")
		lines.append(f"\t\tsizeof({e.TypeName}),")
		lines.append("\t};")
		lines.append("\treturn Type;")
		lines.append("}")
		lines.append("")

	# ---- Enums ----
	for e in enums:
		mangled = _mangled(e.TypeName)
		if e.Values:
			lines.append(f"static const FEnumValue GEnumValues_{mangled}[] =")
			lines.append("{")
			for v in e.Values:
				lines.append(f"\t{{ {_cpp_string(v.Name)}, static_cast<std::int64_t>({v.Value}) }},")
			lines.append("};")
			lines.append("")
		lines.append(f"static FEnumType GEnumType_{mangled} =")
		lines.append("{")
		lines.append(f"\t{_cpp_string(e.TypeName)},")
		if e.Values:
			lines.append(f"\tGEnumValues_{mangled},")
			lines.append(f"\tsizeof(GEnumValues_{mangled}) / sizeof(GEnumValues_{mangled}[0]),")
		else:
			lines.append("\tnullptr,")
			lines.append("\t0,")
		lines.append("};")
		lines.append("")

	# ---- Registries ----
	lines.append("void FObjectTypeRegistry::RegisterGeneratedTypes()")
	lines.append("{")
	lines.append("\tFObjectTypeRegistry& Registry = FObjectTypeRegistry::Get();")
	for e in objects:
		lines.append(f"\tRegistry.RegisterType({e.TypeName}::StaticType());")
	lines.append("}")
	lines.append("")

	lines.append("void FStructTypeRegistry::RegisterGeneratedTypes()")
	lines.append("{")
	lines.append("\tFStructTypeRegistry& Registry = FStructTypeRegistry::Get();")
	for e in structs:
		lines.append(f"\tRegistry.RegisterType({e.TypeName}::StaticType());")
	lines.append("}")
	lines.append("")

	lines.append("void FEnumTypeRegistry::RegisterGeneratedTypes()")
	lines.append("{")
	lines.append("\tFEnumTypeRegistry& Registry = FEnumTypeRegistry::Get();")
	for e in enums:
		mangled = _mangled(e.TypeName)
		lines.append(f"\tRegistry.RegisterType(GEnumType_{mangled});")
	lines.append("}")
	lines.append("")

	pooled = [e for e in objects if e.GCPooledSlots is not None]
	lines.append("void RegisterGeneratedGCPooledTypes(FGCSystem& GC)")
	lines.append("{")
	if not pooled:
		lines.append("\t(void)GC;")
	for e in pooled:
		lines.append(
			f"\tGC.RegisterObjectType<{e.TypeName}>({e.TypeName}::PoolSize);"
		)
	lines.append("}")
	lines.append("")

	lines.append("void EnsureObjectReflectRegistered()")
	lines.append("{")
	lines.append("\tFObjectTypeRegistry::RegisterGeneratedTypes();")
	lines.append("\tFStructTypeRegistry::RegisterGeneratedTypes();")
	lines.append("\tFEnumTypeRegistry::RegisterGeneratedTypes();")
	lines.append("}")
	lines.append("")
	lines.append("namespace")
	lines.append("{")
	lines.append("struct FObjectReflectAutoRegister")
	lines.append("{")
	lines.append("\tFObjectReflectAutoRegister()")
	lines.append("\t{")
	lines.append("\t\tEnsureObjectReflectRegistered();")
	lines.append("\t}")
	lines.append("};")
	lines.append("static FObjectReflectAutoRegister GObjectReflectAutoRegister;")
	lines.append("} // namespace")
	lines.append("")
	lines.append("} // namespace Maho")
	lines.append("")
	return "\n".join(lines)


def render_json(
	objects: list[FTypeEntry],
	structs: list[FTypeEntry],
	enums: list[FEnumEntry],
) -> str:
	payload = {
		"generator": "Tools/object_reflect_codegen.py",
		"objects": [
			{
				"type": e.TypeName,
				"super": e.Super,
				"source": f"{e.SourceRel}:{e.SourceLine}",
				"gcPooledSlots": e.GCPooledSlots,
				"properties": [
					{"name": m.Name, "cppType": m.CppType, "type": m.PropertyType}
					for m in e.Members
					if m.Kind == "property" and m.bSupported
				],
				"functions": [
					{
						"name": m.Name,
						"params": [
							{"cppType": t, "type": k}
							for t, k in zip(m.ParamTypes, m.ParamPropertyTypes)
						],
						"returnType": m.ReturnType if m.bHasReturn else None,
					}
					for m in e.Members
					if m.Kind == "function" and m.bSupported
				],
			}
			for e in objects
		],
		"structs": [
			{
				"type": e.TypeName,
				"source": f"{e.SourceRel}:{e.SourceLine}",
				"properties": [
					{"name": m.Name, "cppType": m.CppType, "type": m.PropertyType}
					for m in e.Members
					if m.Kind == "property" and m.bSupported
				],
			}
			for e in structs
		],
		"enums": [
			{
				"type": e.TypeName,
				"scoped": e.bScoped,
				"underlying": e.Underlying,
				"source": f"{e.SourceRel}:{e.SourceLine}",
				"values": [{"name": v.Name, "value": v.Value} for v in e.Values],
			}
			for e in enums
		],
	}
	return json.dumps(payload, indent=2) + "\n"


def write_if_changed(path: Path, content: str) -> bool:
	path.parent.mkdir(parents=True, exist_ok=True)
	if path.is_file() and path.read_text(encoding="utf-8") == content:
		return False
	path.write_text(content, encoding="utf-8", newline="\n")
	return True


def _html_escape(s: str) -> str:
	return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def _anchor_id(qualified: str) -> str:
	return "type-" + qualified.replace("::", "-")


def render_html_toc(
	objects: list[FTypeEntry],
	structs: list[FTypeEntry],
	enums: list[FEnumEntry],
) -> str:
	lines: list[str] = [
		_HTML_TOC_BEGIN,
		'\t<div class="sec">Objects</div>',
		"\t<ul>",
	]
	for e in objects:
		aid = _anchor_id(e.TypeName)
		lines.append(f'\t\t<li><a href="#{aid}">{_html_escape(e.ShortName)}</a></li>')
	lines.append("\t</ul>")
	lines.append('\t<div class="sec">Structs</div>')
	lines.append("\t<ul>")
	for e in structs:
		aid = _anchor_id(e.TypeName)
		lines.append(f'\t\t<li><a href="#{aid}">{_html_escape(e.ShortName)}</a></li>')
	lines.append("\t</ul>")
	lines.append('\t<div class="sec">Enums</div>')
	lines.append("\t<ul>")
	for e in enums:
		aid = _anchor_id(e.TypeName)
		lines.append(f'\t\t<li><a href="#{aid}">{_html_escape(e.ShortName)}</a></li>')
	lines.append("\t</ul>")
	lines.append(_HTML_TOC_END)
	return "\n".join(lines)


def render_html_body(
	objects: list[FTypeEntry],
	structs: list[FTypeEntry],
	enums: list[FEnumEntry],
) -> str:
	lines: list[str] = [
		_HTML_BODY_BEGIN,
		'<section class="card" id="catalog">',
		"\t<h2>Registered types</h2>",
		'\t<div class="body">',
		"\t\t<p>"
		f"Codegen 扫描结果：{len(objects)} object / {len(structs)} struct / {len(enums)} enum。"
		" 修改标注宏后重新构建即可刷新本节。</p>",
		"",
	]

	def prop_rows(members: list[FMember]) -> None:
		props = [m for m in members if m.Kind == "property" and m.bSupported]
		if not props:
			lines.append("\t\t\t\t<p>无反射属性。</p>")
			return
		lines.append("\t\t\t\t<h4>Properties</h4>")
		lines.append("\t\t\t\t<table>")
		lines.append("\t\t\t\t\t<thead><tr><th>Name</th><th>C++</th><th>Type</th></tr></thead>")
		lines.append("\t\t\t\t\t<tbody>")
		for m in props:
			lines.append(
				"\t\t\t\t\t\t<tr>"
				f"<td><code>{_html_escape(m.Name)}</code></td>"
				f"<td><code>{_html_escape(m.CppType)}</code></td>"
				f"<td><code>{_html_escape(m.PropertyType)}</code></td>"
				"</tr>"
			)
		lines.append("\t\t\t\t\t</tbody>")
		lines.append("\t\t\t\t</table>")

	for e in objects:
		aid = _anchor_id(e.TypeName)
		super_txt = e.Super if e.Super else "(root)"
		lines.append(f'\t\t<article class="fn" id="{aid}">')
		lines.append('\t\t\t<div class="fn-head">')
		lines.append(f'\t\t\t\t<h3 class="fn-name">{_html_escape(e.TypeName)}</h3>')
		lines.append('\t\t\t\t<span class="tag tag-object">object</span>')
		lines.append("\t\t\t</div>")
		lines.append('\t\t\t<div class="fn-body">')
		lines.append(
			f'\t\t\t\t<code class="sig">super: {_html_escape(super_txt)}'
			f" · {_html_escape(e.SourceRel)}:{e.SourceLine}</code>"
		)
		prop_rows(e.Members)
		funcs = [m for m in e.Members if m.Kind == "function" and m.bSupported]
		if funcs:
			lines.append("\t\t\t\t<h4>Functions</h4>")
			lines.append("\t\t\t\t<table>")
			lines.append("\t\t\t\t\t<thead><tr><th>Name</th><th>Signature</th></tr></thead>")
			lines.append("\t\t\t\t\t<tbody>")
			for m in funcs:
				sig_params = ", ".join(m.ParamTypes) if m.ParamTypes else ""
				ret = m.CppType if m.bHasReturn else "void"
				sig = f"{ret}({sig_params})"
				lines.append(
					"\t\t\t\t\t\t<tr>"
					f"<td><code>{_html_escape(m.Name)}</code></td>"
					f"<td><code>{_html_escape(sig)}</code></td>"
					"</tr>"
				)
			lines.append("\t\t\t\t\t</tbody>")
			lines.append("\t\t\t\t</table>")
		lines.append("\t\t\t</div>")
		lines.append("\t\t</article>")
		lines.append("")

	for e in structs:
		aid = _anchor_id(e.TypeName)
		lines.append(f'\t\t<article class="fn" id="{aid}">')
		lines.append('\t\t\t<div class="fn-head">')
		lines.append(f'\t\t\t\t<h3 class="fn-name">{_html_escape(e.TypeName)}</h3>')
		lines.append('\t\t\t\t<span class="tag tag-struct">struct</span>')
		lines.append("\t\t\t</div>")
		lines.append('\t\t\t<div class="fn-body">')
		lines.append(
			f'\t\t\t\t<code class="sig">{_html_escape(e.SourceRel)}:{e.SourceLine}</code>'
		)
		prop_rows(e.Members)
		lines.append("\t\t\t</div>")
		lines.append("\t\t</article>")
		lines.append("")

	for e in enums:
		aid = _anchor_id(e.TypeName)
		scoped = "enum class" if e.bScoped else "enum"
		under = e.Underlying if e.Underlying else "(default)"
		lines.append(f'\t\t<article class="fn" id="{aid}">')
		lines.append('\t\t\t<div class="fn-head">')
		lines.append(f'\t\t\t\t<h3 class="fn-name">{_html_escape(e.TypeName)}</h3>')
		lines.append('\t\t\t\t<span class="tag tag-enum">enum</span>')
		lines.append("\t\t\t</div>")
		lines.append('\t\t\t<div class="fn-body">')
		lines.append(
			f'\t\t\t\t<code class="sig">{_html_escape(scoped)}'
			f" : {_html_escape(under)} · {_html_escape(e.SourceRel)}:{e.SourceLine}</code>"
		)
		if e.Values:
			lines.append("\t\t\t\t<h4>Values</h4>")
			lines.append("\t\t\t\t<table>")
			lines.append("\t\t\t\t\t<thead><tr><th>Name</th><th>Value</th></tr></thead>")
			lines.append("\t\t\t\t\t<tbody>")
			for v in e.Values:
				lines.append(
					"\t\t\t\t\t\t<tr>"
					f"<td><code>{_html_escape(v.Name)}</code></td>"
					f"<td><code>{v.Value}</code></td>"
					"</tr>"
				)
			lines.append("\t\t\t\t\t</tbody>")
			lines.append("\t\t\t\t</table>")
		else:
			lines.append("\t\t\t\t<p>无枚举项。</p>")
		lines.append("\t\t\t</div>")
		lines.append("\t\t</article>")
		lines.append("")

	lines.append("\t</div>")
	lines.append("</section>")
	lines.append(_HTML_BODY_END)
	return "\n".join(lines)


def _patch_marked_region(text: str, begin: str, end: str, replacement: str) -> str:
	i0 = text.find(begin)
	i1 = text.find(end)
	if i0 < 0 or i1 < 0 or i1 < i0:
		raise ValueError(f"missing markers {begin} ... {end}")
	i1_end = i1 + len(end)
	return text[:i0] + replacement + text[i1_end:]


def pascal_to_snake(name: str) -> str:
	out: list[str] = []
	for i, ch in enumerate(name):
		if ch.isupper():
			if i > 0:
				out.append("_")
			out.append(ch.lower())
		else:
			out.append(ch)
	return "".join(out)


def _lua_usertype_name(short: str) -> str:
	"""UObject -> object, UPackage -> package (UE-style U/F prefix strip)."""
	if len(short) > 1 and short[0] in "UF" and short[1].isupper():
		return pascal_to_snake(short[1:])
	return pascal_to_snake(short)


def _lua_wrapper_name(short: str) -> str:
	return f"FLua_{short}"


def _cpp_lua_arg_type(kind: str) -> str:
	return {
		"Bool": "bool",
		"Int32": "std::int32_t",
		"UInt32": "std::uint32_t",
		"Int64": "std::int64_t",
		"UInt64": "std::uint64_t",
		"Float": "float",
		"Double": "double",
		"String": "std::string",
		"EnumInt32": "std::int64_t",
		"ObjectRef": "FLua_UObject",
	}.get(kind, "std::int64_t")


def _emit_pack_from_lua_arg(lines: list[str], indent: str, var: str, kind: str, arg_expr: str) -> None:
	if kind == "Bool":
		lines.append(f"{indent}{var} = FPropertyValue::FromBool({arg_expr});")
		lines.append(f"{indent}{var}.Type = EPropertyType::Bool;")
	elif kind in {"Int32", "Int64", "UInt32", "EnumInt32"}:
		lines.append(f"{indent}{var} = FPropertyValue::FromInt(static_cast<std::int64_t>({arg_expr}));")
		lines.append(f"{indent}{var}.Type = EPropertyType::{kind};")
	elif kind == "UInt64":
		lines.append(f"{indent}{var} = FPropertyValue::FromUInt(static_cast<std::uint64_t>({arg_expr}));")
	elif kind in {"Float", "Double"}:
		lines.append(f"{indent}{var} = FPropertyValue::FromFloat(static_cast<double>({arg_expr}));")
		lines.append(f"{indent}{var}.Type = EPropertyType::{kind};")
	elif kind == "String":
		lines.append(f"{indent}{var} = FPropertyValue::FromString({arg_expr});")
	elif kind == "ObjectRef":
		lines.append(f"{indent}{var} = FPropertyValue::FromObject({arg_expr}.GetRaw());")
	else:
		lines.append(f"{indent}{var} = FPropertyValue::FromInt(static_cast<std::int64_t>({arg_expr}));")
		lines.append(f"{indent}{var}.Type = EPropertyType::EnumInt32;")


def _emit_return_from_property_value(kind: str, expr: str = "Out") -> str:
	if kind == "Bool":
		return f"{expr}.BoolValue"
	if kind in {"Int32", "Int64", "UInt32", "EnumInt32"}:
		return f"static_cast<{_cpp_lua_arg_type(kind)}>({expr}.IntValue)"
	if kind == "UInt64":
		return f"{expr}.UIntValue"
	if kind in {"Float", "Double"}:
		return f"static_cast<{_cpp_lua_arg_type(kind)}>({expr}.FloatValue)"
	if kind == "String":
		return f"{expr}.StringValue"
	if kind == "ObjectRef":
		# Caller must wrap with LuaWrapObjectRef(state, ObjectRefFromPropertyValue(...))
		return f"ObjectRefFromPropertyValue({expr})"
	return f"{expr}.IntValue"


def render_lua_header(objects: list[FTypeEntry]) -> str:
	lines = [
		"//*****************************************************************************",
		"// LuaReflectBindings.gen.h — GENERATED by Tools/object_reflect_codegen.py.",
		"//*****************************************************************************",
		"#pragma once",
		"",
		'#include <Core/Object/Object.h>',
		"",
		"#define SOL_ALL_SAFETIES_ON 1",
		"#include <sol/sol.hpp>",
		"",
		"namespace Maho",
		"{",
		"",
		"/** Lua userdata handle for reflected UObject instances. */",
		"struct FLua_UObject",
		"{",
		"\tFObjectRef Ref;",
		"",
		"\t[[nodiscard]] bool IsValid() const { return Ref.IsValid(); }",
		"\t[[nodiscard]] UObject* GetRaw() const { return Ref ? Ref.operator->() : nullptr; }",
		"};",
		"",
	]
	for e in objects:
		if e.ShortName == "UObject":
			continue
		w = _lua_wrapper_name(e.ShortName)
		lines.append(f"struct {w} : FLua_UObject")
		lines.append("{")
		lines.append("};")
		lines.append("")

	lines += [
		"void RegisterGeneratedLuaObjectBindings(sol::state& Lua);",
		"",
		"/** Push the most-derived Lua usertype for an FObjectRef (never pushes FObjectRef itself). */",
		"MAHO_API [[nodiscard]] sol::object LuaWrapObjectRef(sol::state_view Lua, FObjectRef Ref);",
		"",
		"MAHO_API [[nodiscard]] FLua_UObject MakeLuaObject(FObjectRef Ref);",
	]
	for e in objects:
		if e.ShortName == "UObject":
			continue
		w = _lua_wrapper_name(e.ShortName)
		lines.append(f"MAHO_API [[nodiscard]] {w} MakeLua_{e.ShortName}(FObjectRef Ref);")
	lines += [
		"",
		"} // namespace Maho",
		"",
	]
	return "\n".join(lines)


def render_lua_cpp(objects: list[FTypeEntry]) -> str:
	includes: list[str] = []
	seen: set[str] = set()
	for e in objects:
		inc = e.IncludePath.replace("\\", "/")
		if inc and inc not in seen:
			seen.add(inc)
			includes.append(inc)

	lines: list[str] = [
		"//*****************************************************************************",
		"// LuaReflectBindings.gen.cpp — GENERATED by Tools/object_reflect_codegen.py.",
		"// Named sol2 usertypes from MAHO_OBJECT reflection metadata.",
		"//*****************************************************************************",
		"",
		'#include <LuaReflectBindings.gen.h>',
		"",
		"#define SOL_ALL_SAFETIES_ON 1",
		"#include <sol/sol.hpp>",
		"",
		'#include <Core/Object/ObjectReflect.h>',
		'#include <ObjectReflectTypes.gen.h>',
		"",
	]
	for inc in includes:
		lines.append(f'#include <{inc}>')
	lines += [
		"",
		"namespace Maho",
		"{",
		"",
		"MAHO_API FLua_UObject MakeLuaObject(FObjectRef Ref)",
		"{",
		"\treturn FLua_UObject{ std::move(Ref) };",
		"}",
		"",
	]
	for e in objects:
		if e.ShortName == "UObject":
			continue
		w = _lua_wrapper_name(e.ShortName)
		lines.append(f"MAHO_API {w} MakeLua_{e.ShortName}(FObjectRef Ref)")
		lines.append("{")
		lines.append(f"\t{w} Out;")
		lines.append("\tOut.Ref = std::move(Ref);")
		lines.append("\treturn Out;")
		lines.append("}")
		lines.append("")

	# Prefer most-derived wrap when known
	lines.append("MAHO_API sol::object LuaWrapObjectRef(sol::state_view Lua, FObjectRef Ref)")
	lines.append("{")
	lines.append("\tif (!Ref)")
	lines.append("\t{")
	lines.append("\t\treturn sol::lua_nil;")
	lines.append("\t}")

	def depth(e: FTypeEntry) -> int:
		d = 0
		cur = e
		names = {x.TypeName: x for x in objects}
		while cur.Super and cur.Super in names:
			d += 1
			cur = names[cur.Super]
		return d

	by_depth = sorted([e for e in objects if e.ShortName != "UObject"], key=depth, reverse=True)
	for e in by_depth:
		lines.append(f"\tif (Ref.Cast<{e.TypeName}>())")
		lines.append("\t{")
		lines.append(f"\t\treturn sol::make_object(Lua, MakeLua_{e.ShortName}(std::move(Ref)));")
		lines.append("\t}")
	lines.append("\treturn sol::make_object(Lua, MakeLuaObject(std::move(Ref)));")
	lines.append("}")
	lines.append("")

	lines.append("void RegisterGeneratedLuaObjectBindings(sol::state& Lua)")
	lines.append("{")
	lines.append("\tEnsureObjectReflectRegistered();")
	lines.append("")
	lines.append("\tsol::table MahoTable = Lua[\"maho\"];")
	lines.append("")

	# Register each usertype
	for e in objects:
		w = _lua_wrapper_name(e.ShortName)
		uname = _lua_usertype_name(e.ShortName)
		props = [m for m in e.Members if m.Kind == "property" and m.bSupported]
		funcs = [m for m in e.Members if m.Kind == "function" and m.bSupported]

		lines.append(f"\t// --- {e.TypeName} => maho.{uname} ---")
		if e.ShortName == "UObject" or not e.Super:
			lines.append(f"\tsol::usertype<{w}> UT_{e.ShortName} = Lua.new_usertype<{w}>(")
			lines.append(f'\t\t"{uname}",')
			lines.append("\t\tsol::no_constructor,")
			lines.append('\t\t"is_valid", &FLua_UObject::IsValid,')
			lines.append('\t\t"get_type", [](const FLua_UObject& Self) -> std::string')
			lines.append("\t\t{")
			lines.append("\t\t\tUObject* Obj = Self.GetRaw();")
			lines.append("\t\t\tif (!Obj || !Obj->GetObjectType().Name) { return {}; }")
			lines.append("\t\t\treturn Obj->GetObjectType().Name;")
			lines.append("\t\t}")
			lines.append("\t);")
		else:
			super_short = e.Super.rsplit("::", 1)[-1]
			super_w = _lua_wrapper_name(super_short)
			lines.append(f"\tsol::usertype<{w}> UT_{e.ShortName} = Lua.new_usertype<{w}>(")
			lines.append(f'\t\t"{uname}",')
			lines.append("\t\tsol::no_constructor,")
			lines.append(f"\t\tsol::base_classes, sol::bases<{super_w}>()")
			lines.append("\t);")

		for mem in props:
			snake = pascal_to_snake(mem.Name)
			kind = mem.PropertyType
			ret_cpp = _cpp_lua_arg_type(kind)
			lines.append(f"\tUT_{e.ShortName}[\"{snake}\"] = sol::property(")
			lines.append(f"\t\t[](const {w}& Self) -> sol::optional<{ret_cpp}>")
			lines.append("\t\t{")
			lines.append("\t\t\tUObject* Obj = Self.GetRaw();")
			lines.append("\t\t\tif (!Obj) { return sol::nullopt; }")
			lines.append("\t\t\tFPropertyValue Value;")
			lines.append(f'\t\t\tif (!Obj->GetPropertyValue("{mem.Name}", Value)) {{ return sol::nullopt; }}')
			lines.append(f"\t\t\treturn {_emit_return_from_property_value(kind, 'Value')};")
			lines.append("\t\t},")
			lines.append(f"\t\t[]({w}& Self, {ret_cpp} InValue)")
			lines.append("\t\t{")
			lines.append("\t\t\tUObject* Obj = Self.GetRaw();")
			lines.append("\t\t\tif (!Obj) { return; }")
			lines.append("\t\t\tFPropertyValue Packed;")
			_emit_pack_from_lua_arg(lines, "\t\t\t", "Packed", kind, "InValue")
			lines.append(f'\t\t\t(void)Obj->SetPropertyValue("{mem.Name}", Packed);')
			lines.append("\t\t}")
			lines.append("\t);")

		for mem in funcs:
			snake = pascal_to_snake(mem.Name)
			b_obj_ret = mem.bHasReturn and mem.ReturnType == "ObjectRef"
			arg_decls: list[str] = []
			for i, kind in enumerate(mem.ParamPropertyTypes):
				arg_decls.append(f"{_cpp_lua_arg_type(kind)} A{i}")
			if b_obj_ret:
				arg_decls.append("sol::this_state L")
			args_sig = ", ".join(arg_decls)

			if b_obj_ret:
				ret_sig = "sol::object"
			elif mem.bHasReturn:
				ret_sig = _cpp_lua_arg_type(mem.ReturnType)
			else:
				ret_sig = "void"

			if args_sig:
				lines.append(
					f"\tUT_{e.ShortName}[\"{snake}\"] = []({w}& Self, {args_sig})"
					+ (f" -> {ret_sig}" if mem.bHasReturn or b_obj_ret else "")
				)
			else:
				lines.append(
					f"\tUT_{e.ShortName}[\"{snake}\"] = []({w}& Self)"
					+ (f" -> {ret_sig}" if mem.bHasReturn else "")
				)
			lines.append("\t{")
			lines.append("\t\tUObject* Obj = Self.GetRaw();")
			if b_obj_ret:
				lines.append("\t\tif (!Obj) { return sol::lua_nil; }")
			elif mem.bHasReturn:
				default = "false" if mem.ReturnType == "Bool" else ("{}" if mem.ReturnType == "String" else "0")
				lines.append(f"\t\tif (!Obj) {{ return {default}; }}")
			else:
				lines.append("\t\tif (!Obj) { return; }")

			n = len(mem.ParamPropertyTypes)
			if n > 0:
				lines.append(f"\t\tFPropertyValue Pack[{n}];")
				for i, kind in enumerate(mem.ParamPropertyTypes):
					_emit_pack_from_lua_arg(lines, "\t\t", f"Pack[{i}]", kind, f"A{i}")
				pack_ptr = "Pack"
			else:
				pack_ptr = "static_cast<const FPropertyValue*>(nullptr)"

			if mem.bHasReturn:
				lines.append("\t\tFPropertyValue Out;")
				lines.append(
					f'\t\tif (!Obj->CallFunction("{mem.Name}", {pack_ptr}, {n}, &Out))'
				)
				lines.append("\t\t{")
				if b_obj_ret:
					lines.append("\t\t\treturn sol::lua_nil;")
				else:
					default = "false" if mem.ReturnType == "Bool" else ("{}" if mem.ReturnType == "String" else "0")
					lines.append(f"\t\t\treturn {default};")
				lines.append("\t\t}")
				if b_obj_ret:
					lines.append(
						"\t\treturn LuaWrapObjectRef(sol::state_view(L), ObjectRefFromPropertyValue(Out));"
					)
				else:
					lines.append(f"\t\treturn {_emit_return_from_property_value(mem.ReturnType, 'Out')};")
			else:
				lines.append(f'\t\t(void)Obj->CallFunction("{mem.Name}", {pack_ptr}, {n}, nullptr);')
			lines.append("\t};")

		lines.append(f'\tMahoTable["{uname}"] = UT_{e.ShortName};')
		lines.append("")

	lines.append('\tMahoTable["wrap_object"] = [](sol::this_state L, FLua_UObject Handle) -> sol::object')
	lines.append("\t{")
	lines.append("\t\treturn LuaWrapObjectRef(sol::state_view(L), std::move(Handle.Ref));")
	lines.append("\t};")
	lines.append("}")
	lines.append("")
	lines.append("} // namespace Maho")
	lines.append("")
	return "\n".join(lines)


def render_lua_api_toc(objects: list[FTypeEntry]) -> str:
	lines = [
		_LUA_HTML_TOC_BEGIN,
		'\t<div class="sec">Usertypes</div>',
		"\t<ul>",
	]
	for e in objects:
		uname = _lua_usertype_name(e.ShortName)
		lines.append(f'\t\t<li><a href="#maho.{uname}">maho.{uname}</a></li>')
	lines.append("\t</ul>")
	lines.append(_LUA_HTML_TOC_END)
	return "\n".join(lines)


def render_lua_api_body(objects: list[FTypeEntry]) -> str:
	lines = [
		_LUA_HTML_BODY_BEGIN,
		'<section class="card" id="usertypes">',
		"\t<h2>Usertypes（codegen）</h2>",
		'\t<div class="body">',
		"\t\t<p>由 <code>object_reflect_codegen.py</code> 根据 <code>MAHO_OBJECT</code> 扫描生成具名 sol2 绑定；"
		"方法/属性为 snake_case，对应 C++ 反射成员。"
		"实例可由 <code>maho.get_transient_package</code> / <code>find_*</code> / <code>create_*</code>（FResourceSystem::BindLua）"
		"或 C++ 推进 Lua；其它系统实现 <code>ILuaBindable</code> 经 <code>Bind</code> / <code>OnLuaReady</code> 挂接。</p>",
		"",
	]
	for e in objects:
		uname = _lua_usertype_name(e.ShortName)
		super_txt = ""
		if e.Super:
			super_short = e.Super.rsplit("::", 1)[-1]
			super_txt = f" · extends maho.{_lua_usertype_name(super_short)}"
		lines.append(f'\t\t<article class="fn" id="maho.{uname}">')
		lines.append('\t\t\t<div class="fn-head">')
		lines.append(f'\t\t\t\t<h3 class="fn-name">maho.{uname}</h3>')
		lines.append('\t\t\t\t<span class="tag tag-reflect">usertype</span>')
		lines.append("\t\t\t</div>")
		lines.append('\t\t\t<div class="fn-body">')
		lines.append(
			f'\t\t\t\t<code class="sig">{_html_escape(e.TypeName)}{super_txt}</code>'
		)
		props = [m for m in e.Members if m.Kind == "property" and m.bSupported]
		funcs = [m for m in e.Members if m.Kind == "function" and m.bSupported]
		if props:
			lines.append("\t\t\t\t<h4>Properties</h4>")
			lines.append("\t\t\t\t<table>")
			lines.append("\t\t\t\t\t<thead><tr><th>Lua</th><th>C++</th><th>Type</th></tr></thead>")
			lines.append("\t\t\t\t\t<tbody>")
			for m in props:
				lines.append(
					"\t\t\t\t\t\t<tr>"
					f"<td><code>{_html_escape(pascal_to_snake(m.Name))}</code></td>"
					f"<td><code>{_html_escape(m.Name)}</code></td>"
					f"<td><code>{_html_escape(m.PropertyType)}</code></td>"
					"</tr>"
				)
			lines.append("\t\t\t\t\t</tbody>")
			lines.append("\t\t\t\t</table>")
		if funcs:
			lines.append("\t\t\t\t<h4>Functions</h4>")
			lines.append("\t\t\t\t<table>")
			lines.append("\t\t\t\t\t<thead><tr><th>Lua</th><th>C++</th><th>Signature</th></tr></thead>")
			lines.append("\t\t\t\t\t<tbody>")
			for m in funcs:
				ret = m.CppType if m.bHasReturn else "void"
				sig = f"{ret}({', '.join(m.ParamTypes)})"
				lines.append(
					"\t\t\t\t\t\t<tr>"
					f"<td><code>{_html_escape(pascal_to_snake(m.Name))}</code></td>"
					f"<td><code>{_html_escape(m.Name)}</code></td>"
					f"<td><code>{_html_escape(sig)}</code></td>"
					"</tr>"
				)
			lines.append("\t\t\t\t\t</tbody>")
			lines.append("\t\t\t\t</table>")
		if not props and not funcs:
			lines.append("\t\t\t\t<p>本类无直接反射成员（方法见基类）。</p>")
		lines.append("\t\t\t</div>")
		lines.append("\t\t</article>")
		lines.append("")
	lines.append("\t</div>")
	lines.append("</section>")
	lines.append(_LUA_HTML_BODY_END)
	return "\n".join(lines)


def sync_lua_api_html(path: Path, objects: list[FTypeEntry]) -> bool:
	if not path.is_file():
		print(f"[WARN] skip Lua API html (missing): {path}")
		return False
	raw = path.read_text(encoding="utf-8")
	updated = _patch_marked_region(
		raw, _LUA_HTML_TOC_BEGIN, _LUA_HTML_TOC_END, render_lua_api_toc(objects)
	)
	updated = _patch_marked_region(
		updated, _LUA_HTML_BODY_BEGIN, _LUA_HTML_BODY_END, render_lua_api_body(objects)
	)
	updated_n = updated.replace("\r\n", "\n")
	raw_n = raw.replace("\r\n", "\n")
	if updated_n == raw_n:
		return False
	path.write_text(updated_n, encoding="utf-8", newline="\n")
	return True


def sync_api_html(
	path: Path,
	objects: list[FTypeEntry],
	structs: list[FTypeEntry],
	enums: list[FEnumEntry],
) -> bool:
	if not path.is_file():
		print(f"[WARN] skip API html (missing): {path}")
		return False
	raw = path.read_text(encoding="utf-8")
	updated = _patch_marked_region(
		raw, _HTML_TOC_BEGIN, _HTML_TOC_END, render_html_toc(objects, structs, enums)
	)
	updated = _patch_marked_region(
		updated, _HTML_BODY_BEGIN, _HTML_BODY_END, render_html_body(objects, structs, enums)
	)
	updated_n = updated.replace("\r\n", "\n")
	raw_n = raw.replace("\r\n", "\n")
	if updated_n == raw_n:
		return False
	path.write_text(updated_n, encoding="utf-8", newline="\n")
	return True


def main(argv: list[str]) -> int:
	parser = argparse.ArgumentParser(description="Maho object/struct/enum reflection codegen")
	parser.add_argument("--root", action="append", dest="roots", default=None)
	parser.add_argument("--out-h", type=Path, default=_DEFAULT_OUT_H)
	parser.add_argument("--out-cpp", type=Path, default=_DEFAULT_OUT_CPP)
	parser.add_argument("--out-json", type=Path, default=_DEFAULT_OUT_JSON)
	parser.add_argument("--out-html", type=Path, default=_DEFAULT_OUT_HTML)
	parser.add_argument("--out-lua-h", type=Path, default=_DEFAULT_OUT_LUA_H)
	parser.add_argument("--out-lua-cpp", type=Path, default=_DEFAULT_OUT_LUA_CPP)
	parser.add_argument("--out-lua-html", type=Path, default=_DEFAULT_OUT_LUA_HTML)
	parser.add_argument("--out-resource-h", type=Path, default=_DEFAULT_OUT_RESOURCE_H)
	parser.add_argument("--out-resource-cpp", type=Path, default=_DEFAULT_OUT_RESOURCE_CPP)
	parser.add_argument("--no-html", action="store_true", help="Do not sync ObjectReflectAPI.html / LuaAPI.html")
	parser.add_argument("--repo-root", type=Path, default=ENGINE_ROOT)
	parser.add_argument("--check", action="store_true")
	args = parser.parse_args(argv)

	roots = [Path(r) for r in args.roots] if args.roots else list(_DEFAULT_ROOTS)
	roots = [r if r.is_absolute() else (args.repo_root / r) for r in roots]

	try:
		objects, structs, enums = scan_roots(roots, args.repo_root)
		resource_types = scan_resource_types(roots, args.repo_root, objects)
	except ValueError as ex:
		print(f"[ERROR] {ex}", file=sys.stderr)
		return 1

	header = render_header()
	cpp = render_cpp(objects, structs, enums)
	js = render_json(objects, structs, enums)
	lua_h = render_lua_header(objects)
	lua_cpp = render_lua_cpp(objects)
	resource_h = render_resource_header()
	resource_cpp = render_resource_cpp(resource_types)

	print(
		f"[Maho] reflect scan: {len(objects)} object(s), {len(structs)} struct(s), "
		f"{len(enums)} enum(s) from {len(roots)} root(s)"
	)
	print(f"[Maho] resource scan: {len(resource_types)} TResourceIOTraits specialization(s)")

	outputs = (
		(args.out_h, header),
		(args.out_cpp, cpp),
		(args.out_json, js),
		(args.out_lua_h, lua_h),
		(args.out_lua_cpp, lua_cpp),
		(args.out_resource_h, resource_h),
		(args.out_resource_cpp, resource_cpp),
	)

	if args.check:
		ok = True
		for path, content in outputs:
			if not path.is_file() or path.read_text(encoding="utf-8") != content:
				print(f"[CHECK] stale: {path}")
				ok = False
			else:
				print(f"[CHECK] ok: {path}")
		return 0 if ok else 2

	changed = False
	for path, content in outputs:
		if write_if_changed(path, content):
			print(f"[Maho] wrote {path}")
			changed = True
		else:
			print(f"[Maho] up-to-date {path}")

	if not args.no_html:
		try:
			if sync_api_html(args.out_html, objects, structs, enums):
				print(f"[Maho] wrote {args.out_html}")
				changed = True
			elif args.out_html.is_file():
				print(f"[Maho] up-to-date {args.out_html}")
			if sync_lua_api_html(args.out_lua_html, objects):
				print(f"[Maho] wrote {args.out_lua_html}")
				changed = True
			elif args.out_lua_html.is_file():
				print(f"[Maho] up-to-date {args.out_lua_html}")
		except ValueError as ex:
			print(f"[ERROR] API html sync failed: {ex}", file=sys.stderr)
			return 1

	if not changed:
		print("[Maho] object reflect codegen: nothing changed")
	return 0


# ---------------------------------------------------------------------------
# Resource module registration (UResource subclasses + TResourceIOTraits)
# ---------------------------------------------------------------------------

_RE_RESOURCE_TRAITS = re.compile(
	r"template\s*<>\s*struct\s+TResourceIOTraits\s*<\s*((?:Maho::)?[A-Za-z_]\w*)\s*>"
)
_RE_GET_TYPE = re.compile(r"return\s+EResourceType::(\w+)\s*;")
_RE_TYPE_NAMES = re.compile(
	r"static\s+constexpr\s+const\s+char\s*\*\s*TypeNames\s*\[\s*\]\s*=\s*\{([^}]*)\}",
	re.DOTALL,
)
_RE_CLASS_POOL = re.compile(
	r"\bclass\s+(?:MAHO_API\s+)?(\w+)\s*(?:final\s*)?(?::[^{]+)?\{",
	re.DOTALL,
)


@dataclass
class FResourceSystemTypeEntry:
	TypeName: str  # Maho::UTextureResource
	ShortName: str
	EnumName: str  # Texture / Raw
	TypeNames: list[str] = field(default_factory=list)
	GCPooledSlots: int | None = None
	bAlreadyObjectPooled: bool = False
	IncludePath: str = ""


def _extract_brace_body(text: str, open_brace: int) -> str:
	depth = 0
	i = open_brace
	while i < len(text):
		ch = text[i]
		if ch == "{":
			depth += 1
		elif ch == "}":
			depth -= 1
			if depth == 0:
				return text[open_brace + 1 : i]
		i += 1
	return ""


def _parse_type_names_list(inner: str) -> list[str]:
	names: list[str] = []
	for m in re.finditer(r'"([^"]*)"', inner):
		names.append(m.group(1))
	return names


def _find_class_pool_size(text: str, short_name: str) -> int | None:
	for m in _RE_CLASS_POOL.finditer(text):
		if m.group(1) != short_name:
			continue
		brace = text.find("{", m.start())
		if brace < 0:
			continue
		body = _extract_brace_body(text, brace)
		pm = _RE_POOL_SIZE.search(body)
		if pm:
			return int(pm.group(1))
	return None


def scan_resource_types(
	roots: list[Path],
	repo_root: Path,
	objects: list[FTypeEntry],
) -> list[FResourceSystemTypeEntry]:
	object_pooled = {e.TypeName for e in objects if e.GCPooledSlots is not None}
	found: dict[str, FResourceSystemTypeEntry] = {}

	for root in roots:
		if not root.is_dir():
			continue
		for path in sorted(root.rglob("*")):
			if not path.is_file() or path.suffix.lower() not in _SOURCE_SUFFIXES:
				continue
			if any(part in _SKIP_DIR_NAMES for part in path.parts):
				continue
			raw = path.read_text(encoding="utf-8")
			text = _strip_comments(raw)
			try:
				rel = str(path.relative_to(repo_root)).replace("\\", "/")
			except ValueError:
				rel = path.name

			for m in _RE_RESOURCE_TRAITS.finditer(text):
				type_tok = m.group(1)
				short = _short_name(type_tok)
				qualified = type_tok if "::" in type_tok else f"Maho::{short}"

				brace = text.find("{", m.end())
				if brace < 0:
					raise ValueError(f"{rel}: TResourceIOTraits<{short}> missing body")
				body = _extract_brace_body(text, brace)

				gm = _RE_GET_TYPE.search(body)
				if not gm:
					raise ValueError(f"{rel}: TResourceIOTraits<{short}> missing GetType return")
				enum_name = gm.group(1)

				nm = _RE_TYPE_NAMES.search(body)
				if not nm:
					raise ValueError(f"{rel}: TResourceIOTraits<{short}> missing TypeNames[]")
				type_names = _parse_type_names_list(nm.group(1))
				if not type_names:
					raise ValueError(f"{rel}: TResourceIOTraits<{short}> TypeNames empty")

				pool = _find_class_pool_size(text, short)
				# Class may live in another header (e.g. Resource.h); search all later if needed.
				include_path = ""
				if "Public/" in rel.replace("\\", "/"):
					# Maho/Source/Public/Core/... → Core/...
					idx = rel.replace("\\", "/").find("/Public/")
					if idx >= 0:
						include_path = rel.replace("\\", "/")[idx + len("/Public/") :]
				elif "Private/" in rel.replace("\\", "/"):
					idx = rel.replace("\\", "/").find("/Private/")
					if idx >= 0:
						include_path = rel.replace("\\", "/")[idx + len("/Private/") :]

				entry = FResourceSystemTypeEntry(
					TypeName=qualified,
					ShortName=short,
					EnumName=enum_name,
					TypeNames=type_names,
					GCPooledSlots=pool,
					bAlreadyObjectPooled=qualified in object_pooled,
					IncludePath=include_path,
				)
				if short in found:
					# Merge pool size from class definition file if traits file had none.
					prev = found[short]
					if prev.GCPooledSlots is None and pool is not None:
						prev.GCPooledSlots = pool
					if not prev.TypeNames:
						prev.TypeNames = type_names
					continue
				found[short] = entry

	# Second pass: fill PoolSize from class headers when traits were in ResourceIO.h
	for root in roots:
		if not root.is_dir():
			continue
		for path in sorted(root.rglob("*")):
			if not path.is_file() or path.suffix.lower() not in _SOURCE_SUFFIXES:
				continue
			if any(part in _SKIP_DIR_NAMES for part in path.parts):
				continue
			text = _strip_comments(path.read_text(encoding="utf-8"))
			for short, entry in found.items():
				if entry.GCPooledSlots is not None:
					continue
				pool = _find_class_pool_size(text, short)
				if pool is not None:
					entry.GCPooledSlots = pool

	# Ensure UResource / known object pooled flags
	for entry in found.values():
		if entry.TypeName in object_pooled:
			entry.bAlreadyObjectPooled = True

	entries = list(found.values())
	# Non-Raw first, Raw last (importer fallback order).
	entries.sort(key=lambda e: (0 if e.EnumName == "Raw" else 1, e.TypeName), reverse=True)
	# Want Raw last: sort key Raw=0 others=1 then reverse → Raw last? 
	# reverse=True with Raw=0 → Raw comes last among zeros... Actually:
	# key (is_raw, name): is_raw True for Raw → sort False first then True last
	entries.sort(key=lambda e: (e.EnumName == "Raw", e.TypeName))
	return entries


def render_resource_header() -> str:
	return "\n".join(
		[
			"//*****************************************************************************",
			"// ResourceTypes.gen.h — GENERATED by Tools/object_reflect_codegen.py.",
			"//*****************************************************************************",
			"#pragma once",
			"",
			"#include <Core/Extension/Resource/Resource.h>",
			"",
			"#include <string>",
			"",
			"namespace Maho",
			"{",
			"",
			"class FGCSystem;",
			"class FResourceSystem;",
			"",
			"/** Register non-MAHO_OBJECT UResource pools onto GC (IO is explicit Import/Export templates). */",
			"void RegisterGeneratedResourceTypes(FResourceSystem& Manager, FGCSystem& GC);",
			"",
			"[[nodiscard]] EResourceType ResourceTypeFromString(const std::string& Name);",
			"[[nodiscard]] bool TryResourceTypeFromClassName(const std::string& ClassName, EResourceType& OutType);",
			"",
			"} // namespace Maho",
			"",
		]
	)


def render_resource_cpp(entries: list[FResourceSystemTypeEntry]) -> str:
	lines = [
		"//*****************************************************************************",
		"// ResourceTypes.gen.cpp — GENERATED by Tools/object_reflect_codegen.py.",
		"//*****************************************************************************",
		"",
		'#include <ResourceTypes.gen.h>',
		"",
		'#include "Core/Extension/Resource/ResourceIO.h"',
		'#include <Core/Extension/Resource/Resource.h>',
		'#include <Core/Extension/GC/GC.h>',
		"",
		"#include <cstring>",
		"",
		"namespace Maho",
		"{",
		"",
		"void RegisterGeneratedResourceTypes(FResourceSystem& Manager, FGCSystem& GC)",
		"{",
	]
	if not entries:
		lines.append("\t(void)Manager;")
		lines.append("\t(void)GC;")
	else:
		lines.append("\t(void)Manager;")
		for e in entries:
			if e.GCPooledSlots is not None and not e.bAlreadyObjectPooled:
				lines.append(
					f"\tGC.RegisterObjectType<{e.TypeName}>({e.TypeName}::PoolSize);"
				)
	lines.append("}")
	lines.append("")

	lines.append("namespace")
	lines.append("{")
	lines.append("[[nodiscard]] bool MatchTypeName(const char* const* Names, std::size_t Count, const std::string& Name)")
	lines.append("{")
	lines.append("\tfor (std::size_t Index = 0; Index < Count; ++Index)")
	lines.append("\t{")
	lines.append("\t\tif (Name == Names[Index])")
	lines.append("\t\t{")
	lines.append("\t\t\treturn true;")
	lines.append("\t\t}")
	lines.append("\t}")
	lines.append("\treturn false;")
	lines.append("}")
	lines.append("} // namespace")
	lines.append("")

	lines.append("EResourceType ResourceTypeFromString(const std::string& Name)")
	lines.append("{")
	lines.append("\tEResourceType Type = EResourceType::Unknown;")
	lines.append("\tif (TryResourceTypeFromClassName(Name, Type))")
	lines.append("\t{")
	lines.append("\t\treturn Type;")
	lines.append("\t}")
	lines.append("\treturn EResourceType::Unknown;")
	lines.append("}")
	lines.append("")

	lines.append("bool TryResourceTypeFromClassName(const std::string& ClassName, EResourceType& OutType)")
	lines.append("{")
	if not entries:
		lines.append("\t(void)ClassName;")
		lines.append("\t(void)OutType;")
		lines.append("\treturn false;")
	else:
		for e in entries:
			lines.append(f"\t{{")
			lines.append(f"\t\tusing FTraits = TResourceIOTraits<{e.TypeName}>;")
			lines.append(
				"\t\tconstexpr std::size_t Count = sizeof(FTraits::TypeNames) / sizeof(FTraits::TypeNames[0]);"
			)
			lines.append("\t\tif (MatchTypeName(FTraits::TypeNames, Count, ClassName))")
			lines.append("\t\t{")
			lines.append(f"\t\t\tOutType = EResourceType::{e.EnumName};")
			lines.append("\t\t\treturn true;")
			lines.append("\t\t}")
			lines.append("\t}")
		lines.append("\treturn false;")
	lines.append("}")
	lines.append("")
	lines.append("} // namespace Maho")
	lines.append("")
	return "\n".join(lines)


if __name__ == "__main__":
	raise SystemExit(main(sys.argv[1:]))
