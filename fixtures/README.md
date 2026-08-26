# Makefile fixtures

These examples are intentionally small and ordered. Each directory introduces
one main idea while retaining the earlier ones.

| Fixture | Main idea |
| --- | --- |
| `01-file-target` | A default target creates a file. |
| `02-prerequisite` | A target depends on a source file. |
| `03-variables` | Variables and the automatic variable `$@`. |
| `04-pattern-rule` | Substitution, a pattern rule, `$<`, and multiple derived files. |
| `05-include` | A Makefile includes a separate configuration fragment. |

Every fixture also has a phony `clean` target so the harness can compare a
complete build, a no-change rebuild, and cleanup.

The progression is based on the teaching material in
[`isomorphisms/c-examples`](https://github.com/isomorphisms/c-examples/tree/master/Makefile_examples)
and the GNU Make manual example cited there. The fixture commands use ordinary
text files so testing Make semantics does not depend on a particular C
compiler.

Large Makefiles do not belong in this directory. Real-world compatibility
cases will be catalogued separately, with their origin and the behavior they
are meant to exercise.
