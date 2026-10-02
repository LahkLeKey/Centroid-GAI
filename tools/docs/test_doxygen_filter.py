"""Documentation filters must preserve coverage, diagnostics and original source offsets."""

import subprocess
import os
import html
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from xml.etree import ElementTree

import doxygen_filter as docs


DOXYGEN = os.environ.get("CGAI_DOXYGEN_EXECUTABLE") or shutil.which("doxygen")
ROOT = Path(__file__).resolve().parents[2]


def smoke_fixture(root: Path) -> Path:
    """Create one public/private pair and a real filter beneath paths containing spaces."""
    for directory in ("include", "src", "tools/docs"):
        (root / directory).mkdir(parents=True)
    (root / "include/api.h").write_text('''/** @file api.h */
/** @brief Canonical public marker.
 * @param id Requested ID.
 * @return Selected ID. */
int public_lookup(int id);
''', encoding="utf-8")
    (root / "src/private.h").write_text('''/** @file private.h */
/** @brief Private declaration marker. */
int private_lookup(int id);
''', encoding="utf-8")
    (root / "src/implementation.c").write_text('''/** @file implementation.c */
#include "private.h"
#include "../include/api.h"
/** @brief Original public source marker.
 * @param id Input ID.
 * @return Selected value. */
int public_lookup(int id) { return private_lookup(id); }
/** @brief Canonical private marker.
 * @param id Input ID.
 * @return Selected value. */
int private_lookup(int id) { return id; }
''', encoding="utf-8")
    script = root / "tools/docs/doxygen_filter.py"
    shutil.copyfile(docs.__file__, script)
    return script


def smoke_configuration(root: Path, script: Path) -> Path:
    """Use the real Doxyfile input-filter syntax and the platform's shell launcher."""
    command = ('call ' if os.name == "nt" else '') + f'"{Path(sys.executable).as_posix()}" "{script.as_posix()}"'
    template = (ROOT / "Doxyfile.in").read_text(encoding="utf-8")
    filter_line = next(line for line in template.splitlines() if line.startswith("INPUT_FILTER"))
    filter_line = filter_line.replace("@DOXYGEN_INPUT_FILTER@", command.replace('"', '\\"'))
    configuration = root / "Doxyfile"
    configuration.write_text(f'''PROJECT_NAME = FilterSmoke
OUTPUT_DIRECTORY = "{(root / 'output').as_posix()}"
INPUT = "{(root / 'include').as_posix()}" "{(root / 'src').as_posix()}"
FILE_PATTERNS = *.h *.c
RECURSIVE = YES
{filter_line}
FILTER_SOURCE_FILES = NO
STRIP_CODE_COMMENTS = NO
EXTRACT_ALL = NO
EXTRACT_STATIC = YES
WARN_IF_UNDOCUMENTED = YES
WARN_NO_PARAMDOC = YES
WARN_AS_ERROR = YES
QUIET = YES
GENERATE_XML = YES
GENERATE_HTML = YES
GENERATE_LATEX = NO
SOURCE_BROWSER = YES
HAVE_DOT = NO
''', encoding="utf-8")
    return configuration


def run_documentation(configuration: Path) -> subprocess.CompletedProcess[str]:
    """Execute the actual CMake docs gate with bounded time and captured diagnostics."""
    return subprocess.run(["cmake", f"-DDOXYGEN_EXECUTABLE={DOXYGEN}",
                           f"-DDOXYGEN_CONFIG={configuration}",
                           f"-DPython3_EXECUTABLE={sys.executable}",
                           f"-DDOXYGEN_INPUT_DIR={configuration.parent / 'include'}",
                           f"-DDOXYGEN_XML_DIR={configuration.parent / 'output/xml'}",
                           "-P", str(ROOT / "cmake/DoxygenRun.cmake")],
                          capture_output=True, text=True, timeout=60)


