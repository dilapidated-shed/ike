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
    char *target;
    char **deps;
    size_t dep_count;
    size_t dep_capacity;
    char **recipes;
    size_t recipe_count;
    size_t recipe_capacity;
    int state;
} Rule;

typedef struct {
    Rule *rules;
    size_t count;
    size_t capacity;
    const char *recipe_runner;
} Build;

static void die(const char *message)
{
    fprintf(stderr, "ike: %s\n", message);
    exit(1);
}

static void *xrealloc(void *ptr, size_t size)
{
    void *next = realloc(ptr, size);
    if (next == NULL)
        die("out of memory");
    return next;
}

static char *xstrdup(const char *text)
{
    char *copy = strdup(text);
    if (copy == NULL)
        die("out of memory");
    return copy;
}

static char *trim_left(char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text))
        text++;
    return text;
}

static void trim_right(char *text)
{
    size_t n = strlen(text);
    while (n > 0 && isspace((unsigned char)text[n - 1]))
        text[--n] = '\0';
}

static void add_string(char ***items, size_t *count, size_t *capacity,
                       const char *value)
{
    if (*count == *capacity) {
        *capacity = *capacity == 0 ? 4 : *capacity * 2;
        *items = xrealloc(*items, *capacity * sizeof **items);
    }
    (*items)[(*count)++] = xstrdup(value);
}

static Rule *find_rule(Build *build, const char *target)
{
    for (size_t i = 0; i < build->count; i++) {
        if (strcmp(build->rules[i].target, target) == 0)
            return &build->rules[i];
    }
    return NULL;
}

static Rule *add_rule(Build *build, const char *target)
{
    if (find_rule(build, target) != NULL) {
        fprintf(stderr, "ike: duplicate target: %s\n", target);
        exit(1);
    }

    if (build->count == build->capacity) {
        build->capacity = build->capacity == 0 ? 8 : build->capacity * 2;
        build->rules = xrealloc(build->rules,
                                build->capacity * sizeof *build->rules);
    }

    Rule *rule = &build->rules[build->count++];
    *rule = (Rule){0};
    rule->target = xstrdup(target);
    return rule;
}

static void parse_ikefile(Build *build, const char *path)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "ike: cannot open %s: %s\n", path, strerror(errno));
        exit(1);
    }

    char *line = NULL;
    size_t line_capacity = 0;
    ssize_t length;
    size_t line_number = 0;
    Rule *current = NULL;

    while ((length = getline(&line, &line_capacity, file)) != -1) {
        (void)length;
        line_number++;
        trim_right(line);

        int indented = line[0] == ' ' || line[0] == '\t';
        char *body = trim_left(line);
        if (*body == '\0' || *body == '#')
            continue;

        if (indented) {
            if (current == NULL) {
                fprintf(stderr, "ike: %s:%zu: recipe without a target\n",
                        path, line_number);
                exit(1);
            }
            add_string(&current->recipes, &current->recipe_count,
                       &current->recipe_capacity, body);
            continue;
        }

        char *marker = strstr(body, " depends on");
        if (marker == NULL || (marker[11] != '\0' &&
                              !isspace((unsigned char)marker[11]))) {
            fprintf(stderr,
                    "ike: %s:%zu: expected 'TARGET depends on DEPENDENCIES'\n",
                    path, line_number);
            exit(1);
        }

        char *deps = marker + 11;
        *marker = '\0';
        trim_right(body);
        deps = trim_left(deps);

        if (*body == '\0') {
            fprintf(stderr, "ike: %s:%zu: empty target\n", path, line_number);
            exit(1);
        }

        current = add_rule(build, body);

        char *save = NULL;
        for (char *dep = strtok_r(deps, " \t", &save);
             dep != NULL;
             dep = strtok_r(NULL, " \t", &save)) {
            add_string(&current->deps, &current->dep_count,
                       &current->dep_capacity, dep);
        }
    }

    free(line);
    fclose(file);

    if (build->count == 0)
        die("Ikefile contains no targets");
}

static int status_code(int status)
{
    if (!WIFEXITED(status))
        return 1;
    return WEXITSTATUS(status);
}

