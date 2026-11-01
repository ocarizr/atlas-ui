"""Convert Doxygen description elements to readable GitHub Wiki Markdown."""

from html import escape


def plain(element):
    """Read XML text without losing template punctuation or source-code whitespace."""
    return "" if element is None else "".join(element.itertext())


def code_text(element):
    """Recover spaces represented as empty XML nodes in syntax-highlighted code examples."""
    if element.tag == "sp":
        return " "
    return (element.text or "") + "".join(
        code_text(child) + (child.tail or "") for child in element
    )


def description(element, links):
    """Preserve prose, code, links, lists, parameter contracts and diagnostic sections."""
    if element is None:
        return ""
    body = escape(element.text or "", quote=False)
    for child in element:
        body += markup(child, links) + escape(child.tail or "", quote=False)
    return body


def markup(element, links):
    """Render one XML node, retaining text from unrecognized Doxygen containers."""
    tag = element.tag
    body = description(element, links)
    if tag == "ref":
        target = links.get(element.get("refid"))
        return f"[{body}]({target})" if target else f"`{plain(element)}`"
    if tag in {"computeroutput", "formula"}:
        return f"`{plain(element)}`"
    if tag == "bold":
        return f"**{body}**"
    if tag == "emphasis":
        return f"*{body}*"
    if tag == "ulink":
        return f"[{body}]({element.get('url')})"
    if tag in {"programlisting", "verbatim"}:
        code = "\n".join(code_text(line) for line in element.findall("codeline"))
        return f"\n\n```cpp\n{code or plain(element)}\n```\n\n"
    if tag == "sp":
        return " "
    if tag in {"para", "itemizedlist", "orderedlist", "parameterlist"}:
        return body.strip() + "\n\n"
    if tag == "listitem":
        return f"- {body.strip()}\n"
    if tag == "parameteritem":
        names = ", ".join(plain(name) for name in element.findall(".//parametername"))
        contract = description(element.find("parameterdescription"), links).strip()
        return f"- **{names}**: {contract}\n"
    if tag == "simplesect":
        return f"\n\n**{element.get('kind', 'note').capitalize()}:** {body.strip()}\n\n"
    if tag == "linebreak":
        return "\n"
    if tag == "title":
        return f"\n\n### {body}\n\n"
    return body
