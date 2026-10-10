{
  description = "Tree-sitter grammar for Texinfo";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        # Real-world manuals for regression parsing, pinned via flake.lock
        examples =
          pkgs.runCommand "texinfo-examples"
            {
              srcs = with pkgs; [
                texinfo.src
                gnumake.src
                gnused.src
                gawk.src
              ];
            }
            ''
              # Extracts the given paths of a source tarball, minus the
              # leading directories, and keeps only its Texinfo files.
              extract() {
                local src=$1 strip=$2
                shift 2
                name=$(stripHash "$src")
                dir="$out/''${name%.tar.*}"
                mkdir -p "$dir"
                tar -xf "$src" -C "$dir" --strip-components="$strip" --wildcards "$@"
                find "$dir" -type f ! \( -name '*.texi' -o -name '*.texinfo' -o -name '*.txi' \) -delete
                find "$dir" -depth -type d -empty -delete
              }

              for src in $srcs; do
                extract "$src" 2 '*/doc/*'
              done

              # Other GNU manuals, each with the files where it keeps them:
              # glibc's many custom definition macros, autoconf's and
              # automake's m4 examples full of braces, gdb's and binutils'
              # conditionals per target, bash's and coreutils' variables.
              extract ${pkgs.glibc.src} 1 '*/manual/*'
              extract ${pkgs.autoconf.src} 1 '*/doc/*'
              extract ${pkgs.automake.src} 1 '*/doc/*'
              extract ${pkgs.gdb.src} 1 '*/gdb/doc/*'
              extract ${pkgs.binutils-unwrapped.src} 1 \
                '*/binutils/doc/*' '*/ld/*.texi' '*/gas/doc/*'
              extract ${pkgs.bash.src} 1 '*/doc/*'
              extract ${pkgs.coreutils.src} 1 '*/doc/*'

              # Emacs: its user manual, Lisp reference and other manuals
              # (Gnus, Calc, TRAMP, ...), the largest and most varied here.
              # Its source is a directory, not a tarball.
              dir="$out/emacs-${pkgs.emacs.version}"
              mkdir -p "$dir/lispref" "$dir/emacs" "$dir/misc"
              cp ${pkgs.emacs.src}/doc/lispref/*.texi "$dir/lispref"
              cp ${pkgs.emacs.src}/doc/emacs/*.texi "$dir/emacs"
              cp ${pkgs.emacs.src}/doc/misc/*.texi "$dir/misc"

              # Manuals by other authors, in other styles: SBCL's, with its
              # contribs (ASDF's among them); Maxima's, in English and
              # Japanese; GNU Parallel's, generated from Perl's POD by
              # pod2texi; Stow's; and the LaTeX2e reference manual.
              extract ${pkgs.sbcl.src} 1 '*/doc/manual/*' '*/doc/internals/*' '*/contrib/*.texinfo'
              extract ${pkgs.maxima.src} 1 '*/doc/info/*' \
                --exclude='*/doc/info/'{de,es,pt,pt_BR,ru}'/*'
              extract ${pkgs.parallel.src} 1 '*/src/*.texi'
              extract ${pkgs.stow.src} 1 '*/doc/*'
              dir="$out/latex2e-help-texinfo"
              mkdir -p "$dir"
              cp ${pkgs.texlive.pkgs.latex2e-help-texinfo.texdoc}/doc/latex/latex2e-help-texinfo/*.texi "$dir"
            '';

        # The inputs of texi2any's own parser tests, one file per test case
        texinfoTests = pkgs.runCommand "texinfo-tests" { nativeBuildInputs = [ pkgs.perl ]; } ''
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

            # Compiling the generated parser and the scanner. LLVM 23 rather
            # than the default 21, whose AddressSanitizer deadlocks at startup
            # on macOS 27 (script/fuzz-scanner.sh); its clang-format formats
            # src/scanner.c the same.
            llvmPackages_23.clang
            gnumake

            # clangd language server for editing src/scanner.c, and clang-format
            llvmPackages_23.clang-tools

            # Checks and formats the queries against the grammar
            ts_query_ls

            # Linters and formatters (script/lint.sh), besides clang-format
            # and ESLint from npm
            ruff
            shellcheck
            actionlint
            nixfmt

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
      }
    );
}
