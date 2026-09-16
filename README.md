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

There are deliberately no variables, pattern rules, implicit rules, or fuzzy English yet.

## Bootstrap

```sh
cc -std=c11 -Wall -Wextra -Wpedantic -O2 ike.c -o ike
./ike
```

Ike then builds itself from its own `Ikefile`.

Run the basic checks with:

```sh
./ike test
```

A later English layer can recognize phrases near `depends on`, but it should resolve them to this exact dependency graph before any build runs. That keeps interpretation separate from execution.
