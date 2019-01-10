#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
int cmd_pwd(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);
int cmd_wait(struct tokens *tokens);

void set_sig_handlers(bool ignore);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_exit, "exit", "exit the command shell"},
  {cmd_pwd, "pwd", "shows current directory"},
  {cmd_cd, "cd", "go to given directory"},
  {cmd_wait, "wait", "waits for all background jobs to finish"}
};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens *tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens *tokens) {
  exit(0);
}

/* Prints current directory */
int cmd_pwd(unused struct tokens *tokens) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != 0) {
        printf("%s\n", cwd);
        return 0;
    }
    return 1;
}

/* Goes to given directory */
int cmd_cd(struct tokens *tokens) {
   if (tokens_get_length(tokens) < 2)
       return 1;
   if (chdir(tokens_get_token(tokens, 1)) == 0)
       return 0;
   return 1;
}

/* Waits for background jobs to finish */
int cmd_wait(struct tokens *tokens) {
    int status;
    int done = wait(&status);
    while (done != -1) {
        done = wait(&status);
    }
    return 0;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Executes a given function */
int run_function(struct tokens *tokens) {
    int tokens_len = tokens_get_length(tokens);
    if (tokens_len < 1)
        return 1;

    bool in_bg = strcmp(tokens_get_token(tokens, tokens_len - 1), "&") == 0;

    int pid = fork();
    if (pid == 0) {
        int argv_len = tokens_len;
        
        // define process group, move to foreground by default
        setpgid(getpid(), getpid());
        if (!in_bg) {
            tcsetpgrp(0, getpgrp());
        } else {
            argv_len -= 1;
        }

        set_sig_handlers(false);

        bool replace_out = false, replace_in = false;
        // compile arguments
        if (tokens_len >= 3) {
            if (strcmp(tokens_get_token(tokens, tokens_len - 2), ">") == 0) {
                argv_len -= 2;
                freopen(tokens_get_token(tokens, tokens_len - 1), "w+", stdout);
                replace_out = true;
            } else if (strcmp(tokens_get_token(tokens, tokens_len - 2), "<") == 0) {
                argv_len -= 2;
                freopen(tokens_get_token(tokens, tokens_len - 1), "r", stdin);
                replace_in = true;
            }
        }
        char *argv[argv_len + 1];
        for (int i = 0; i < argv_len; i++) {
            argv[i] = tokens_get_token(tokens, i);
        }
        argv[argv_len] = 0;

        // find correct path TODO factor this out
        char *program_path = tokens_get_token(tokens, 0);
        if (program_path[0] != '/') {
            printf("%s\n", program_path);
            char *env_path = getenv("PATH");
            char *curr_path = strtok(env_path, ":");
            char *try;
            while (curr_path != 0) {
                try = malloc(strlen(curr_path) + strlen(program_path) + 2);
                strcat(try, curr_path);
                strcat(try, "/");
                strcat(try, program_path);
                if (access(try, F_OK) != -1) {
                    program_path = try;
                    break;
                }
            }
            free(try);
        }
        
        // run program
        int val = execv(program_path, argv);
        if (replace_out) {
            fclose(stdout);       
        }
        if (replace_in) {
            fclose(stdin);
        }
        if (val == -1) {
            exit(1);
        } else {
            exit(0);
        }
    } else if (pid > 0) {
        if (!in_bg) {
            int status;
            wait(&status);
            
            // set shell back to foreground
            tcsetpgrp(0, getpgrp());      
        }
        return 0;
    } else {
        return 1;
    }
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

void set_sig_handlers(bool ignore) {
  signal(SIGINT, ignore ? SIG_IGN : SIG_DFL);
  signal(SIGQUIT, ignore ? SIG_IGN : SIG_DFL);
  signal(SIGTERM, ignore ? SIG_IGN : SIG_DFL);
  signal(SIGTSTP, ignore ? SIG_IGN : SIG_DFL);
  signal(SIGCONT, ignore ? SIG_IGN : SIG_DFL);
  signal(SIGTTIN, ignore ? SIG_IGN : SIG_DFL);
  signal(SIGTTOU, ignore ? SIG_IGN : SIG_DFL);
}

int main(unused int argc, unused char *argv[]) {
  init_shell();
  
  set_sig_handlers(true);

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens *tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      run_function(tokens);
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}
