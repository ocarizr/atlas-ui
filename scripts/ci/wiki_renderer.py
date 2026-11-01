"""Build per-library API pages from Doxygen's documented XML output format."""

import re
import xml.etree.ElementTree as ET
from pathlib import Path

from scripts.ci.wiki_markup import description, plain


def identifier(value):
    """Reject identifiers that cannot safely form a flat GitHub Wiki page or anchor."""
    if not re.fullmatch(r"[A-Za-z0-9_-]+", value):
        raise ValueError(f"Invalid Doxygen identifier: {value}")
    return value


def source_link(element, repository, revision):
    """Link declarations to their exact source revision without local filesystem paths."""
    location = element.find("location")
    if location is None:
        return ""
    file = location.get("file", "").replace("\\", "/")
    if not file.startswith("libs/") or ".." in Path(file).parts:
        return ""
    return f"[Source]({repository}/blob/{revision}/{file}#L{location.get('line', '1')})\n\n"


def documentation(element, links):
    """Join brief and detailed contracts without inventing missing reference prose."""
    return (
        "\n\n".join(
            text
            for tag in ("briefdescription", "detaileddescription")
            if (text := description(element.find(tag), links).strip())
        )
        + "\n\n"
    )


def template_prefix(element):
    """Retain template parameters and defaults that are essential to calling generic APIs."""
    parameters = []
    for parameter in element.findall("templateparamlist/param"):
        text = plain(parameter.find("type"))
        name = plain(parameter.find("declname"))
        if name:
            text += f" {name}"
        default = plain(parameter.find("defval"))
        if default:
            text += f" = {default}"
        parameters.append(text)
    return f"template <{', '.join(parameters)}>\n" if parameters else ""


def member_page(member, links, repository, revision):
    """Render callable signatures, member data and enumerator documentation in place."""
    anchor = identifier(member.get("id"))
    signature = plain(member.find("definition")) + plain(member.find("argsstring"))
    if not signature:
        signature = plain(member.find("name"))
    result = f'<a name="{anchor}"></a>\n\n### `{plain(member.find("name"))}`\n\n'
    signature = template_prefix(member) + signature
    constraint = plain(member.find("requiresclause")).strip()
    if constraint:
        signature += f"\nrequires {constraint}"
    initializer = plain(member.find("initializer")).strip()
    if initializer:
        signature += f" {initializer}"
    result += f"```cpp\n{signature}\n```\n\n"
    result += source_link(member, repository, revision) + documentation(member, links)
    for value in member.findall("enumvalue"):
        result += f'<a name="{identifier(value.get("id"))}"></a>\n\n'
        result += f"#### `{plain(value.find('name'))}`\n\n" + documentation(value, links)
    return result


def render_library(library, xml_directory, repository, revision):
    """Render deterministic compound pages and a library index, failing on missing XML."""
    identifier(library)
    directory = Path(xml_directory)
    index = ET.parse(directory / "index.xml").getroot()
    compounds = []
    for entry in index.findall("compound"):
        if entry.get("kind") in {"dir"}:
            continue
        refid = identifier(entry.get("refid"))
        compound = ET.parse(directory / f"{refid}.xml").getroot().find("compounddef")
        if compound is None:
            raise ValueError(f"Missing compound definition: {refid}")
        compounds.append((refid, compound))
    if not compounds:
        raise ValueError(f"No Doxygen API compounds for {library}")
    compounds.sort(key=lambda item: (item[1].get("kind") in {"file", "namespace"}, item[0]))
    links = {}
    for refid, compound in compounds:
        page = f"api-{library}-{refid}"
        links[refid] = page
        for member in compound.findall(".//memberdef"):
            if member.get("prot") == "private":
                continue
            links.setdefault(member.get("id"), f"{page}#{identifier(member.get('id'))}")
            for value in member.findall("enumvalue"):
                links.setdefault(value.get("id"), f"{page}#{identifier(value.get('id'))}")
    pages = {}
    overview = f"# {library}\n\n[All libraries](Home)\n\n"
    overview += f"[Library source]({repository}/tree/{revision}/libs/{library})\n\n"
    for refid, compound in sorted(compounds):
        name = plain(compound.find("compoundname"))
        overview += f"- [{name}]({links[refid]}) ({compound.get('kind')})\n"
        body = f"# {name}\n\n[Library index](library-{library})\n\n"
        prefix = template_prefix(compound)
        initializer = plain(compound.find("initializer")).strip()
        if initializer:
            body += f"```cpp\n{initializer}\n```\n\n"
        elif prefix:
            body += f"```cpp\n{prefix}{compound.get('kind')} {name}\n```\n\n"
        body += source_link(compound, repository, revision) + documentation(compound, links)
        for relation in compound.findall("basecompoundref") + compound.findall("innerclass"):
            target = links.get(relation.get("refid"))
            if target:
                body += f"- [{plain(relation)}]({target})\n\n"
        for section in compound.findall("sectiondef"):
            members = [
                member for member in section.findall("memberdef") if member.get("prot") != "private"
            ]
            if not members:
                continue
            body += f"## {section.get('kind', 'Members').replace('-', ' ').title()}\n\n"
            for member in members:
                body += member_page(member, links, repository, revision)
        pages[f"{links[refid]}.md"] = body
    pages[f"library-{library}.md"] = overview
    return pages
