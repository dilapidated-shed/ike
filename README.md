# Ike

A small build tool whose first syntax is ordinary English.

```text
program depends on main.o print.o
    cc main.o print.o -o program

main.o depends on main.c
    cc -c main.c -o main.o

print.o depends on print.c
    cc -c print.c -o print.o
```

Version 1 treats `depends on` as exact syntax. The first target is the default target. Indented lines are recipes. Dependencies can be files or other targets. Ike rebuilds a target when it is missing or when a dependency is newer, and refuses missing dependencies and dependency cycles.

Ike follows the selected target's dependencies from left to right. A target reached more than once is built at most once per invocation. Rules outside the selected target's dependency closure are not built.

By default, recipes retain the original POSIX `system()` execution boundary. To use another recipe interpreter, set `IKE_RECIPE_RUNNER` to its absolute executable path:

```sh
IKE_RECIPE_RUNNER=/absolute/path/to/runner ./ike target
```

For each recipe, Ike writes exactly that recipe line plus a newline to a private temporary source file and executes:

```text
/absolute/path/to/runner temporary-source-file
```

Ike does not add `-c`, parse runner options, or reinterpret the recipe. This source-file protocol is intentionally small enough for the maintained Ish milestone as well as Grease. A recipe still has to stay within the syntax implemented by the selected runner.

## Build receipts

Set `IKE_RECEIPT` to request a versioned execution receipt and
`IKE_IKEFILE_IDENTITY` to the identity anchored by the caller:

```sh
IKE_RECEIPT=/tmp/ike.tsv \
IKE_IKEFILE_IDENTITY=sha256:<caller-checked-digest> \
    ./ike target
```

The `ike-build-v1` TSV receipt records the selected target, the caller-supplied
Ikefile identity, the recipe-runner mode and identity, each rule and recipe that
actually executes in order, every recipe exit status, and the final PASS/FAIL
result. Free-form text fields are lowercase hexadecimal bytes so tabs and other
recipe characters cannot change the record structure.

The receipt is evidence, not self-authenticating proof. A consumer must pin the
Ike source it compiled, independently anchor the Ikefile identity, and verify the
receipt and produced effects. Requesting a receipt without an Ikefile identity is
a hard error; receipt write failure also makes the build fail.

There are deliberately no variables, pattern rules, implicit rules, or fuzzy English yet.

## Bootstrap

Compile `ike.c` using the absolute path to the qualified ICK C compiler, with
`-fno-link-libatomic -std=c11 -Wall -Wextra -Wpedantic -O2`, producing `ike`.
Set `ICK` to that same compiler path before invoking `./ike`.  Ike then builds
itself from its own `Ikefile`; compiler selection is an explicit environment
input rather than a silent host-compiler choice.

The native CI producer pins ICK's `←`, `×`, and `÷` frontend and separately declares
its prebuilt host startup/runtime boundary.  Android packages compile the same
maintained source with cross ICK and use the pinned NDK for headers, final
linking, and runtime libraries.  Package construction and device execution
remain separate qualifications.

The parser maps immutable source spans to typed line definitions before graph
allocation.  Owned dependency/recipe sequences and named traversal states
retain the original English grammar, left-to-right traversal, recipe protocol,
and receipt schema.  See [_/ci/c-source.tsv](_/ci/c-source.tsv) for source roles.

Run the basic checks with:

```sh
./ike test
```

A later English layer can recognize phrases near `depends on`, but it should resolve them to this exact dependency graph before any build runs. That keeps interpretation separate from execution.

The test target also compares 13 parser edge cases against the frozen original
in `_/tests/reference/ike.c`, with both executables compiled by ICK.  The
inherited POSIX behavior fixtures exercise Ike's declared recipe protocol;
they are separate from Grease and Ish's actual launcher/runner checks.

The 2026-10-09 division migration changes the maintained parser scenario-count
calculation to `÷`. Its producer pins ICK
`c61e448251744a2f40ad743ebef1a027bdcd2f9d`. The complete `./ike test` target
passed using that frontend: the original behavior/receipt suite returned `ok`,
and all 13 parser scenarios matched across 26 executions. The frozen source
and the shell-language fixtures remain unchanged.
