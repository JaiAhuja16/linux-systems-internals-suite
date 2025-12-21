#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#define rep(i,k,n) for(int i=k;i<n;i++)
#define MAX_HISTORY 1000        // max history entries
#define MAX_PIPES 64            // max piped cmds per input
#define MAX_ARGS 128            // max args per cmd

typedef struct {
    char *cmd;                     // cmd string
    pid_t pids[MAX_PIPES];         // PIDs of forked processes
    int n_pids;                    // no. of pids
    time_t st;             // timestamp of input
    double duration;               // exec time (s)
} hst;

static hst history[MAX_HISTORY];
static int hcount = 0;               // no. of history entries
static volatile sig_atomic_t stopped = 0;

// SIGINT handler
void onint(int sig) {
    (void)sig;
    stopped = 1;
}

// whitespace handler(left and right)
char *trim(char *s) {
    if (!s) return s;
    while (*s == ' ' || *s == '\t') s++;
    char *end = s + strlen(s) - 1;
    while (end >= s && (*end == ' ' || *end == '\t')) { *end = '\0'; end--; }
    return s;
}

// string --> args array
char **parse(char *s, int *argc) {
    char **res = malloc(MAX_ARGS * sizeof(char *));
    if (!res) return NULL;
    *argc = 0;
    char *token = strtok(s, " \t");
    while (token && *argc < MAX_ARGS - 1) {
        res[(*argc)++] = token;
        token = strtok(NULL, " \t");
    }
    res[*argc] = NULL;
    return res;
}

// add commands to history
hst *add_to_his(const char *cmd){
    if (hcount >= MAX_HISTORY) {
        free(history[0].cmd);
        memmove(&history[0], &history[1], sizeof(hst) * (MAX_HISTORY - 1));
        hcount = MAX_HISTORY - 1;
    }
    hst *entry = &history[hcount];
    entry->cmd = strdup(cmd);
    if (!entry->cmd) { perror("strdup"); return NULL; }
    entry->n_pids = 0;
    entry->st = time(NULL);
    entry->duration = 0.0;
    hcount++;
    return entry;
}

// print history
void print_history(void) {
    rep(i, 0, hcount) printf("%d %s\n", i + 1, history[i].cmd ? history[i].cmd : "(null)");
}

// custom func for cd command
int handle_cd(char *path) {
    if (!path || strcmp(path, "~") == 0) {
        // change to home for (no arg or ~)
        path = getenv("HOME");
        if (!path) {
            fprintf(stderr, "cd: HOME environment variable not set\n");
            return -1;
        }
    }

    if (chdir(path) != 0) {
        perror("cd");
        return -1;
    }
    return 0;
}

// execution report
void report(void) {
    printf("\n--- Report ---\n");
    rep(i, 0, hcount){
        printf("Command %d: %s\n", i + 1, history[i].cmd ? history[i].cmd : "(null)");
        printf("-> Timestamp: %s", ctime(&history[i].st));
        printf("-> Duration: %.3fs\n", history[i].duration);
        printf("-> PIDs: ");
        rep(j,0,history[i].n_pids) printf("%d ", history[i].pids[j]);
        printf("\n\n");
        free(history[i].cmd);
        history[i].cmd = NULL;
    }
    exit(0);
}

int main(void) {
    if (signal(SIGINT, onint) == SIG_ERR) {
        perror("signal");
        return 1;
    }

    char input[4096];

    while (1) {
        if (stopped) report();

        printf("simple-shell$ ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin)) report();

        input[strcspn(input, "\n")] = 0;

        if (!input[0]) continue;

        // history command
        if (!strcmp(input, "history")) {
            add_to_his(input);
            print_history();
            continue;
        }

        // exit command
        if (!strcmp(input, "exit")) {
            add_to_his(input);
            printf("Exiting simple shell. Goodbye!\n");
            report();
        }

        // parse and handle built ins
        int argc;
        char *cmd_copy = strdup(input);
        if (!cmd_copy) {
            perror("strdup");
            continue;
        }
        char **argv = parse(cmd_copy, &argc);
        if (!argv) {
            free(cmd_copy);
            perror("malloc");
            continue;
        }

        if (argc > 0) {
            if (strcmp(argv[0], "cd") == 0) {
                hst *entry = add_to_his(input);
                if (argc > 2)
                    fprintf(stderr, "cd: too many arguments\n");
                else
                    handle_cd((argc == 2) ? argv[1] : NULL);
                if (entry) entry->duration = 0.0;
                free(cmd_copy);
                free(argv);
                continue; 
            }
        }

        free(cmd_copy);
        free(argv);

        hst *entry = add_to_his(input);
        if (!entry) continue;

        // split in pipeline stages
        char *commands[MAX_PIPES], *saveptr = NULL, *w = input;
        int npipes = 0;
        char *stage = strtok_r(w, "|", &saveptr);
        while (stage && npipes < MAX_PIPES - 1) {
            commands[npipes++] = trim(stage);
            stage = strtok_r(NULL, "|", &saveptr);
        }
        commands[npipes] = NULL;

        int in_fd = STDIN_FILENO, fd[2];
        struct timespec t_start, t_end;
        clock_gettime(CLOCK_MONOTONIC, &t_start);

        rep(i, 0, npipes) {
            if (i < npipes - 1 && pipe(fd) < 0) {
                perror("pipe");
                break;
            }

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                break;
            }

            if (!pid) {
                // child proc
                signal(SIGINT, SIG_DFL);

                // prev pipe
                if (in_fd != STDIN_FILENO) {
                    if (dup2(in_fd, STDIN_FILENO) < 0) {
                        perror("dup2 in"); _exit(1);
                    }
                    close(in_fd);
                }
                // next pipe
                if (i < npipes - 1) {
                    close(fd[0]);
                    if (dup2(fd[1], STDOUT_FILENO) < 0) {
                        perror("dup2 out"); _exit(1);
                    }
                    close(fd[1]);
                }

                int argc;
                char *cmd_copy = strdup(commands[i]);
                if (!cmd_copy) { perror("malloc"); _exit(127); }
                char **argv = parse(cmd_copy, &argc);
                if (!argv) { perror("malloc"); free(cmd_copy); _exit(127); }
                if (!argc) { free(cmd_copy); free(argv); _exit(0); }
                execvp(argv[0], argv);
                perror("execvp");
                free(cmd_copy);
                free(argv);
                _exit(127);
            }
            else {
                // saving child pids;
                if (entry->n_pids < MAX_PIPES) entry->pids[entry->n_pids++] = pid;
                if (in_fd != STDIN_FILENO) close(in_fd);
                if (i < npipes - 1) {
                    close(fd[1]);
                    in_fd = fd[0];
                }
            }
        }

        // wait for child processes
        rep(i, 0, entry->n_pids) {
            int status;
            pid_t w;
            do { w = waitpid(entry->pids[i], &status, 0); } while (w == -1 && errno == EINTR);
        }
        if (in_fd != STDIN_FILENO) close(in_fd);

        clock_gettime(CLOCK_MONOTONIC, &t_end);
        entry->duration = (t_end.tv_sec - t_start.tv_sec) + (t_end.tv_nsec - t_start.tv_nsec) / 1e9;
    }
    return 0;
}
