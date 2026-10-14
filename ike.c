#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    char **values;
    size_t count;
    size_t capacity;
} owned_string_sequence;

typedef enum {
    RULE_UNVISITED,
    RULE_VISITING,
    RULE_FINISHED
} traversal_state;

typedef struct {
    char *target;
    owned_string_sequence dependency_names;
    owned_string_sequence recipe_lines;
    traversal_state visit_state;
} dependency_rule;

typedef struct {
    dependency_rule *rules;
    size_t count;
    size_t capacity;
    const char *recipe_runner;
    FILE *receipt;
    size_t receipt_event;
} build_session;

typedef struct {
    const char *begin;
    size_t length;
} text_span;

typedef enum {
    SOURCE_IGNORED,
    SOURCE_RECIPE,
    SOURCE_RULE,
    SOURCE_INVALID_RULE,
    SOURCE_EMPTY_TARGET
} source_line_kind;

typedef struct {
    source_line_kind kind;
    text_span body;
    text_span target;
    text_span dependencies;
} interpreted_source_line;

static void die(const char *message)
{
    fprintf(stderr, "ike: %s\n", message);
    exit(1);
}

static void *xrealloc(void *ptr, size_t size)
{
    void *next ← realloc(ptr, size);
    if (next == NULL)
        die("out of memory");
    return next;
}

static text_span without_leading_space(text_span text)
{
    while (text.length > 0 && isspace((unsigned char)*text.begin)) {
        text.begin++;
        text.length--;
    }
    return text;
}

static text_span without_trailing_space(text_span text)
{
    while (text.length > 0 &&
           isspace((unsigned char)text.begin[text.length - 1]))
        text.length--;
    return text;
}

/* Interpret one source line as a value before allocating graph state.
   Recipes retain their exact normalized body; dependency tokens retain the
   existing space/tab delimiter rule. */
static interpreted_source_line interpret_source_line(const char *line)
{
    text_span whole ← {line, strlen(line)};
    text_span body ← without_leading_space(without_trailing_space(whole));
    interpreted_source_line interpreted ← {
        .kind ← SOURCE_IGNORED,
        .body ← body
    };
    if (body.length == 0 || body.begin[0] == '#')
        return interpreted;
    if (line[0] == ' ' || line[0] == '\t') {
        interpreted.kind ← SOURCE_RECIPE;
        return interpreted;
    }

    const char *marker ← strstr(body.begin, " depends on");
    if (marker == NULL || (marker[11] != '\0' &&
                          !isspace((unsigned char)marker[11]))) {
        interpreted.kind ← SOURCE_INVALID_RULE;
        return interpreted;
    }

    text_span target ← {body.begin, (size_t)(marker - body.begin)};
    interpreted.target ← without_trailing_space(target);
    if (interpreted.target.length == 0) {
        interpreted.kind ← SOURCE_EMPTY_TARGET;
        return interpreted;
    }

    /* An empty dependency tail is valid, including a trailing blank. */
    size_t prefix_length ← (size_t)(marker - body.begin) + 11;
    size_t dependency_length ←
        prefix_length < body.length ? body.length - prefix_length : 0;
    text_span dependencies ← {marker + 11, dependency_length};
    interpreted.dependencies ← without_leading_space(dependencies);
    interpreted.kind ← SOURCE_RULE;
    return interpreted;
}

static char *owned_text(text_span text)
{
    char *copy ← strndup(text.begin, text.length);
    if (copy == NULL)
        die("out of memory");
    return copy;
}

static void append_owned_text(owned_string_sequence *sequence, text_span text)
{
    if (sequence->count == sequence->capacity) {
        sequence->capacity ←
            sequence->capacity == 0 ? 4 : sequence->capacity × 2;
        sequence->values ←
            xrealloc(sequence->values, sequence->capacity × sizeof *sequence->values);
    }
    sequence->values[sequence->count++] ← owned_text(text);
}

typedef struct {
    text_span token;
    text_span remaining;
} dependency_token_step;

static dependency_token_step next_dependency_token(text_span remaining)
{
    while (remaining.length > 0 &&
           (remaining.begin[0] == ' ' || remaining.begin[0] == '\t')) {
        remaining.begin++;
        remaining.length--;
    }
    size_t token_length ← 0;
    while (token_length < remaining.length &&
           remaining.begin[token_length] != ' ' &&
           remaining.begin[token_length] != '\t')
        token_length++;
    dependency_token_step step ← {
        .token ← {remaining.begin, token_length},
        .remaining ← {remaining.begin + token_length,
                      remaining.length - token_length}
    };
    return step;
}

