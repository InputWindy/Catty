# Shared CATTY_REFLECT_CLASS() body parser for reflect_codegen.py.
from __future__ import annotations

import re
from dataclasses import dataclass, field


@dataclass
class FScannedMember:
	Name: str
	Kind: str  # property | function
	DeclHead: str = ""  # signature head (for Lua bindability checks)


@dataclass
class FScannedClass:
	TypeName: str
	Members: list[FScannedMember] = field(default_factory=list)
	Attrs: str = ""
	SourceLine: int = 0
	Bases: list[str] = field(default_factory=list)


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


def _is_ident_start(ch: str) -> bool:
	return ch.isalpha() or ch == "_"


def _is_ident_char(ch: str) -> bool:
	return ch.isalnum() or ch == "_"


def _is_ident_boundary(text: str, index: int) -> bool:
	return not (index > 0 and _is_ident_char(text[index - 1]))


def _skip_ws(text: str, i: int) -> int:
	n = len(text)
	while i < n and text[i].isspace():
		i += 1
	return i


def _read_ident(text: str, i: int) -> tuple[str, int]:
	i = _skip_ws(text, i)
	if i >= len(text) or not _is_ident_start(text[i]):
		return "", i
	j = i + 1
	while j < len(text) and _is_ident_char(text[j]):
		j += 1
	return text[i:j], j


def _line_number(text: str, index: int) -> int:
	return text.count("\n", 0, index) + 1


def _enclosing_namespaces(text: str, pos: int) -> list[str]:
	stack: list[str | None] = []
	i = 0
	n = min(pos, len(text))
	while i < n:
		if text.startswith("namespace", i) and _is_ident_boundary(text, i):
			j = _skip_ws(text, i + len("namespace"))
			parts: list[str] = []
			while j < n:
				ident, j2 = _read_ident(text, j)
				if not ident:
					break
				parts.append(ident)
				j = _skip_ws(text, j2)
				if j < n and text.startswith("::", j):
					j += 2
					continue
				break
			j = _skip_ws(text, j)
			if j < n and text[j] == "{":
				stack.append("::".join(parts) if parts else "")
				i = j + 1
				continue
			i = j
			continue
		ch = text[i]
		if ch == "{":
			stack.append(None)
			i += 1
			continue
		if ch == "}":
			if stack:
				stack.pop()
			i += 1
			continue
		i += 1
	return [ns for ns in stack if ns]


def _skip_template_args(text: str, i: int) -> int:
	i = _skip_ws(text, i)
	if i >= len(text) or text[i] != "<":
		return i
	close = _find_matching(text, i, "<", ">")
	return close + 1 if close >= 0 else i


def _skip_brace_group(text: str, i: int) -> int:
	i = _skip_ws(text, i)
	if i >= len(text) or text[i] != "{":
		return i
	close = _find_matching(text, i, "{", "}")
	return close + 1 if close >= 0 else i


def _parse_public_members(body: str, class_name: str, *, default_public: bool) -> list[FScannedMember]:
	access = "public" if default_public else "private"
	members: list[FScannedMember] = []
	i = 0
	n = len(body)

	while i < n:
		i = _skip_ws(body, i)
		if i >= n:
			break

		matched_access = False
		for spec in ("public", "protected", "private"):
			if body.startswith(spec, i) and _is_ident_boundary(body, i):
				j = _skip_ws(body, i + len(spec))
				if j < n and body[j] == ":":
					access = spec
					i = j + 1
					matched_access = True
					break
		if matched_access:
			continue

		nested = False
		for kw in ("class", "struct", "enum", "union"):
			if body.startswith(kw, i) and _is_ident_boundary(body, i):
				j = i + len(kw)
				j = _skip_template_args(body, j)
				_, j = _read_ident(body, j)
				j = _skip_ws(body, j)
				if j < n and body[j] == ":":
					while j < n and body[j] not in "{;":
						j += 1
				if j < n and body[j] == "{":
					j = _skip_brace_group(body, j)
				elif j < n and body[j] == ";":
					j += 1
				i = j
				nested = True
				break
		if nested:
			continue

		skip_kw = False
		for kw in ("using", "typedef", "friend", "static_assert", "template"):
			if body.startswith(kw, i) and _is_ident_boundary(body, i):
				j = i + len(kw)
				while j < n:
					if body[j] == "{":
						j = _skip_brace_group(body, j)
						continue
					if body[j] == ";":
						j += 1
						break
					j += 1
				i = j
				skip_kw = True
				break
		if skip_kw:
			continue

		start = i
		paren_depth = 0
		angle_depth = 0
		j = i
		while j < n:
			ch = body[j]
			if ch == "<":
				angle_depth += 1
			elif ch == ">" and angle_depth > 0:
				angle_depth -= 1
			elif ch == "(" and angle_depth == 0:
				paren_depth += 1
			elif ch == ")" and paren_depth > 0:
				paren_depth -= 1
			elif ch == "{" and paren_depth == 0 and angle_depth == 0:
				j = _skip_brace_group(body, j)
				j = _skip_ws(body, j)
				if j < n and body[j] == ";":
					j += 1
				break
			elif ch == ";" and paren_depth == 0 and angle_depth == 0:
				j += 1
				break
			j += 1

		decl = body[start:j].strip()
		i = j
		if not decl or access != "public":
			continue

		core = decl[:-1].strip() if decl.endswith(";") else decl
		brace_idx = core.find("{")
		head = core if brace_idx < 0 else core[:brace_idx].strip()

		if re.search(r"\bstatic\b", head):
			continue
		if "operator" in head or "~" in head:
			continue

		name = ""
		kind = "property"
		if "(" in head:
			kind = "function"
			before_paren = head[: head.index("(")].rstrip()
			m = re.search(r"([A-Za-z_]\w*)\s*$", before_paren)
			if m:
				name = m.group(1)
		else:
			no_bit = re.split(r"\s*:\s*\d+\s*$", head)[0]
			no_init = no_bit.split("=")[0].strip()
			m = re.search(r"([A-Za-z_]\w*)\s*$", no_init)
			if m:
				name = m.group(1)

		if not name or name == class_name:
			continue
		if name in {"if", "for", "while", "switch", "return", "sizeof"}:
			continue

		members.append(FScannedMember(Name=name, Kind=kind, DeclHead=head))

	seen: set[str] = set()
	unique: list[FScannedMember] = []
	for mem in members:
		if mem.Name in seen:
			continue
		seen.add(mem.Name)
		unique.append(mem)
	return unique


