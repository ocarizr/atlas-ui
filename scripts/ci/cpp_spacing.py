"""Preserve one visual boundary after complete C++ control statements.

clang-format handles indentation, braces and definition spacing. This supplementary
syntax-tree rule separates a completed control statement from its next sibling;
it never splits an if/else, try/catch or do/while statement.
"""

import tree_sitter_cpp
from tree_sitter import Language, Parser

CONTROL_STATEMENTS = {
    "if_statement",
    "for_statement",
    "for_range_loop",
    "while_statement",
    "do_statement",
    "switch_statement",
    "try_statement",
}


def separate_control_blocks(source):
    """Return source bytes with a blank line after nonfinal sibling control statements."""
    root = Parser(Language(tree_sitter_cpp.language())).parse(source).root_node
    lines = source.splitlines(keepends=True)
    boundaries = set()
    pending = [root]
    while pending:
        node = pending.pop()
        if node.type == "compound_statement":
            siblings = node.named_children
            for index, current in enumerate(siblings):
                following = next(
                    (
                        candidate
                        for candidate in siblings[index + 1 :]
                        if not (
                            candidate.type == "comment"
                            and candidate.start_point.row == current.end_point.row
                        )
                    ),
                    None,
                )
                boundary = current.end_point.row + 1
                if (
                    current.type in CONTROL_STATEMENTS
                    and following is not None
                    and following.start_point.row >= boundary
                    and boundary < len(lines)
                    and lines[boundary].strip()
                ):
                    boundaries.add(boundary)
        pending.extend(node.named_children)
    newline = b"\r\n" if b"\r\n" in source else b"\n"
    for boundary in sorted(boundaries, reverse=True):
        lines.insert(boundary, newline)
    return b"".join(lines)
