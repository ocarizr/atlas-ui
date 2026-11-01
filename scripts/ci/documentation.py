"""Check Python docstrings and adjacent C++/Protobuf reference comments.

This is a source-coverage check, not a documentation generator or a semantic proof
that a comment is accurate. Compiler tests and review remain authoritative.
"""

import ast
import re
from pathlib import Path

import tree_sitter_cpp
from tree_sitter import Language, Parser


def cpp_declarations(source):
    """Yield documentable declarations and their template-aware comment attachment points."""
    parser = Parser(Language(tree_sitter_cpp.language()))
    tree = parser.parse(source)

    def visit(node):
        """Visit named declarations while keeping template wrappers as documentation anchors."""
        target = node.parent if node.parent and node.parent.type == "template_declaration" else node
        if target.parent and target.parent.type == "field_declaration":
            target = target.parent
        if (
            node.type in {"class_specifier", "struct_specifier", "enum_specifier"}
            and target.parent
            and target.parent.type == "declaration"
        ):
            target = target.parent
        if node.type in {
            "class_specifier",
            "struct_specifier",
            "enum_specifier",
            "enumerator",
            "alias_declaration",
            "concept_definition",
            "function_definition",
            "field_declaration",
            "preproc_def",
            "preproc_function_def",
        }:
            yield node, target
        elif node.type == "declaration":
            declarator = node.child_by_field_name("declarator")
            parent = node.parent
            local = False
            while parent:
                local |= parent.type in {
                    "compound_statement",
                    "function_definition",
                    "lambda_expression",
                }
                parent = parent.parent
            if not local or (declarator and "function_declarator" in declarator.type):
                yield node, target
        for child in node.named_children:
            yield from visit(child)

    yield from visit(tree.root_node)


def cpp_has_comment(node, source):
    """Recognize leading Doxygen comments or same-line trailing member documentation."""
    previous = node.prev_named_sibling
    while (
        previous
        and previous.type == "comment"
        and b"NOLINT" in source[previous.start_byte : previous.end_byte]
    ):
        previous = previous.prev_named_sibling
    if previous and previous.type == "comment":
        text = source[previous.start_byte : previous.end_byte].decode("utf-8")
        prefix = source[source.rfind(b"\n", 0, previous.start_byte) + 1 : previous.start_byte]
        if (
            not prefix.strip()
            and not text.startswith(("///<", "//!<"))
            and text.startswith(("///", "//!", "/**", "/*!"))
            and "@file" not in text
        ):
            return bool(text.strip("/*! \r\n"))
    following = node.next_named_sibling
    if (
        following
        and following.type == "comment"
        and following.start_point.row == node.end_point.row
    ):
        return source[following.start_byte : following.end_byte].startswith((b"///<", b"//!<"))
    return False


def python_errors(path, text):
    """Require module, class and callable docstrings, including private/nested helpers."""
    tree = ast.parse(text, filename=str(path))
    errors = []
    for node in ast.walk(tree):
        if isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)):
            if not (ast.get_docstring(node) or "").strip():
                errors.append(
                    f"{path}:{getattr(node, 'lineno', 1)}: missing docstring for {getattr(node, 'name', 'module')}"
                )
    return errors


def proto_errors(path, text):
    """Require schema and declaration comments without interpreting wire-format semantics."""
    errors = []
    previous = ""
    in_comment = False
    for line, value in enumerate(text.splitlines(), 1):
        stripped = value.strip()
        if stripped.startswith(("//", "/*")) or in_comment:
            previous = "comment"
            in_comment = "*/" not in stripped and (in_comment or stripped.startswith("/*"))
            continue
        if not stripped:
            continue
        declaration = re.match(
            r"(?:syntax\s*=|(?:message|enum|service|rpc|oneof)\s+|(?:repeated\s+|optional\s+)?[\w.<> ,]+\s+\w+\s*=)",
            stripped,
        )
        if declaration and previous != "comment":
            errors.append(f"{path}:{line}: missing schema documentation: {stripped}")
        previous = stripped
    return errors


def check_documentation(path):
    """Return location-bearing diagnostics for one owned source file."""
    path = Path(path)
    source = path.read_bytes()
    text = source.decode("utf-8")
    if path.suffix == ".py":
        return python_errors(path, text)
    if path.suffix == ".proto":
        return proto_errors(path, text)
    errors = []
    if not re.search(r"(?:/\*\*[\s*]*|///\s*|//!\s*)@file\b", text):
        errors.append(f"{path}:1: missing Doxygen @file documentation")
    seen = set()
    for node, target in cpp_declarations(source):
        if target.start_byte in seen:
            continue
        seen.add(target.start_byte)
        if not cpp_has_comment(target, source):
            summary = source[node.start_byte : node.end_byte].decode("utf-8").splitlines()[0][:100]
            errors.append(
                f"{path}:{target.start_point.row + 1}: missing Doxygen comment: {summary}"
            )
    return errors
