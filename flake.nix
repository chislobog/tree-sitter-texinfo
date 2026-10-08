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
      in
      {
        packages.examples = examples;

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
          ];

          shellHook = ''
            ln -sfn ${examples} examples
            echo "tree-sitter $(tree-sitter --version | cut -d' ' -f2) | node $(node --version)"
          '';
        };
      });
}
