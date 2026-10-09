{
  description = "Tree-sitter grammar for Texinfo";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        # Real-world manuals for regression parsing, pinned via flake.lock
        examples = pkgs.runCommand "texinfo-examples"
          {
            srcs = with pkgs; [ texinfo.src gnumake.src gnused.src gawk.src ];
          }
          ''
            for src in $srcs; do
              name=$(stripHash "$src")
              dir="$out/''${name%.tar.*}"
              mkdir -p "$dir"
              tar -xf "$src" -C "$dir" --strip-components=2 --wildcards '*/doc/*.texi'
            done
          '';

        # The inputs of texi2any's own parser tests, one file per test case
        texinfoTests = pkgs.runCommand "texinfo-tests"
          { nativeBuildInputs = [ pkgs.perl ]; }
          ''
            mkdir tests
            tar -xf ${pkgs.texinfo.src} -C tests --strip-components=4 --wildcards \
              '*/tta/perl/t/*.t' '*/tta/perl/t/input_files/*'
            perl ${./script/extract-texi2any-tests.pl} tests "$out"
          '';
      in
      {
        packages.examples = examples;
        packages.texinfo-tests = texinfoTests;

        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            # Grammar authoring: `tree-sitter generate`, `test`, `parse`, `playground`
            tree-sitter
            nodejs_22

            # Compiling the generated parser and the scanner
            clang
            gnumake

            # clangd language server for editing src/scanner.c
            clang-tools

            # Reference implementation and sample documents to test against
            texinfo

            # script/compare-texi2any.py
            (python3.withPackages (ps: [ ps.tree-sitter ]))
          ];

          shellHook = ''
            ln -sfn ${examples} examples
            ln -sfn ${texinfoTests} texinfo-tests
            # Git hooks: Conventional Commits, and tests before pushing.
            git config core.hooksPath .githooks
            echo "tree-sitter $(tree-sitter --version | cut -d' ' -f2) | node $(node --version)"
          '';
        };
      });
}
