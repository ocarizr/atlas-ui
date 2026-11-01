"""Guard documentation coverage without generating or publishing reference pages."""

import pytest

from scripts.ci.documentation import check_documentation


@pytest.mark.parametrize(
    ("name", "source"),
    [
        ("example.py", '"""Module contract."""\ndef _helper():\n    pass\n'),
        ("example.hpp", "/** @file example.hpp\n * @brief Example API. */\nvoid run();\n"),
        ("example.proto", '/// Preview contract.\nsyntax = "proto3";\nmessage Hello {}\n'),
    ],
    ids=["private-python-function", "cpp-declaration", "proto-message"],
)
def test_missing_symbol_documentation_reports_location(tmp_path, name, source):
    """Private helpers and schema declarations need their own explanation."""
    path = tmp_path / name
    path.write_text(source, encoding="utf-8")
    errors = check_documentation(path)
    assert errors
    assert all(str(path) in error for error in errors)


def test_cpp_documents_templates_members_and_ignores_lambda_locals(tmp_path):
    """Associate comments with declarations rather than braces in strings or lambdas."""
    path = tmp_path / "example.hpp"
    path.write_text(
        "/**\n * @file example.hpp\n * @brief Example API.\n */\n"
        "/// Own a value.\ntemplate<class T> struct box {\n"
        "/// Stored value.\nT value;\n"
        '/// Read a value.\nT read() { auto callback=[] { return "}"; }; return value; }\n'
        "};\n",
        encoding="utf-8",
    )
    assert check_documentation(path) == []


def test_python_module_and_class_docs_do_not_cover_undocumented_methods(tmp_path):
    """Check each declaration even when its surrounding scope is documented."""
    path = tmp_path / "example.py"
    path.write_text(
        '"""Module."""\nclass Item:\n    """Item."""\n    def run(self):\n        pass\n'
    )
    assert any("run" in error for error in check_documentation(path))


def test_cpp_trailing_comments_do_not_document_the_next_symbol(tmp_path):
    """A documented member or namespace close must not hide the next missing contract."""
    path = tmp_path / "example.hpp"
    path.write_text(
        "/** @file example.hpp */\n"
        "namespace inner {} ///< Namespace.\n"
        "void missing();\n"
        "/// State.\nstruct value {\n"
        "int first; ///< First value.\n"
        "int second;\n};\n",
        encoding="utf-8",
    )
    errors = check_documentation(path)
    assert any("missing()" in error for error in errors)
    assert any("second" in error for error in errors)


def test_cpp_inline_struct_instance_and_analysis_annotations_keep_their_docs(tmp_path):
    """Recognize comments before a type-instance declaration and next-line suppression."""
    path = tmp_path / "example.hpp"
    path.write_text(
        "/** @file example.hpp */\n"
        "/// Scenario state.\nstruct state {\n/// Value.\nint value;\n} instance;\n"
        "/// Execute once.\n// NOLINTNEXTLINE(example-check)\nvoid run();\n",
        encoding="utf-8",
    )
    assert check_documentation(path) == []


def test_cpp_reference_returns_and_namespace_state_need_contracts(tmp_path):
    """Wrapped declarators and namespace variables must not escape reference coverage."""
    path = tmp_path / "example.hpp"
    path.write_text(
        "/** @file example.hpp */\nconst int& read();\nextern thread_local void* current;\n",
        encoding="utf-8",
    )
    errors = check_documentation(path)
    assert any("read()" in error for error in errors)
    assert any("current" in error for error in errors)
