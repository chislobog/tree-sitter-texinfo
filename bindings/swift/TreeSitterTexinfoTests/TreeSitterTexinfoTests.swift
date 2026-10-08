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
}