_RE_CLASS_MACRO = re.compile(r"\bCATTY_REFLECT_CLASS\s*\(")
_RE_CLASS_OR_STRUCT = re.compile(r"\b(class|struct)\b")


def scan_reflect_classes(text: str) -> list[FScannedClass]:
	"""Find CATTY_REFLECT_CLASS() + following class/struct; return scanned types."""
	out: list[FScannedClass] = []
	for macro_match in _RE_CLASS_MACRO.finditer(text):
		# Skip #define CATTY_REFLECT_CLASS
		line_start = text.rfind("\n", 0, macro_match.start()) + 1
		prefix = text[line_start : macro_match.start()].lstrip()
		if prefix.startswith("#"):
			continue

		open_paren = text.find("(", macro_match.start())
		close = _find_matching(text, open_paren, "(", ")")
		if close < 0:
			raise ValueError("Unclosed CATTY_REFLECT_CLASS(")
		attrs_text = text[open_paren + 1 : close]
		attrs = ", ".join(_split_top_level_args(attrs_text)) if attrs_text.strip() else ""
		after_macro = close + 1

		i = _skip_ws(text, after_macro)
		if text.startswith("template", i) and _is_ident_boundary(text, i):
			raise ValueError("CATTY_REFLECT_CLASS on template types is not supported yet")

		cm = _RE_CLASS_OR_STRUCT.match(text, i)
		if not cm:
			raise ValueError("CATTY_REFLECT_CLASS must be followed by class/struct")
		keyword = cm.group(1)
		j = cm.end()
		j = _skip_template_args(text, j)
		# Skip export macros / attributes between `class` and the type name:
		#   class CATTY_API FObject
		#   class [[nodiscard]] Foo
		class_name = ""
		while True:
			j = _skip_ws(text, j)
			if j < len(text) and text.startswith("[[", j):
				close = text.find("]]", j + 2)
				if close < 0:
					raise ValueError("Unclosed [[ attribute")
				j = close + 2
				continue
			if text.startswith("__declspec", j):
				j = _skip_ws(text, j + len("__declspec"))
				if j < len(text) and text[j] == "(":
					close = _find_matching(text, j, "(", ")")
					j = close + 1 if close >= 0 else j
				continue
			ident, j2 = _read_ident(text, j)
			if not ident:
				break
			# Common linkage / API macros — not the type name.
			if ident in {"CATTY_API", "DLLIMPORT", "DLLEXPORT"}:
				j = j2
				continue
			class_name = ident
			j = j2
			break
		if not class_name:
			raise ValueError("CATTY_REFLECT_CLASS: missing type name")

		j = _skip_ws(text, j)
		if text.startswith("final", j) and _is_ident_boundary(text, j):
			j = _skip_ws(text, j + 5)

		bases: list[str] = []
		if j < len(text) and text[j] == ":":
			j += 1
			while j < len(text) and text[j] != "{":
				j = _skip_ws(text, j)
				# optional public/protected/private / virtual
				for kw in ("public", "protected", "private", "virtual"):
					if text.startswith(kw, j) and _is_ident_boundary(text, j):
						j = _skip_ws(text, j + len(kw))
				# base name possibly qualified
				parts: list[str] = []
				while j < len(text):
					ident, j2 = _read_ident(text, j)
					if not ident:
						break
					parts.append(ident)
					j = _skip_ws(text, j2)
					if j < len(text) and text.startswith("::", j):
						j += 2
						continue
					break
				j = _skip_template_args(text, j)
				if parts:
					bases.append("::".join(parts))
				j = _skip_ws(text, j)
				if j < len(text) and text[j] == ",":
					j += 1
					continue
				break

		j = _skip_ws(text, j)
		if j >= len(text) or text[j] != "{":
			raise ValueError(f"CATTY_REFLECT_CLASS({class_name}): expected class body '{{'")

		body_close = _find_matching(text, j, "{", "}")
		if body_close < 0:
			raise ValueError(f"CATTY_REFLECT_CLASS({class_name}): unclosed class body")
		body = text[j + 1 : body_close]

		namespaces = _enclosing_namespaces(text, macro_match.start())
		qualified = "::".join([*namespaces, class_name]) if namespaces else class_name
		# Qualify relative base names with the same enclosing namespaces when needed.
		qualified_bases: list[str] = []
		for base in bases:
			if "::" in base:
				qualified_bases.append(base)
			elif namespaces:
				qualified_bases.append("::".join([*namespaces, base]))
			else:
				qualified_bases.append(base)

		members = _parse_public_members(body, class_name, default_public=(keyword == "struct"))
		out.append(
			FScannedClass(
				TypeName=qualified,
				Members=members,
				Attrs=attrs,
				SourceLine=_line_number(text, macro_match.start()),
				Bases=qualified_bases,
			)
		)
	return out
