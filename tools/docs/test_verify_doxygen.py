"""Public API verification must reject missing, empty, malformed and stale output."""

import tempfile
import unittest
from pathlib import Path
from xml.etree import ElementTree

from verify_doxygen import verify


class PublicApiVerificationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.include = self.root / "include"
        self.xml = self.root / "xml"
        self.include.mkdir()
        self.xml.mkdir()
        (self.include / "api.h").write_text('''/** @brief Resolve a value.
 * @param value Requested value.
 * @return Resolved value. */
int lookup(int value);
''', encoding="utf-8")
        self.index("api.h", "lookup")
        (self.xml / "api.xml").write_text('''<doxygen><compounddef kind="file"><compoundname>api.h</compoundname>
<sectiondef><memberdef kind="function"><name>lookup</name>
<briefdescription><para>Resolve a value.</para></briefdescription>
<detaileddescription /></memberdef></sectiondef>
</compounddef></doxygen>''', encoding="utf-8")

    def index(self, header, member):
        (self.xml / "index.xml").write_text(f'''<doxygenindex>
<compound kind="file" refid="api"><name>{header}</name>
<member kind="function" refid="api_member"><name>{member}</name></member>
</compound></doxygenindex>''', encoding="utf-8")

    def test_complete_current_header_is_accepted(self):
        self.assertEqual(verify(self.include, self.xml), 1)

    def test_wrong_header_cannot_substitute_for_public_api(self):
        self.index("private.h", "lookup")
        with self.assertRaisesRegex(ValueError, "Missing current public API header"):
            verify(self.include, self.xml)

    def test_stale_compound_cannot_substitute_for_current_index_members(self):
        self.index("api.h", "unrelated")
        with self.assertRaisesRegex(ValueError, "Missing current public API members"):
            verify(self.include, self.xml)

    def test_empty_contract_is_rejected(self):
        path = self.xml / "api.xml"
        path.write_text(path.read_text().replace("<para>Resolve a value.</para>", ""), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Empty public API documentation"):
            verify(self.include, self.xml)

    def test_wrong_compound_cannot_substitute_for_indexed_header(self):
        path = self.xml / "api.xml"
        path.write_text(path.read_text().replace("<compoundname>api.h</compoundname>",
                                                "<compoundname>private.h</compoundname>"), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Mismatched public API header compound"):
            verify(self.include, self.xml)

    def test_indexed_member_requires_actual_member_documentation(self):
        path = self.xml / "api.xml"
        path.write_text(path.read_text().replace("<name>lookup</name>", "<name>unrelated</name>"), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Missing public API definition documentation"):
            verify(self.include, self.xml)

    def test_missing_and_malformed_xml_are_rejected(self):
        path = self.xml / "index.xml"
        path.unlink()
        with self.assertRaises(FileNotFoundError):
            verify(self.include, self.xml)
        path.write_text("<broken>", encoding="utf-8")
        with self.assertRaises(ElementTree.ParseError):
            verify(self.include, self.xml)

    def test_empty_expected_api_is_rejected(self):
        (self.include / "api.h").write_text("int lookup(int value);", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "No complete public API"):
            verify(self.include, self.xml)


if __name__ == "__main__":
    unittest.main()
