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

Version 1 treats `depends on` as exact syntax. The first target is the default target. Indented lines are shell recipes. Dependencies can be files or other targets. Ike rebuilds a target when it is missing or when a dependency is newer, and refuses missing dependencies and dependency cycles.

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