static void append_dependency_names(dependency_rule *rule, text_span dependencies)
{
    while (dependencies.length > 0) {
        dependency_token_step step ← next_dependency_token(dependencies);
        if (step.token.length > 0)
            append_owned_text(&rule->dependency_names, step.token);
        dependencies ← step.remaining;
    }
}

static dependency_rule *find_rule(build_session *build, const char *target)
{
    for (size_t rule_index ← 0; rule_index < build->count; rule_index++) {
        if (strcmp(build->rules[rule_index].target, target) == 0)
            return &build->rules[rule_index];
    }
    return NULL;
}

static dependency_rule *add_rule(build_session *build, text_span target_text)
{
    char *target ← owned_text(target_text);
    if (find_rule(build, target) != NULL) {
        fprintf(stderr, "ike: duplicate target: %s\n", target);
        exit(1);
    }

    if (build->count == build->capacity) {
        build->capacity ← build->capacity == 0 ? 8 : build->capacity × 2;
        build->rules ← xrealloc(build->rules,
                                build->capacity × sizeof *build->rules);
    }

    dependency_rule *rule ← &build->rules[build->count++];
    *rule ← (dependency_rule){0};
    rule->target ← target;
    return rule;
}

static void parse_ikefile(build_session *build, const char *path)
{
    FILE *file ← fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "ike: cannot open %s: %s\n", path, strerror(errno));
        exit(1);
    }

    char *line ← NULL;
    size_t line_capacity ← 0;
    ssize_t length;
    size_t line_number ← 0;
    dependency_rule *current ← NULL;

    while ((length ← getline(&line, &line_capacity, file)) != -1) {
        (void)length;
        line_number++;
        interpreted_source_line interpreted ← interpret_source_line(line);
        switch (interpreted.kind) {
        case SOURCE_IGNORED:
            break;
        case SOURCE_RECIPE:
            if (current == NULL) {
                fprintf(stderr, "ike: %s:%zu: recipe without a target\n",
                        path, line_number);
                exit(1);
            }
            append_owned_text(&current->recipe_lines, interpreted.body);
            break;
        case SOURCE_RULE:
            current ← add_rule(build, interpreted.target);
            append_dependency_names(current, interpreted.dependencies);
            break;
        case SOURCE_EMPTY_TARGET:
            fprintf(stderr, "ike: %s:%zu: empty target\n", path, line_number);
            exit(1);
        case SOURCE_INVALID_RULE:
            fprintf(stderr,
                    "ike: %s:%zu: expected 'TARGET depends on DEPENDENCIES'\n",
                    path, line_number);
            exit(1);
        }
    }

    free(line);
    fclose(file);

    if (build->count == 0)
        die("Ikefile contains no targets");
}

static void receipt_hex(FILE *file, const char *text)
{
    const unsigned char *cursor ← (const unsigned char *)text;
    while (*cursor != '\0') {
        fprintf(file, "%02x", (unsigned int)*cursor);
        cursor++;
    }
}

static int receipt_text(FILE *file, const char *key, const char *value)
{
    if (fprintf(file, "%s\t", key) < 0)
        return 1;
    receipt_hex(file, value);
    if (fputc('\n', file) == EOF)
        return 1;
    return ferror(file) != 0;
}

static int start_receipt(build_session *build, const char *path,
                         const char *ikefile_identity,
                         const char *selected_target)
{
    build->receipt ← fopen(path, "w");
    if (build->receipt == NULL) {
        fprintf(stderr, "ike: cannot open receipt %s: %s\n",
                path, strerror(errno));
        return 1;
    }

    const char *runner_mode ←
        build->recipe_runner == NULL ? "posix-system" : "external-source-file";
    const char *runner_identity ←
        build->recipe_runner == NULL ? "POSIX-system()" : build->recipe_runner;

    if (fprintf(build->receipt, "schema\tike-build-v1\n") < 0 ||
        receipt_text(build->receipt, "selected_target_hex",
                     selected_target) != 0 ||
        receipt_text(build->receipt, "ikefile_identity_hex",
                     ikefile_identity) != 0 ||
        fprintf(build->receipt, "recipe_runner_mode\t%s\n",
                runner_mode) < 0 ||
        receipt_text(build->receipt, "recipe_runner_identity_hex",
                     runner_identity) != 0) {
        fprintf(stderr, "ike: cannot write receipt %s\n", path);
        fclose(build->receipt);
        build->receipt ← NULL;
        return 1;
    }

    return 0;
}

