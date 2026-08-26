# Ike

Ike is an experiment in understanding existing Makefiles without changing what
they do.

The repository currently contains only a differential test harness. It runs
each small Makefile twice:

1. directly with GNU Make;
2. through the mock Ike parser and mock Ike runner.

It then compares the exit status, standard output, standard error, and files
produced by both runs. The mock parser is currently `cat`; the mock runner
delegates to GNU Make. Therefore an all-green run proves the test harness, not
that Ike has implemented Make yet.

Run the complete suite with:

```sh
sh test
```

## Layout

- `bin/ike-parse` is the parser boundary. For now it copies a Makefile to
  standard output unchanged.
- `bin/ike-make` is the execution boundary. For now it asks GNU Make to read
  that output.
- `fixtures/` contains the small Makefiles, ordered from simple to less simple.
- `test` runs the same observations through GNU Make and mock Ike.

Ninja is not part of this first boundary. It may later execute Ike's explicit
build graph, but Makefile compatibility is specified independently of any
backend.

