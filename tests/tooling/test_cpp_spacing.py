"""Exercise the supplementary control-block spacing rule independently of C++ compilation."""

import pytest

from scripts.ci.cpp_spacing import separate_control_blocks


@pytest.mark.parametrize(
    "statement",
    [
        "if (ready) { run(); } else { recover(); }",
        "for (auto value : values) { use(value); }",
        "while (ready) { run(); }",
        "do { run(); } while (ready);",
        "try { run(); } catch (...) { recover(); }",
    ],
    ids=["if-else", "range-for", "while", "do-while", "try-catch"],
)
def test_control_block_is_separated_from_the_next_statement(statement):
    """Separate the complete statement without splitting its else/catch/while clauses."""
    before = f"void run() {{\n{statement}\nfinish();\n}}\n".encode()
    expected = f"void run() {{\n{statement}\n\nfinish();\n}}\n".encode()
    assert separate_control_blocks(before) == expected
    assert separate_control_blocks(expected) == expected


def test_strings_and_last_statements_do_not_create_spurious_blank_lines():
    """Parse syntax rather than counting braces inside strings or comments."""
    source = b'void run() {\nauto text = "if(x) { }";\nif (ready) { finish(); }\n}\n'
    assert separate_control_blocks(source) == source


def test_trailing_control_comment_stays_with_its_block():
    """A same-line explanation must not hide the boundary before the next statement."""
    source = b"void run() {\nif (ready) { finish(); } // Retain this explanation.\nnext();\n}\n"
    expected = source.replace(b"\nnext();", b"\n\nnext();")
    assert separate_control_blocks(source) == expected
