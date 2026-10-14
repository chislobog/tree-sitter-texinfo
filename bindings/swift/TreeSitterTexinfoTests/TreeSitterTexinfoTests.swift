import XCTest
import SwiftTreeSitter
import TreeSitterTexinfo

final class TreeSitterTexinfoTests: XCTestCase {
    func testCanLoadGrammar() throws {
        let parser = Parser()
        let language = Language(language: tree_sitter_texinfo())
        XCTAssertNoThrow(try parser.setLanguage(language),
                         "Error loading GNU Texinfo grammar")
    }

    func testParsesADocument() throws {
        let parser = Parser()
        try parser.setLanguage(Language(language: tree_sitter_texinfo()))
        let tree = try XCTUnwrap(parser.parse("@node Top\n@top Title\n\nSome @code{x}.\n"))
        let root = try XCTUnwrap(tree.rootNode)
        XCTAssertFalse(root.hasError, root.sExpressionString ?? "")
        XCTAssertEqual(root.child(at: 0)?.nodeType, "top")
    }
}