static int receipt_rule(build_session *build, const char *target)
{
    if (build->receipt == NULL)
        return 0;

    if (fprintf(build->receipt, "rule\t%zu\t", build->receipt_event++) < 0)
        return 1;
    receipt_hex(build->receipt, target);
    if (fputc('\n', build->receipt) == EOF)
        return 1;
    return ferror(build->receipt) != 0;
}

static int receipt_recipe(build_session *build, const char *target,
                          const char *recipe, int status)
{
    if (build->receipt == NULL)
        return 0;

    if (fprintf(build->receipt, "recipe\t%zu\t",
                build->receipt_event++) < 0)
        return 1;
    receipt_hex(build->receipt, target);
    if (fputc('\t', build->receipt) == EOF)
        return 1;
    receipt_hex(build->receipt, recipe);
    if (fprintf(build->receipt, "\t%d\n", status) < 0)
        return 1;
    return ferror(build->receipt) != 0;
}

static int finish_receipt(build_session *build, int result)
{
    if (build->receipt == NULL)
        return result;

    int failed ←
        fprintf(build->receipt, "final_result\t%s\n",
                result == 0 ? "PASS" : "FAIL") < 0;
    if (fclose(build->receipt) != 0)
        failed ← 1;
    build->receipt ← NULL;

    if (failed) {
        fprintf(stderr, "ike: cannot finish receipt\n");
        return 1;
    }
    return result;
}

static int status_code(int status)
{
    if (!WIFEXITED(status))
        return 1;
    return WEXITSTATUS(status);
}

static char *recipe_source_path(void)
{
    const char *temporary_directory ← getenv("TMPDIR");
    if (temporary_directory == NULL || *temporary_directory == '\0')
        temporary_directory ← "/tmp";

    size_t directory_length ← strlen(temporary_directory);
    int needs_slash ← temporary_directory[directory_length - 1] != '/';
    const char *name ← "ike-recipe-XXXXXX";
    size_t size ← directory_length + (size_t)needs_slash + strlen(name) + 1;
    char *path ← xrealloc(NULL, size);

    snprintf(path, size, "%s%s%s", temporary_directory,
             needs_slash ? "/" : "", name);
    return path;
}

static int run_recipe_with_runner(const char *runner, const char *command)
{
    char *source_path ← recipe_source_path();
    int source ← mkstemp(source_path);
    if (source == -1) {
        fprintf(stderr, "ike: cannot create recipe source: %s\n",
                strerror(errno));
        free(source_path);
        return 1;
    }

    FILE *file ← fdopen(source, "w");
    if (file == NULL) {
        fprintf(stderr, "ike: cannot open recipe source: %s\n",
                strerror(errno));
        close(source);
        unlink(source_path);
        free(source_path);
        return 1;
    }

    int write_failed ← fprintf(file, "%s\n", command) < 0;
    if (fclose(file) != 0)
        write_failed ← 1;
    if (write_failed) {
        fprintf(stderr, "ike: cannot write recipe source: %s\n",
                strerror(errno));
        unlink(source_path);
        free(source_path);
        return 1;
    }

    pid_t child ← fork();
    if (child == -1) {
        fprintf(stderr, "ike: cannot start recipe runner: %s\n",
                strerror(errno));
        unlink(source_path);
        free(source_path);
        return 1;
    }

    if (child == 0) {
        execl(runner, runner, source_path, (char *)NULL);
        fprintf(stderr, "ike: cannot execute recipe runner %s: %s\n",
                runner, strerror(errno));
        _exit(126);
    }

    int status;
    while (waitpid(child, &status, 0) == -1) {
        if (errno == EINTR)
            continue;
        fprintf(stderr, "ike: cannot wait for recipe runner: %s\n",
                strerror(errno));
        unlink(source_path);
        free(source_path);
        return 1;
    }

    unlink(source_path);
    free(source_path);
    return status_code(status);
}

