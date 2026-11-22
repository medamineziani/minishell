#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_TOKENS 512
#define MAX_CMDS 64

typedef struct {
    char *argv[MAX_TOKENS];
    char *infile;
    char *outfile;
    int append;
} Command;

static volatile sig_atomic_t child_changed = 0;

static void sigchld_handler(int sig) {
    (void)sig;
    child_changed = 1;
}

static void reap_children(void) {
    int status;
    pid_t pid;
    child_changed = 0;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        fprintf(stderr, "[minishell] processus %ld terminé\n", (long)pid);
    }
}

static char *read_line(void) {
    char *line = NULL;
    size_t cap = 0;
    ssize_t n = getline(&line, &cap, stdin);
    if (n < 0) {
        free(line);
        return NULL;
    }
    if (n && line[n - 1] == '\n') line[n - 1] = '\0';
    return line;
}

/* Découpe les mots en conservant les espaces à l'intérieur des quotes.
   Les opérateurs |, <, >, >> et & deviennent des tokens séparés. */
static int tokenize(const char *s, char **tokens) {
    int count = 0;
    size_t i = 0, n = strlen(s);

    while (i < n) {
        while (s[i] == ' ' || s[i] == '\t') i++;
        if (i >= n) break;
        if (count >= MAX_TOKENS - 1) {
            fprintf(stderr, "Erreur: trop de tokens\n");
            return -1;
        }

        if (s[i] == '|' || s[i] == '<' || s[i] == '&') {
            tokens[count++] = strndup(s + i, 1);
            i++;
        } else if (s[i] == '>') {
            if (i + 1 < n && s[i + 1] == '>') {
                tokens[count++] = strdup(">>");
                i += 2;
            } else {
                tokens[count++] = strdup(">");
                i++;
            }
        } else {
            char *word = calloc(n + 1, 1);
            size_t j = 0;
            char quote = 0;
            while (i < n) {
                char c = s[i];
                if (quote) {
                    if (c == quote) quote = 0;
                    else if (c == '\\' && quote == '"' && i + 1 < n) word[j++] = s[++i];
                    else word[j++] = c;
                    i++;
                } else {
                    if (c == '\'' || c == '"') { quote = c; i++; }
                    else if (c == '\\' && i + 1 < n) { word[j++] = s[i + 1]; i += 2; }
                    else if (c == ' ' || c == '\t' || c == '|' || c == '<' || c == '>' || c == '&') break;
                    else { word[j++] = c; i++; }
                }
            }
            if (quote) {
                fprintf(stderr, "Erreur: guillemet non fermé\n");
                free(word);
                return -1;
            }
            word[j] = '\0';
            tokens[count++] = word;
        }
    }
    tokens[count] = NULL;
    return count;
}

static void free_tokens(char **tokens, int n) {
    for (int i = 0; i < n; i++) free(tokens[i]);
}

static int parse_commands(char **t, int nt, Command *cmds, int *ncmd, int *background) {
    int c = 0, a = 0;
    memset(cmds, 0, sizeof(Command) * MAX_CMDS);
    *background = 0;
    if (nt == 0) { *ncmd = 0; return 0; }

    if (!strcmp(t[nt - 1], "&")) {
        *background = 1;
        nt--;
    }

    for (int i = 0; i < nt; i++) {
        if (!strcmp(t[i], "|")) {
            if (a == 0 || c >= MAX_CMDS - 1) return -1;
            cmds[c].argv[a] = NULL;
            c++; a = 0;
        } else if (!strcmp(t[i], "<") || !strcmp(t[i], ">") || !strcmp(t[i], ">>")) {
            if (i + 1 >= nt) return -1;
            if (!strcmp(t[i], "<")) cmds[c].infile = strdup(t[++i]);
            else {
                cmds[c].append = !strcmp(t[i], ">>");
                cmds[c].outfile = strdup(t[++i]);
            }
        } else {
            if (a >= MAX_TOKENS - 1) return -1;
            cmds[c].argv[a++] = strdup(t[i]);
        }
    }
    if (a == 0) return -1;
    cmds[c].argv[a] = NULL;
    *ncmd = c + 1;
    return 0;
}

static void free_commands(Command *cmds, int n) {
    for (int i = 0; i < n; i++) {
        for (int j = 0; cmds[i].argv[j]; j++) free(cmds[i].argv[j]);
        free(cmds[i].infile);
        free(cmds[i].outfile);
    }
}

