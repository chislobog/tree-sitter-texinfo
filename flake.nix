{
  description = "Tree-sitter grammar for Texinfo";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-parts = {
      url = "github:hercules-ci/flake-parts";
      inputs.nixpkgs-lib.follows = "nixpkgs";
    };
    # `nix fmt`: every formatter, each on its files.
    treefmt-nix = {
      url = "github:numtide/treefmt-nix";
      inputs.nixpkgs.follows = "nixpkgs";
    };
    # pre-commit: the git hooks, also run by `nix flake check`.
    git-hooks = {
      url = "github:cachix/git-hooks.nix";
      inputs.nixpkgs.follows = "nixpkgs";
      inputs.flake-compat.follows = "";
    };
  };

  outputs =
    inputs:
    inputs.flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      imports = [
        inputs.treefmt-nix.flakeModule
        inputs.git-hooks.flakeModule
      ];

      perSystem =
        {
          config,
          pkgs,
          lib,
          ...
        }:
        let
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

          python = pkgs.python3.withPackages (ps: [ ps.tree-sitter ]);
          # LLVM 23 rather than the default 21, whose AddressSanitizer deadlocks
          # at startup on macOS 27 (the scanner's fuzzing); its clang-format
          # formats src/scanner.c the same.
          llvm = pkgs.llvmPackages_23;

          # ESLint, as package-lock.json pins it, with the committed
          # eslint.config.mjs; without install scripts, which ESLint doesn't
          # need.
          nodeModules = pkgs.importNpmLock.buildNodeModules {
            npmRoot = ./.;
            nodejs = pkgs.nodejs_22;
            derivationArgs.npmFlags = [ "--ignore-scripts" ];
          };
          # The config's imports resolve from its directory's node_modules.
          eslintConfig = pkgs.runCommand "eslint-config" { } ''
            mkdir $out
            cp ${./eslint.config.mjs} $out/eslint.config.mjs
            ln -s ${nodeModules}/node_modules $out/node_modules
          '';
          eslint = pkgs.writeShellApplication {
            name = "eslint";
            runtimeInputs = [ pkgs.nodejs_22 ];
            text = ''
              exec node ${nodeModules}/node_modules/eslint/bin/eslint.js --config ${eslintConfig}/eslint.config.mjs "$@"
            '';
          };

          # Commands that pre-commit hooks run, as programs Nix checks with
          # shellcheck when it builds them.
          hookProgram =
            name: runtimeInputs: text:
            lib.getExe (pkgs.writeShellApplication { inherit name runtimeInputs text; });

          # The source the checks need, without the docs and CI config, so
          # editing those doesn't rerun every check.
          checkSource = lib.fileset.toSource {
            root = ./.;
            fileset = lib.fileset.unions [
              ./grammar.js
              ./tree-sitter.json
              ./.tsqueryrc.json
              ./src
              ./script
              ./test
              ./queries
              ./editors
            ];
          };

          # A check: COMMAND in a writable copy of the source, with the
          # example manuals and texi2any's test inputs linked as in the dev
          # shell.
          mkCheck =
            name: command:
            pkgs.runCommand "check-${name}"
              {
                nativeBuildInputs = [
                  pkgs.tree-sitter
                  pkgs.nodejs_22
                  llvm.clang
                  pkgs.texinfo
                  python
                ];
              }
              ''
                cp -r ${checkSource} source
                chmod -R u+w source
                cd source
                ln -s ${examples} examples
                ln -s ${texinfoTests} texinfo-tests
                export HOME=$TMPDIR XDG_CACHE_HOME=$TMPDIR/cache TEXINFO_XS=omit
                ${command}
                touch $out
              '';

          # As tree-sitter's fuzz-action: the serializer may be called with a
          # null buffer.
          fuzzIgnorelist = pkgs.writeText "ignorelist.ini" ''
            [nonnull-attribute]
            fun:tree_sitter_*_external_scanner_serialize
            fun:tree_sitter_*_external_scanner_deserialize
          '';
          # Builds the libFuzzer target for the parser and scanner, with
          # AddressSanitizer and UndefinedBehaviorSanitizer, into OUTPUT.
          buildFuzzer = output: ''
            ${llvm.clang}/bin/clang -std=c11 -Og -g -Isrc -I${pkgs.tree-sitter}/include \
              -fsanitize=fuzzer,address,undefined -fsanitize-ignorelist=${fuzzIgnorelist} \
              test/fuzz/harness.c src/parser.c src/scanner.c \
              -L${pkgs.tree-sitter}/lib -ltree-sitter -Wl,-rpath,${pkgs.tree-sitter}/lib -o ${output}
          '';
        in
        {
          packages.examples = examples;
          packages.texinfo-tests = texinfoTests;

          # `nix fmt`. Only the files written by hand: src/ and the bindings
          # are generated. Its check is the pre-commit hook's.
          treefmt = {
            projectRootFile = "flake.nix";
            flakeCheck = false;
            programs.clang-format = {
              enable = true;
              package = llvm.clang-tools;
              includes = [
                "src/scanner.c"
                "test/fuzz/*.c"
              ];
            };
            programs.ruff-format = {
              enable = true;
              includes = [
                "script/*.py"
                ".claude/hooks/*.py"
              ];
            };
            programs.nixfmt.enable = true;
            programs.just.enable = true;
            # ts_query_ls rewrites a file even when its format doesn't
            # change, which treefmt would take for a change: only
            # unformatted ones.
            settings.formatter.ts_query_ls = {
              command = lib.getExe (
                pkgs.writeShellApplication {
                  name = "ts-query-format";
                  runtimeInputs = [ pkgs.ts_query_ls ];
                  text = ''
                    for file in "$@"; do
                      ts_query_ls format --check "$file" >/dev/null 2>&1 || ts_query_ls format "$file"
                    done
                  '';
                }
              );
              includes = [ "queries/*.scm" ];
            };
          };

          # The git hooks, installed by the dev shell and run by `nix flake
          # check` (as its `pre-commit` check) on every file. The treefmt
          # hook uses the formatters above.
          pre-commit.settings.hooks = {
            treefmt.enable = true;
            ruff = {
              enable = true;
              # Not the bindings, which tree-sitter generates.
              files = "^(script|\\.claude/hooks)/.*\\.py$";
            };
            actionlint.enable = true;
            shellcheck = {
              enable = true;
              excludes = [ "^\\.envrc$" ];
            };
            eslint = {
              enable = true;
              package = eslint;
              files = "^grammar\\.js$";
            };
            # queries/ against its capture names, and each editor's
            # generated queries against the editor's; ts_query_ls needs the
            # built parser, and finds the language from a
            # `tree-sitter-texinfo` directory in the path.
            queries = {
              enable = true;
              name = "queries";
              entry =
                hookProgram "check-queries"
                  [
                    pkgs.tree-sitter
                    llvm.clang
                    pkgs.ts_query_ls
                  ]
                  ''
                    work=$(mktemp -d)/tree-sitter-texinfo
                    mkdir -p "$work"
                    cp -r queries editors .tsqueryrc.json "$work"
                    tree-sitter build -o "$work/texinfo.so" >/dev/null
                    cd "$work"
                    ts_query_ls check -f queries/
                    for editor in editors/*/; do
                      ts_query_ls check -f -c "$(cat "$editor/tsqueryrc.json")" "$editor/queries"
                    done
                  '';
              files = "^(queries|editors)/|tsqueryrc\\.json$";
              pass_filenames = false;
            };
            editor-queries = {
              enable = true;
              name = "editor queries up to date";
              entry = "${python}/bin/python3 script/editor-queries.py --check";
              files = "^(queries|editors)/|^script/editor-queries\\.py$";
              pass_filenames = false;
            };
            # Conventional Commits, with convco; but not the messages git
            # writes itself, which convco rejects.
            commit-message = {
              enable = true;
              name = "Conventional Commits";
              entry = hookProgram "check-commit-message" [ pkgs.convco pkgs.gnugrep pkgs.coreutils ] ''
                message=$(grep -v '^#' "$1")
                case "$(head -n 1 <<<"$message")" in
                  "Merge "* | "Revert \""* | "fixup! "* | "squash! "* | "amend! "*) exit 0 ;;
                esac
                convco check --from-stdin <<<"$message"
              '';
              stages = [ "commit-msg" ];
            };
            # Before pushing: the committed parser matches grammar.js, and
            # the tests pass. `nix flake check` runs the rest.
            generated = {
              enable = true;
              name = "src/ matches grammar.js";
              entry = hookProgram "check-generated" [ pkgs.tree-sitter pkgs.nodejs_22 pkgs.git ] ''
                tree-sitter generate
                git diff --exit-code -- src/
              '';
              pass_filenames = false;
              always_run = true;
              stages = [ "pre-push" ];
            };
            tests = {
              enable = true;
              name = "tree-sitter test";
              entry = hookProgram "run-tests" [ pkgs.tree-sitter llvm.clang ] "tree-sitter test";
              pass_filenames = false;
              always_run = true;
              stages = [ "pre-push" ];
            };
          };

          checks = {
            # src/ is what `tree-sitter generate` makes of grammar.js.
            generated = mkCheck "generated" ''
              cp -r src ../src.committed
              tree-sitter generate
              diff -r ../src.committed src
            '';
            # Corpus, highlight and tags tests.
            tests = mkCheck "tests" "tree-sitter test";
            scanner-warnings = mkCheck "scanner-warnings" ''
              clang -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -Isrc -c src/scanner.c -o scanner.o
            '';
            # Incremental parsing after random edits of the corpus tests.
            fuzz = mkCheck "fuzz" "tree-sitter fuzz --iterations 100";
            # Each check still fails on the inputs in test/checks/ it is meant
            # to catch.
            checks-catch = mkCheck "checks-catch" "python3 script/test-checks.py";
            # Only the manuals in test/parse-errors.txt fail to parse, none
            # ten times slower than the median.
            parse = mkCheck "parse" "python3 script/parse-examples.py --manuals";
            # The differences from texi2any are exactly those listed.
            compare-manuals = mkCheck "compare-manuals" ''
              python3 script/compare-texi2any.py --hunks 0 --expected test/texi2any-differences.txt --compared-manuals
            '';
            compare-tests = mkCheck "compare-tests" ''
              python3 script/compare-texi2any.py --hunks 0 --expected test/texi2any-differences.txt --test-inputs
            '';
            # Every tree is as recorded in test/tree-snapshots.txt.
            snapshots = mkCheck "snapshots" ''
              python3 script/tree-snapshots.py --check --manuals --test-inputs
            '';
            # Incremental parsing after random edits of texi2any's test inputs
            # and the manuals, as corpus tests that exist only here.
            fuzz-inputs = mkCheck "fuzz-inputs" ''
              python3 script/fuzz-corpus.py --test-inputs --manuals
              tree-sitter test --file-name fuzz-inputs.txt >/dev/null
              tree-sitter fuzz --include texinfo-tests --iterations 50
              tree-sitter fuzz --include examples --iterations 10
            '';
            # Typing mistakes in the manuals stay local.
            edits = mkCheck "edits" ''
              python3 script/check-edits.py --edits 5 --seed 0 --edited-manuals
            '';
            # The generated Neovim queries load and run in Neovim.
            neovim-queries = mkCheck "neovim-queries" ''
              mkdir -p runtime/parser runtime/queries
              tree-sitter build -o runtime/parser/texinfo.so >/dev/null
              ln -s "$PWD/editors/neovim/queries" runtime/queries/texinfo
              ${lib.getExe pkgs.neovim} --headless -l test/neovim/check-queries.lua runtime \
                test/highlight/*.texi test/tags/*.texi
            '';
            # libFuzzer with sanitizers on the parser and scanner, for two
            # minutes, seeded with the corpus tests' inputs.
            fuzz-scanner = mkCheck "fuzz-scanner" ''
              ${buildFuzzer "fuzzer"}
              python3 script/corpus-inputs.py seeds
              python3 script/fuzz-dict.py > dict
              ./fuzzer -dict=dict -timeout=10 -max_len=4096 -max_total_time=120 seeds
            '';
          };

          # Longer fuzzing than the check, on several cores, keeping what it
          # covers in build/fuzz/ between runs: `nix run .#fuzz-scanner --
          # [SECONDS] [JOBS]`.
          apps.fuzz-scanner = {
            type = "app";
            program = lib.getExe (
              pkgs.writeShellApplication {
                name = "fuzz-scanner";
                runtimeInputs = [ python ];
                text = ''
                  seconds=''${1:-600}
                  jobs=''${2:-4}
                  mkdir -p build/fuzz/artifacts build/fuzz/out
                  ${buildFuzzer "build/fuzz/fuzzer"}
                  python3 script/corpus-inputs.py build/fuzz/seeds >/dev/null
                  python3 script/fuzz-dict.py > build/fuzz/dict
                  cd build/fuzz
                  exec ./fuzzer out seeds -dict=dict -artifact_prefix=artifacts/ -timeout=10 \
                    -max_len=4096 -max_total_time="$seconds" -fork="$jobs" -ignore_crashes=1
                '';
              }
            );
          };

          devShells.default = pkgs.mkShell {
            # The pre-commit hooks' tools, and the script that installs them.
            inputsFrom = [ config.pre-commit.devShell ];
            packages = with pkgs; [
              # Grammar authoring: `tree-sitter generate`, `test`, `parse`, `playground`
              tree-sitter
              nodejs_22

              # Short names for the checks and tools (justfile)
              just

              # Compiling the generated parser and the scanner
              llvm.clang

              # clangd language server for editing src/scanner.c
              llvm.clang-tools

              # Checks and formats the queries against the grammar
              ts_query_ls

              # Reference implementation and sample documents to test against
              texinfo

              # The Python checks
              python
            ];

            shellHook = ''
              ln -sfn ${examples} examples
              ln -sfn ${texinfoTests} texinfo-tests
              echo "tree-sitter $(tree-sitter --version | cut -d' ' -f2) | node $(node --version)"
            '';
          };
        };
    };
}
