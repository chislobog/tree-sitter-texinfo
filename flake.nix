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
      in
      {
        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            # Grammar authoring: `tree-sitter generate`, `test`, `parse`, `playground`
            tree-sitter
            nodejs_22

            # Compiling the generated parser (and the scanner, if one is added)
            clang
            gnumake

            # Reference implementation and sample documents to test against
            texinfo
          ];

          shellHook = ''
            echo "tree-sitter $(tree-sitter --version | cut -d' ' -f2) | node $(node --version)"
          '';
        };
      });
}