static int apply_redirections(Command *cmd) {
    if (cmd->infile) {
        int fd = open(cmd->infile, O_RDONLY);
        if (fd < 0) { perror(cmd->infile); return -1; }
        if (dup2(fd, STDIN_FILENO) < 0) { perror("dup2"); close(fd); return -1; }
        close(fd);
    }
    if (cmd->outfile) {
        int flags = O_WRONLY | O_CREAT | (cmd->append ? O_APPEND : O_TRUNC);
        int fd = open(cmd->outfile, flags, 0644);
        if (fd < 0) { perror(cmd->outfile); return -1; }
        if (dup2(fd, STDOUT_FILENO) < 0) { perror("dup2"); close(fd); return -1; }
        close(fd);
    }
    return 0;
}

static int is_builtin(const char *s) {
    return s && (!strcmp(s, "cd") || !strcmp(s, "exit") ||
                 !strcmp(s, "pwd") || !strcmp(s, "help"));
}

static int run_builtin(char **argv) {
    if (!strcmp(argv[0], "cd")) {
        const char *dest = argv[1] ? argv[1] : getenv("HOME");
        if (!dest) { fprintf(stderr, "cd: HOME non défini\n"); return 1; }
        if (chdir(dest) < 0) { perror("cd"); return 1; }
        return 0;
    }
    if (!strcmp(argv[0], "pwd")) {
        char cwd[4096];
        if (!getcwd(cwd, sizeof cwd)) { perror("pwd"); return 1; }
        puts(cwd);
        return 0;
    }
    if (!strcmp(argv[0], "help")) {
        puts("Commandes internes: cd [dossier], pwd, help, exit");
        puts("Fonctionnalités: commandes Linux, |, <, >, >>, &, quotes simples/doubles.");
        return 0;
    }
    if (!strcmp(argv[0], "exit")) {
        exit(argv[1] ? atoi(argv[1]) : 0);
    }
    return 0;
}

static int execute_pipeline(Command *cmds, int n, int background) {
    int pipes[MAX_CMDS - 1][2];
    pid_t pids[MAX_CMDS];
    pid_t pgid = 0;

    if (n == 1 && is_builtin(cmds[0].argv[0]) && !background) {
        int saved_in = dup(STDIN_FILENO), saved_out = dup(STDOUT_FILENO);
        if (apply_redirections(&cmds[0]) < 0) {
            dup2(saved_in, STDIN_FILENO); dup2(saved_out, STDOUT_FILENO);
            close(saved_in); close(saved_out); return 1;
        }
        int result = run_builtin(cmds[0].argv);
        dup2(saved_in, STDIN_FILENO); dup2(saved_out, STDOUT_FILENO);
        close(saved_in); close(saved_out);
        return result;
    }

    for (int i = 0; i < n - 1; i++) {
        if (pipe(pipes[i]) < 0) { perror("pipe"); return 1; }
    }

    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            if (i > 0) dup2(pipes[i - 1][0], STDIN_FILENO);
            if (i < n - 1) dup2(pipes[i][1], STDOUT_FILENO);
            for (int k = 0; k < n - 1; k++) {
                close(pipes[k][0]); close(pipes[k][1]);
            }
            if (apply_redirections(&cmds[i]) < 0) _exit(1);
            if (is_builtin(cmds[i].argv[0])) _exit(run_builtin(cmds[i].argv));
            execvp(cmds[i].argv[0], cmds[i].argv);
            fprintf(stderr, "%s: commande introuvable ou impossible à exécuter\n", cmds[i].argv[0]);
            _exit(errno == ENOENT ? 127 : 126);
        }
        pids[i] = pid;
        if (pgid == 0) pgid = pid;
        setpgid(pid, pgid);
    }

    for (int i = 0; i < n - 1; i++) {
        close(pipes[i][0]); close(pipes[i][1]);
    }

    if (background) {
        printf("[arrière-plan] PID %ld\n", (long)pgid);
    } else {
        int status = 0;
        for (int i = 0; i < n; i++) {
            while (waitpid(pids[i], &status, 0) < 0 && errno == EINTR) {}
        }
    }
    return 0;
}

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    for (;;) {
        if (child_changed) reap_children();
        char cwd[1024];
        if (getcwd(cwd, sizeof cwd)) printf("minishell:%s$ ", cwd);
        else printf("minishell$ ");
        fflush(stdout);

        char *line = read_line();
        if (!line) { putchar('\n'); break; }
        char *tokens[MAX_TOKENS];
        int nt = tokenize(line, tokens);
        free(line);
        if (nt <= 0) { if (nt < 0) continue; else continue; }

        Command cmds[MAX_CMDS];
        int ncmd = 0, background = 0;
        if (parse_commands(tokens, nt, cmds, &ncmd, &background) < 0) {
            fprintf(stderr, "Erreur de syntaxe dans la commande\n");
        } else if (ncmd > 0) {
            execute_pipeline(cmds, ncmd, background);
            free_commands(cmds, ncmd);
        }
        free_tokens(tokens, nt);
    }
    return 0;
}