class DoxygenFilterTests(unittest.TestCase):
    def test_public_definition_keeps_text_and_offsets(self):
        source = "/** @brief Resolve.\r\n * @param id Value.\r\n * @return Text. */\r\nconst char *lookup(int id) { return 0; }\r\n"
        result = docs.filter_source(source, {("lookup", ("id",))})
        self.assertEqual(result, source[:2] + " " + source[3:])
        self.assertEqual(len(result), len(source))
        self.assertEqual(result.splitlines(keepends=True)[1:], source.splitlines(keepends=True)[1:])

    def test_private_static_and_test_documentation_remains(self):
        source = "/** @brief Private. @param value Input. @return Output. */\nint private_call(int value) { return value; }\n"
        static = source.replace("int private_call", "static int public_call")
        test = source.replace("private_call", "test_call")
        declarations = {("public_call", ("value",))}
        for original in (source, static, test):
            self.assertEqual(docs.filter_source(original, declarations), original)

    def test_strings_macros_fields_and_file_comments_are_not_functions(self):
        source = '''const char *text = "/** @brief Fake. */ int public_call(int value) {";
/** @file sample.c */
#define public_call(value) (value)
/** @brief Field. */
struct sample { int public_call; };
/* Ordinary */ int public_call(int value) { return value; }
'''
        self.assertEqual(docs.filter_source(source, {("public_call", ("value",))}), source)

    def test_only_complete_public_prototypes_are_canonical(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "api.h"
            path.write_text('''/** @brief Full. @param value Input. @return Output. */
int complete(int value);
/** @brief Missing parameter. @return Output. */
int incomplete(int value);
/** @brief Missing return. @param value Input. */
int no_return(int value);
/** @brief Duplicate. @param value First. @param value Second. @return Output. */
int duplicate(int value);
/** @brief Cleanup. @param value Owned. */
void cleanup(int value);
/** @brief Inline. @param value Input. @return Output. */
static inline int inline_call(int value) { return value; }
''', encoding="utf-8")
            self.assertEqual(docs.public_declarations(Path(directory)), {
                ("complete", ("value",)), ("cleanup", ("value",))})

    def test_private_prototype_uses_only_complete_definition_documentation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "src").mkdir()
            (root / "src/helpers.c").write_text('''/** @brief Complete. @param value Input. @return Value. */
int complete(int value) { return value; }
/** @brief Incomplete. @return Value. */
int incomplete(int value) { return value; }
/** @brief Writer. @param format Format. */
void writer(const char *format, ...) { }
''', encoding="utf-8")
            definitions = docs.documented_definitions(root)
            self.assertEqual(definitions, {("complete", ("value",)), ("writer", ("format",))})
            header = '''/** @brief Declaration. */ int complete(int value);
/** @brief Unmatched. @param value Input. @return Value. */ int incomplete(int value);
/** @brief Writer declaration. */ void writer(const char *format, ...);
/** @brief Inline. @param value Input. @return Value. */ static inline int inline_call(int value) { return value; }
'''
            filtered = docs.filter_private_header(header, definitions)
            self.assertIn("/*  @brief Declaration.", filtered)
            self.assertIn("/*  @brief Writer declaration.", filtered)
            self.assertIn("/** @brief Unmatched.", filtered)
            self.assertIn("/** @brief Inline.", filtered)
            self.assertEqual(len(header), len(filtered))
            self.assertEqual(header.count("\n"), filtered.count("\n"))

    def test_multiline_arrays_callbacks_and_export_prefix(self):
        source = '''/** @brief Visit. @param values Array. @param callback Handler. */
CGAI_EXPORT void visit(const int values[3],
                      void (*callback)(int item));
'''
        function, = docs.functions(source)
        self.assertEqual(function.name, "visit")
        self.assertEqual(function.parameters, ("values", "callback"))

    def test_unsupported_or_unmatched_signatures_keep_their_docs(self):
        for source in (
            "/** @brief Unsupported. */ int (*lookup(int id))(int) { return 0; }",
            "/** @brief Unnamed. */ int lookup(int) { return 0; }",
            "/** @brief Unmatched. */ int lookup(int value) { return value; }",
        ):
            self.assertEqual(docs.filter_source(source, {("lookup", ("id",))}), source)

    def test_real_public_header_covers_the_ci_regression(self):
        root = Path(__file__).resolve().parents[2]
        declarations = docs.public_declarations(root / "include")
        self.assertIn(("cgai_bark_catalog_text", ("id",)), declarations)
        source = (root / "src/bark/bark_model.c").read_bytes().decode("utf-8")
        filtered = docs.filter_source(source, declarations)
        self.assertIn("/*  @brief Resolve one catalog ID to authored game content.", filtered)
        self.assertIn("/** @file bark_model.c", filtered)
        self.assertEqual(source.count("\n"), filtered.count("\n"))

    def test_cli_leaves_non_c_input_byte_identical(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "notes.md"
            original = b"# Notes\r\nUTF-8: \xc3\xa9\r\n"
            path.write_bytes(original)
            result = subprocess.run([sys.executable, str(Path(docs.__file__)), str(path)],
                                    check=True, capture_output=True)
            self.assertEqual(result.stdout, original)
            self.assertEqual(result.stderr, b"")

    @unittest.skipUnless(DOXYGEN, "Doxygen executable is required for documentation integration")
    def test_real_doxygen_preserves_public_private_docs_and_source_browser(self):
        with tempfile.TemporaryDirectory(prefix="cgai docs smoke ") as directory:
            root = Path(directory)
            configuration = smoke_configuration(root, smoke_fixture(root))
            result = run_documentation(configuration)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            documents = [ElementTree.parse(path).getroot() for path in (root / "output/xml").glob("*.xml")]
            descriptions = " ".join("".join(description.itertext()) for document in documents
                                    for description in document.iter("briefdescription"))
            self.assertIn("Canonical public marker.", descriptions)
            self.assertIn("Canonical private marker.", descriptions)
            self.assertNotIn("Original public source marker.", descriptions)
            source_pages = " ".join(path.read_text(encoding="utf-8")
                                    for path in (root / "output/html").glob("*_source.html"))
            self.assertTrue("Original public source marker." in html.unescape(re.sub(r"<[^>]+>", "", source_pages)),
                            "Source browser omitted original definition comments")

    @unittest.skipUnless(DOXYGEN, "Doxygen executable is required for documentation integration")
    def test_real_doxygen_filter_failure_cannot_pass_the_docs_gate(self):
        with tempfile.TemporaryDirectory(prefix="cgai docs failure ") as directory:
            root = Path(directory)
            script = smoke_fixture(root)
            script.write_text("import sys\nsys.stderr.write('deliberate filter failure\\n')\nsys.exit(7)\n", encoding="utf-8")
            result = run_documentation(smoke_configuration(root, script))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("deliberate filter failure", result.stderr)

    @unittest.skipUnless(DOXYGEN, "Doxygen executable is required for documentation integration")
    def test_silent_filter_failures_cannot_reuse_stale_api_docs(self):
        with tempfile.TemporaryDirectory(prefix="cgai docs silent failure ") as directory:
            root = Path(directory)
            script = smoke_fixture(root)
            configuration = smoke_configuration(root, script)
            self.assertEqual(run_documentation(configuration).returncode, 0)
            for status in (7, 0):
                script.write_text(f"import sys\nsys.exit({status})\n", encoding="utf-8")
                result = run_documentation(configuration)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Public API documentation verification failed", result.stderr)


if __name__ == "__main__":
    unittest.main()