static int run_recipe(const char *runner, const char *command)
{
    printf("%s\n", command);
    fflush(stdout);

    if (runner != NULL)
        return run_recipe_with_runner(runner, command);

    int status ← system(command);
    if (status == -1)
        return 1;
    return status_code(status);
}

static int mtime_is_newer(const struct stat *left, const struct stat *right)
{
    if (left->st_mtim.tv_sec != right->st_mtim.tv_sec)
        return left->st_mtim.tv_sec > right->st_mtim.tv_sec;
    return left->st_mtim.tv_nsec > right->st_mtim.tv_nsec;
}

static int build_rule(build_session *build, dependency_rule *rule)
{
    if (rule->visit_state == RULE_FINISHED)
        return 0;
    if (rule->visit_state == RULE_VISITING) {
        fprintf(stderr, "ike: dependency cycle at %s\n", rule->target);
        return 1;
    }
    rule->visit_state ← RULE_VISITING;

    struct stat target_stat;
    int target_exists ← stat(rule->target, &target_stat) == 0;
    int needs_build ← !target_exists;

    for (size_t dependency_index ← 0; dependency_index < rule->dependency_names.count; dependency_index++) {
        const char *dep ← rule->dependency_names.values[dependency_index];
        dependency_rule *dep_rule ← find_rule(build, dep);

        if (dep_rule != NULL && build_rule(build, dep_rule) != 0)
            return 1;

        struct stat dep_stat;
        if (stat(dep, &dep_stat) != 0) {
            if (dep_rule != NULL) {
                needs_build ← 1;
                continue;
            }
            fprintf(stderr, "ike: missing dependency '%s' for '%s'\n",
                    dep, rule->target);
            return 1;
        }

        if (!target_exists || mtime_is_newer(&dep_stat, &target_stat))
            needs_build ← 1;
    }

    if (needs_build) {
        if (receipt_rule(build, rule->target) != 0) {
            fprintf(stderr, "ike: cannot write receipt\n");
            return 1;
        }

        for (size_t recipe_index ← 0; recipe_index < rule->recipe_lines.count; recipe_index++) {
            int status ← run_recipe(build->recipe_runner, rule->recipe_lines.values[recipe_index]);
            if (receipt_recipe(build, rule->target, rule->recipe_lines.values[recipe_index],
                               status) != 0) {
                fprintf(stderr, "ike: cannot write receipt\n");
                return 1;
            }
            if (status != 0) {
                fprintf(stderr, "ike: recipe for '%s' failed with status %d\n",
                        rule->target, status);
                return 1;
            }
        }
    }

    rule->visit_state ← RULE_FINISHED;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 2) {
        fprintf(stderr, "usage: %s [target]\n", argv[0]);
        return 2;
    }

    build_session build ← {0};
    build.recipe_runner ← getenv("IKE_RECIPE_RUNNER");
    if (build.recipe_runner != NULL && *build.recipe_runner == '\0')
        build.recipe_runner ← NULL;
    if (build.recipe_runner != NULL && build.recipe_runner[0] != '/')
        die("IKE_RECIPE_RUNNER must be an absolute path");
    if (build.recipe_runner != NULL && access(build.recipe_runner, X_OK) != 0) {
        fprintf(stderr, "ike: recipe runner is not executable: %s: %s\n",
                build.recipe_runner, strerror(errno));
        return 1;
    }

    parse_ikefile(&build, "Ikefile");

    dependency_rule *goal ← argc == 2 ? find_rule(&build, argv[1]) : &build.rules[0];
    if (goal == NULL) {
        fprintf(stderr, "ike: unknown target: %s\n", argv[1]);
        return 1;
    }

    const char *receipt_path ← getenv("IKE_RECEIPT");
    if (receipt_path != NULL && *receipt_path == '\0')
        receipt_path ← NULL;

    if (receipt_path != NULL) {
        const char *ikefile_identity ← getenv("IKE_IKEFILE_IDENTITY");
        if (ikefile_identity == NULL || *ikefile_identity == '\0')
            die("IKE_IKEFILE_IDENTITY is required when IKE_RECEIPT is set");
        if (start_receipt(&build, receipt_path, ikefile_identity,
                          goal->target) != 0)
            return 1;
    }

    int result ← build_rule(&build, goal);
    return finish_receipt(&build, result);
}