static char *recipe_source_path(void)
{
    const char *temporary_directory = getenv("TMPDIR");
    if (temporary_directory == NULL || *temporary_directory == '\0')
        temporary_directory = "/tmp";

    size_t directory_length = strlen(temporary_directory);
    int needs_slash = temporary_directory[directory_length - 1] != '/';
    const char *name = "ike-recipe-XXXXXX";
    size_t size = directory_length + (size_t)needs_slash + strlen(name) + 1;
    char *path = xrealloc(NULL, size);

    snprintf(path, size, "%s%s%s", temporary_directory,
             needs_slash ? "/" : "", name);
    return path;
}

static int run_recipe_with_runner(const char *runner, const char *command)
{
    char *source_path = recipe_source_path();
    int source = mkstemp(source_path);
    if (source == -1) {
        fprintf(stderr, "ike: cannot create recipe source: %s\n",
                strerror(errno));
        free(source_path);
        return 1;
    }

    FILE *file = fdopen(source, "w");
    if (file == NULL) {
        fprintf(stderr, "ike: cannot open recipe source: %s\n",
                strerror(errno));
        close(source);
        unlink(source_path);
        free(source_path);
        return 1;
    }

    int write_failed = fprintf(file, "%s\n", command) < 0;
    if (fclose(file) != 0)
        write_failed = 1;
    if (write_failed) {
        fprintf(stderr, "ike: cannot write recipe source: %s\n",
                strerror(errno));
        unlink(source_path);
        free(source_path);
        return 1;
    }

    pid_t child = fork();
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

    int status = system(command);
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

static int build_rule(Build *build, Rule *rule)
{
    if (rule->state == 2)
        return 0;
    if (rule->state == 1) {
        fprintf(stderr, "ike: dependency cycle at %s\n", rule->target);
        return 1;
    }
    rule->state = 1;

    struct stat target_stat;
    int target_exists = stat(rule->target, &target_stat) == 0;
    int needs_build = !target_exists;

    for (size_t i = 0; i < rule->dep_count; i++) {
        const char *dep = rule->deps[i];
        Rule *dep_rule = find_rule(build, dep);

        if (dep_rule != NULL && build_rule(build, dep_rule) != 0)
            return 1;

        struct stat dep_stat;
        if (stat(dep, &dep_stat) != 0) {
            if (dep_rule != NULL) {
                needs_build = 1;
                continue;
            }
            fprintf(stderr, "ike: missing dependency '%s' for '%s'\n",
                    dep, rule->target);
            return 1;
        }

        if (!target_exists || mtime_is_newer(&dep_stat, &target_stat))
            needs_build = 1;
    }

    if (needs_build) {
        for (size_t i = 0; i < rule->recipe_count; i++) {
            int status = run_recipe(build->recipe_runner, rule->recipes[i]);
            if (status != 0) {
                fprintf(stderr, "ike: recipe for '%s' failed with status %d\n",
                        rule->target, status);
                return 1;
            }
        }
    }

    rule->state = 2;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 2) {
        fprintf(stderr, "usage: %s [target]\n", argv[0]);
        return 2;
    }

    Build build = {0};
    build.recipe_runner = getenv("IKE_RECIPE_RUNNER");
    if (build.recipe_runner != NULL && *build.recipe_runner == '\0')
        build.recipe_runner = NULL;
    if (build.recipe_runner != NULL && build.recipe_runner[0] != '/')
        die("IKE_RECIPE_RUNNER must be an absolute path");
    if (build.recipe_runner != NULL && access(build.recipe_runner, X_OK) != 0) {
        fprintf(stderr, "ike: recipe runner is not executable: %s: %s\n",
                build.recipe_runner, strerror(errno));
        return 1;
    }

    parse_ikefile(&build, "Ikefile");

    Rule *goal = argc == 2 ? find_rule(&build, argv[1]) : &build.rules[0];
    if (goal == NULL) {
        fprintf(stderr, "ike: unknown target: %s\n", argv[1]);
        return 1;
    }

    return build_rule(&build, goal);
}
