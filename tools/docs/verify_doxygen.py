"""Reject incomplete public API output, including silent or stale filter failures."""

import argparse
from pathlib import Path
from xml.etree import ElementTree

from doxygen_filter import complete_documentation, functions


def public_api(include: Path) -> dict[str, set[str]]:
    """Require the currently authored, complete public declarations by header name."""
    expected = {}
    for path in sorted(include.glob("*.h")):
        names = {function.name for function in functions(path.read_bytes().decode("utf-8"))
                 if not function.definition and complete_documentation(function)}
        if names:
            expected[path.name] = names
    if not expected:
        raise ValueError("No complete public API declarations found for documentation verification")
    return expected


def verify(include: Path, xml: Path) -> int:
    """Require current indexed header members and their actual rendered contract text."""
    expected = public_api(include)
    index = ElementTree.parse(xml / "index.xml").getroot()
    headers = {Path(compound.findtext("name", "").replace("\\", "/")).name: compound
               for compound in index.findall("compound") if compound.get("kind") == "file"}
    count = 0
    for header, names in expected.items():
        compound = headers.get(header)
        if compound is None:
            raise ValueError(f"Missing current public API header documentation: {header}")
        indexed = {member.findtext("name") for member in compound.findall("member")
                   if member.get("kind") == "function"}
        if not names <= indexed:
            raise ValueError(f"Missing current public API members in {header}: {sorted(names - indexed)}")
        reference = compound.get("refid", "")
        if not reference or Path(reference).name != reference:
            raise ValueError(f"Invalid documentation compound reference for {header}")
        document = ElementTree.parse(xml / (reference + ".xml")).getroot()
        definition = document.find("compounddef")
        if (definition is None or definition.get("kind") != "file" or
                Path(definition.findtext("compoundname", "").replace("\\", "/")).name != header):
            raise ValueError(f"Mismatched public API header compound: {header}")
        members = {member.findtext("name"): member for member in definition.iter("memberdef")
                   if member.get("kind") == "function"}
        for name in names:
            member = members.get(name)
            if member is None:
                raise ValueError(f"Missing public API definition documentation: {header}: {name}")
            descriptions = [member.find(tag) for tag in ("briefdescription", "detaileddescription")]
            text = "".join("".join(description.itertext()) for description in descriptions
                           if description is not None).strip()
            if not text:
                raise ValueError(f"Empty public API documentation: {header}: {name}")
            count += 1
    return count


def main() -> None:
    """Verify documentation after Doxygen; failures propagate to the strict CMake gate."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include", required=True, type=Path)
    parser.add_argument("--xml", required=True, type=Path)
    args = parser.parse_args()
    count = verify(args.include, args.xml)
    print(f"Verified complete documentation for {count} public API declarations.")


if __name__ == "__main__":
    main()
