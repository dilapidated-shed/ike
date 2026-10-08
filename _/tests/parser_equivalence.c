#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    const char *name;
    const char *source;
    size_t length;
    int expected_status;
} parser_scenario;

typedef struct {
    int status;
    char transcript[4096];
} captured_execution;

static void require(int condition, const char *message)
{
    if (!condition) {
        perror(message);
        exit(1);
    }
}

static void replace_private_source(const parser_scenario *scenario)
{
    int source ← open("Ikefile", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    require(source >= 0, "open fixture");
    size_t written ← 0;
    while (written < scenario->length) {
        ssize_t count ← write(source, scenario->source + written,
                              scenario->length - written);
        if (count < 0 && errno == EINTR)
            continue;
        require(count > 0, "write fixture");
        written ← written + (size_t)count;
    }
    require(close(source) == 0, "close fixture");
}

static captured_execution execute_private_fixture(const char *program)
{
    int output_pipe[2];
    require(pipe(output_pipe) == 0, "pipe");
    pid_t child ← fork();
    require(child >= 0, "fork");
    if (child == 0) {
        close(output_pipe[0]);
        require(dup2(output_pipe[1], STDOUT_FILENO) >= 0, "stdout pipe");
        require(dup2(output_pipe[1], STDERR_FILENO) >= 0, "stderr pipe");
        close(output_pipe[1]);
        unsetenv("IKE_RECIPE_RUNNER");
        unsetenv("IKE_RECEIPT");
        unsetenv("IKE_IKEFILE_IDENTITY");
        execl(program, program, (char *)NULL);
        _exit(126);
    }

    close(output_pipe[1]);
    captured_execution execution ← {0};
    size_t received ← 0;
    while (true) {
        require(received + 1 < sizeof execution.transcript, "bounded transcript");
        ssize_t count ← read(output_pipe[0], execution.transcript + received,
                             sizeof execution.transcript - received - 1);
        if (count < 0 && errno == EINTR)
            continue;
        require(count >= 0, "read transcript");
        if (count == 0)
            break;
        received ← received + (size_t)count;
    }
    execution.transcript[received] ← '\0';
    require(close(output_pipe[0]) == 0, "close transcript");
    int child_status;
    pid_t waited;
    do {
        waited ← waitpid(child, &child_status, 0);
    } while (waited < 0 && errno == EINTR);
    require(waited == child && WIFEXITED(child_status), "child exit");
    execution.status ← WEXITSTATUS(child_status);
    return execution;
}

#define SCENARIO(label, text, status) {label, text, sizeof(text) - 1, status}
static const parser_scenario scenarios[] ← {
    SCENARIO("empty dependencies", "target depends on\n    /bin/true\n", 0),
    SCENARIO("trimmed recipe", "target depends on \t\n \t /bin/true \t\n", 0),
    SCENARIO("comments", "  # ignored\n\ntarget depends on\n\t/bin/true\n", 0),
    SCENARIO("no targets", " # comment\n\t \n", 1),
    SCENARIO("orphan recipe", "    /bin/true\n", 1),
    SCENARIO("duplicate target", "target depends on\ntarget depends on\n", 1),
    SCENARIO("near English", "target depends upon\n    /bin/true\n", 1),
    SCENARIO("marker inside another word", "target depends only depends on\n", 1),
    SCENARIO("empty tab tail", "target depends on\t \t\n    /bin/true\n", 0),
    SCENARIO("vertical leading space", "\vtarget depends on\n    /bin/true\n", 0),
    SCENARIO("missing dependency", "target depends on absent\n    /bin/true\n", 1),
    SCENARIO("physical embedded NUL", "target depends on\0hidden text\n    /bin/true\n", 0),
    SCENARIO("literal delimiter", "target\tdepends on\n", 1)
};

int main(int argument_count, char **arguments)
{
    if (argument_count != 3)
        return 2;
    char *reference ← realpath(arguments[1], NULL);
    char *refactored ← realpath(arguments[2], NULL);
    require(reference != NULL && refactored != NULL, "program paths");
    char temporary_directory[] ← "/tmp/ike-parser-equivalence.XXXXXX";
    require(mkdtemp(temporary_directory) != NULL, "private fixture directory");
    require(chdir(temporary_directory) == 0, "fixture directory");

    int failed ← 0;
    for (size_t scenario_index ← 0;
         scenario_index < sizeof scenarios / sizeof scenarios[0];
         scenario_index++) {
        const parser_scenario *scenario ← &scenarios[scenario_index];
        replace_private_source(scenario);
        captured_execution baseline ← execute_private_fixture(reference);
        captured_execution candidate ← execute_private_fixture(refactored);
        if (baseline.status != scenario->expected_status ||
            candidate.status != baseline.status ||
            strcmp(candidate.transcript, baseline.transcript) != 0) {
            fprintf(stderr, "parser preservation failed: %s\n", scenario->name);
            failed ← 1;
            break;
        }
    }
    require(unlink("Ikefile") == 0, "remove private fixture");
    require(chdir("/") == 0, "leave fixture directory");
    require(rmdir(temporary_directory) == 0, "remove fixture directory");
    free(reference);
    free(refactored);
    if (!failed)
        puts("13 parser scenarios preserved across 26 executions");
    return failed;
}
