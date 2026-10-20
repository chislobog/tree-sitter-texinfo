{
  description = "Tree-sitter grammar for Texinfo";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    # texi2any, the reference, and the manuals compared with it, pinned
    # apart from the tools by a fixed revision, which `nix flake update`
    # leaves: moving it changes many listed differences and snapshots at
    # once, so it moves on purpose, in a commit of its own (CLAUDE.md).
    reference.url = "github:NixOS/nixpkgs/39ad350a0602fa0a58a544344e3e9187526ea45c";
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
      # Not x86_64-darwin, which nixpkgs 26.11 no longer supports. CI
      # evaluates every check of each (`--all-systems --no-build`).
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "aarch64-darwin"
      ];
      imports = [
        inputs.treefmt-nix.flakeModule
        inputs.git-hooks.flakeModule
      ];

      perSystem =
        {
          config,
          inputs',
          pkgs,
          lib,
          ...
        }:
        let
          reference = inputs'.reference.legacyPackages;

          # Real-world manuals for regression parsing, pinned by `reference`:
          # the Texinfo files of these sources, extracted by
          # script/extract-examples.py.
          archive = strip: members: src: {
            source = "${src}";
            into = builtins.head (builtins.match "(.*)\\.tar\\..*" src.name);
            inherit strip members;
          };
          examplesManifest = pkgs.writeText "examples.json" (
            builtins.toJSON (
              map (archive 2 [ "*/doc/*" ]) (
                with reference;
                [
                  texinfo.src
                  gnumake.src
                  gnused.src
                  gawk.src
                ]
              )
              ++ [
                # Other GNU manuals, each with the files where it keeps them:
                # glibc's many custom definition macros, autoconf's and
                # automake's m4 examples full of braces, gdb's and binutils'
                # conditionals per target, bash's and coreutils' variables.
                (archive 1 [ "*/manual/*" ] reference.glibc.src)
                (archive 1 [ "*/doc/*" ] reference.autoconf.src)
                (archive 1 [ "*/doc/*" ] reference.automake.src)
                (archive 1 [ "*/gdb/doc/*" ] reference.gdb.src)
                (archive 1 [
                  "*/binutils/doc/*"
                  "*/ld/*.texi"
                  "*/gas/doc/*"
                ] reference.binutils-unwrapped.src)
                (archive 1 [ "*/doc/*" ] reference.bash.src)
                (archive 1 [ "*/doc/*" ] reference.coreutils.src)
                # Emacs: its user manual, Lisp reference and other manuals
                # (Gnus, Calc, TRAMP, ...), the largest and most varied here.
                # Its source is a directory, not an archive.
                {
                  source = "${reference.emacs.src}";
                  into = "emacs-${reference.emacs.version}";
                  strip = 1;
                  members = [
                    "doc/lispref/*.texi"
                    "doc/emacs/*.texi"
                    "doc/misc/*.texi"
                  ];
                }
                # Manuals by other authors, in other styles: SBCL's, with its
                # contribs (ASDF's among them); Maxima's, in English and
                # Japanese; GNU Parallel's, generated from Perl's POD by
                # pod2texi; Stow's; and the LaTeX2e reference manual.
                (archive 1 [
                  "*/doc/manual/*"
                  "*/doc/internals/*"
                  "*/contrib/*.texinfo"
                ] reference.sbcl.src)
                (
                  archive 1 [ "*/doc/info/*" ] reference.maxima.src
                  // {
                    exclude = map (language: "*/doc/info/${language}/*") [
                      "de"
                      "es"
                      "pt"
                      "pt_BR"
                      "ru"
                    ];
                  }
                )
                (archive 1 [ "*/src/*.texi" ] reference.parallel.src)
                (archive 1 [ "*/doc/*" ] reference.stow.src)
                {
                  source = "${reference.texlive.pkgs.latex2e-help-texinfo.texdoc}";
                  into = "latex2e-help-texinfo";
                  strip = 3;
                  members = [ "doc/latex/latex2e-help-texinfo/*.texi" ];
                }
              ]
            )
          );
          examples = pkgs.runCommand "texinfo-examples" { nativeBuildInputs = [ pkgs.python3 ]; } ''
            python3 ${./script/extract-examples.py} ${examplesManifest} "$out"
          '';

          # The inputs of texi2any's own parser tests, one file per test case
          texinfoTests = pkgs.runCommand "texinfo-tests" { nativeBuildInputs = [ pkgs.perl ]; } ''
            mkdir tests
            tar -xf ${reference.texinfo.src} -C tests --strip-components=4 --wildcards \
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

          # The files a check reads, and only those, so that a change to
          # others, such as the docs, a query or a snapshot, doesn't run it
          # again: `tree-sitter` needs the grammar's files, the scripts in
          # script/ the grammar and the scanner too (texinfo_language.py
          # reads their tables).
          parserFiles = [
            ./grammar.js
            ./tree-sitter.json
            ./src
          ];
          scriptFiles = parserFiles ++ [ ./script ];

          # A check: COMMAND in a writable copy of FILES, with the example
          # manuals and texi2any's test inputs linked as in the dev shell.
          # The command is a shell application, which shellcheck checks
          # when Nix builds it; it runs in the build's environment, whose
          # compiler finds its tools (dsymutil on macOS) by its setup hooks.
          mkCheck =
            name: files: command:
            let
              source = lib.fileset.toSource {
                root = ./.;
                fileset = lib.fileset.unions files;
              };
              script = pkgs.writeShellApplication {
                name = "check-${name}";
                text = ''
                  cp -r ${source} source
                  chmod -R u+w source
                  cd source
                  ln -s ${examples} examples
                  ln -s ${texinfoTests} texinfo-tests
                  export HOME=$TMPDIR XDG_CACHE_HOME=$TMPDIR/cache TEXINFO_XS=omit
                  ${command}
                '';
              };
            in
            pkgs.runCommand "check-${name}" {
              nativeBuildInputs = [
                pkgs.tree-sitter
                pkgs.nodejs_22
                llvm.clang
                reference.texinfo
                python
              ];
            } "${lib.getExe script} && touch $out";

          # As tree-sitter's fuzz-action: the serializer may be called with a
          # null buffer.
          fuzzIgnorelist = pkgs.writeText "ignorelist.ini" ''
            [nonnull-attribute]
            fun:tree_sitter_*_external_scanner_serialize
            fun:tree_sitter_*_external_scanner_deserialize
          '';
          # Builds the libFuzzer target for the parser and scanner, with
          # AddressSanitizer and UndefinedBehaviorSanitizer, into OUTPUT. The
          # scanner is built with checks of the tokens it returns that no tree
          # shows (test/fuzz/checked_scanner.c).
          buildFuzzer = output: ''
            ${llvm.clang}/bin/clang -std=c11 -Og -g -Isrc -I${pkgs.tree-sitter}/include \
              -fsanitize=fuzzer,address,undefined -fsanitize-ignorelist=${fuzzIgnorelist} \
              test/fuzz/harness.c src/parser.c test/fuzz/checked_scanner.c \
              -L${pkgs.tree-sitter}/lib -ltree-sitter -Wl,-rpath,${pkgs.tree-sitter}/lib -o ${output}
          '';
        in
        {
          packages.examples = examples;
          packages.texinfo-tests = texinfoTests;
          # Every check's result, which CI keeps as a garbage collector root
          # in the Nix store it caches, so that the next run with the same
          # inputs builds none of them again.
          packages.checks = pkgs.linkFarm "checks" (
            lib.mapAttrsToList (name: path: { inherit name path; }) config.checks
          );

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
                    temporary=$(mktemp -d)
                    trap 'rm -rf "$temporary"' EXIT
                    work=$temporary/tree-sitter-texinfo
                    mkdir -p "$work"
                    cp -r queries editors .tsqueryrc.json "$work"
                    tree-sitter build -o "$work/texinfo.so" >/dev/null
                    cd "$work"
                    ts_query_ls check -f queries/
                    for editor in editors/*/; do
                      ts_query_ls check -f -c "$(cat "$editor/tsqueryrc.json")" "$editor/queries"
                    done
                  '';
              # The grammar too, as renaming a node breaks the queries.
              files = "^(queries|editors|src)/|^grammar\\.js$|tsqueryrc\\.json$";
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
                message=$(grep -v '^#' "$1" || true)
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
                git diff --exit-code HEAD -- src/
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
            generated = mkCheck "generated" parserFiles ''
              cp -r src ../src.committed
              tree-sitter generate
              diff -r ../src.committed src
            '';
            # Corpus, highlight and tags tests, and the other queries' tests,
            # with those of Neovim's own patterns.
            tests = mkCheck "tests" (
              scriptFiles
              ++ [
                ./queries
                ./editors/neovim/queries
                ./test/corpus
                ./test/highlight
                ./test/tags
                ./test/queries
              ]
            ) "tree-sitter test && python3 script/test-queries.py";
            scanner-warnings = mkCheck "scanner-warnings" [ ./src ] ''
              clang -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -Isrc -c src/scanner.c -o scanner.o
            '';
            # The scanner's tokens and tables agree with the grammar's.
            scanner-tables = mkCheck "scanner-tables" scriptFiles "python3 script/check-scanner.py";
            # The parser's state count is within its budget.
            parser-size = mkCheck "parser-size" scriptFiles "python3 script/parser-size.py";
            # Incremental parsing after random edits of the corpus tests,
            # from a fixed seed, so that the check's result depends only on
            # its inputs, as Nix caches it; `just fuzz-deep` tries others.
            fuzz = mkCheck "fuzz" (
              parserFiles
              ++ [
                ./script/fuzz.py
                ./test/corpus
              ]
            ) "TREE_SITTER_SEED=0 python3 script/fuzz.py --iterations 100";
            # Each check still fails on the inputs in test/checks/ it is meant
            # to catch, and the check of the queries' patterns on changed
            # copies of them.
            checks-catch = mkCheck "checks-catch" (
              scriptFiles
              ++ [
                ./test/checks
                ./queries
                ./test/highlight
                ./test/tags
              ]
            ) "python3 script/test-checks.py";
            # Only the manuals in test/parse-errors.txt fail to parse, none
            # ten times slower than the median.
            parse = mkCheck "parse" (
              scriptFiles ++ [ ./test/parse-errors.txt ]
            ) "python3 script/parse-examples.py --manuals --stress";
            # The differences from texi2any are exactly those listed.
            compare-manuals = mkCheck "compare-manuals" (scriptFiles ++ [ ./test/texi2any-differences.txt ]) ''
              python3 script/compare-texi2any.py --hunks 0 --expected test/texi2any-differences.txt --compared-manuals
            '';
            # Also on the corpus tests' inputs, whose trees are written by
            # hand.
            compare-tests =
              mkCheck "compare-tests"
                (
                  scriptFiles
                  ++ [
                    ./test/texi2any-differences.txt
                    ./test/corpus
                  ]
                )
                ''
                  python3 script/compare-texi2any.py --hunks 0 --expected test/texi2any-differences.txt --test-inputs --corpus-inputs
                '';
            # Every tree is as recorded in test/tree-snapshots.txt.
            snapshots = mkCheck "snapshots" (scriptFiles ++ [ ./test/tree-snapshots.txt ]) ''
              python3 script/tree-snapshots.py --check --manuals --test-inputs
            '';
            # Incremental parsing after random edits of texi2any's test inputs
            # and the manuals, as corpus tests that exist only here.
            fuzz-inputs =
              mkCheck "fuzz-inputs"
                (
                  scriptFiles
                  ++ [
                    ./queries
                    ./test/corpus
                    ./test/highlight
                    ./test/tags
                  ]
                )
                ''
                  python3 script/fuzz-corpus.py --test-inputs --manuals
                  tree-sitter test --file-name fuzz-inputs.txt >/dev/null
                  TREE_SITTER_SEED=0 python3 script/fuzz.py --include texinfo-tests --iterations 50
                  TREE_SITTER_SEED=0 python3 script/fuzz.py --include examples --iterations 10
                '';
            # Typing mistakes in the manuals stay local.
            edits = mkCheck "edits" scriptFiles ''
              python3 script/check-edits.py --edits 5 --seed 0 --edited-manuals
            '';
            # The generated Neovim queries load and run in Neovim.
            neovim-queries =
              mkCheck "neovim-queries"
                (
                  parserFiles
                  ++ [
                    ./editors/neovim
                    ./test/neovim
                    ./test/highlight
                    ./test/tags
                    ./test/queries
                  ]
                )
                ''
                  mkdir -p runtime/parser runtime/queries
                  tree-sitter build -o runtime/parser/texinfo.so >/dev/null
                  ln -s "$PWD/editors/neovim/queries" runtime/queries/texinfo
                  ${lib.getExe pkgs.neovim} --headless -l test/neovim/check-queries.lua runtime \
                    test/highlight/*.texi test/tags/*.texi test/queries/*/*.texi
                '';
            # libFuzzer with sanitizers on the parser and scanner, for a fixed
            # number of runs from a fixed seed (about two minutes), seeded with
            # the corpus tests' inputs.
            fuzz-scanner =
              mkCheck "fuzz-scanner"
                (
                  scriptFiles
                  ++ [
                    ./test/corpus
                    ./test/fuzz
                  ]
                )
                ''
                  ${buildFuzzer "fuzzer"}
                  python3 script/corpus-inputs.py seeds
                  python3 script/fuzz-dict.py > dict
                  ./fuzzer -dict=dict -timeout=10 -max_len=4096 -seed=1 -runs=60000 seeds
                '';
          };

          # Longer fuzzing than the check, on several cores, keeping what it
          # covers in build/fuzz/ between runs: `nix run .#fuzz-scanner --
          # [SECONDS] [JOBS]`.
          apps.fuzz-scanner = {
            type = "app";
            meta.description = "libFuzzer with sanitizers on the scanner, longer than its check";
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

              # Checks a commit message, as the commit-msg hook does
              convco

              # Compiling the generated parser and the scanner
              llvm.clang

              # clangd language server for editing src/scanner.c
              llvm.clang-tools

              # Checks and formats the queries against the grammar
              ts_query_ls

              # Reference implementation and sample documents to test against
              reference.texinfo

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
