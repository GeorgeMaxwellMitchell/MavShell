/*

  Name: George Mitchell
  ID:   1001429081

*/


/***********
 * HEADERS *
 ***********/

/* Standard C headers */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <signal.h>

/* POSIX headers */
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>


/**************************
 * Macro limits defitions *
 **************************/

#define MAX_IN     256          /* Max input length */
#define MAX_TOK    10           /* Max number of tokens */
#define MAX_CMDHIS 15           /* Max number of commands to remember */
#define MAX_PIDHIS 15           /* Max number of process ID's to remember */

/**********************
 * Struct definitions *
 **********************/

/** \struct msh_t
    Holds user input and its derived tokens
*/
typedef struct msh_t {
  char in[MAX_IN];              /* Raw user input */
  char argv[MAX_TOK][MAX_IN];   /* Holds generated tokens */
  int argc;                     /* Count of tokens generated */
} msh_t;

/** \struct cmdhis_t
    Stores command history
*/
typedef struct cmdhis_t {
  char in[MAX_CMDHIS][MAX_IN];  /* Array of inputs passed into msh */
  int n;                        /* Total number of inputs tracked */
  int r;                        /* Location of command to rerun */
} cmdhis_t;

/** \struct pidhis_t
    Stores process ID history
*/
typedef struct pidhis_t {
  pid_t pid[MAX_PIDHIS];        /* Array of PIDs spawned by msh */
  pid_t cur;                    /* Current PID */
  int n;                        /* Total number of PID's tracked */
} pidhis_t;

/** \struct tok_t
    Token type used when adding tokens to msh_t.argv
*/
typedef struct tok_t {
  char str[MAX_IN];             /* token to send to argv */
  int len;                      /* length of token */
} tok_t;


/***********************
 * Function prototypes *
 ***********************/

/* Note: function descriptions are above their definitions */

/* Process management */
static void sig_hnd(int sig);
int msh_runproc(msh_t* msh, pidhis_t* ph);
void msh_genloc(char* loc, const char* path, const char* cmd);
void msh_genargvproc(char** pa, msh_t* msh);
void msh_freeargvproc(char** argv, int argc);
void msh_uppidhis(pidhis_t* ph, pid_t pid);
void msh_showpidhis(pidhis_t* ph);

/* Handling internal commands */
int msh_run(msh_t* msh, cmdhis_t* ch, pidhis_t* ph);
int msh_quit(const char* cmd);
void msh_cd(msh_t* msh);

/* Token generation */
void msh_gentok(msh_t* msh);
void msh_addtok(msh_t* msh, tok_t* tok);

/* Command History helper functions */
void msh_upcmdhis(cmdhis_t* h, msh_t* msh);
void msh_showcmdhis(cmdhis_t* cmdhis);
int  msh_getruncmd(const char* cmd);


/********************
 * Global variables *
 ********************/

/* Note: struct descriptions are above their definitions */
static msh_t        msh;
static cmdhis_t     ch;
static pidhis_t     ph;

/* Directory list to search for requested program to run sorted by priority */
static char* const  envp[] = {"./", "/usr/local/bin/", "/usr/bin/", "/bin/"};

/* Length of envp */
static const int    envc   = 4;


/**************
 * Entrypoint *
 **************/
 
/** \fn main
    Entry point of program
*/
int main() {
  
  /* Set all global vars to 0 */
  memset(&msh, 0, sizeof(msh));
  memset(&ch,  0, sizeof(ch) );
  memset(&ph,  0, sizeof(ph) );

  /* Register signal handler */
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = &sig_hnd;
  if (sigaction(SIGINT,  &sa, NULL) < 0) { return -1; }
  if (sigaction(SIGTSTP, &sa, NULL) < 0) { return -1; }

  /* Determines if the input was handled by msh 
   * If not handled by msh, then tries to spawn process */
  int handled;

  int running = 1;
  while (running) { /* Program loop */

    memset(&msh, 0, sizeof(msh)); /* Reset input container */
    handled = 0;                  /* Reset handled condition */

    /* Obtain input */            /* If ch.r != 0 then re-running cmd */
    if (ch.r) {                   /* from history at location ch.r-1  */

      /* Rerun command in history at index ch.r-1 */
      strcpy(msh.in, ch.in[ch.r-1]);
      ch.r = 0;

    } else {

      /* Else, wait for user input */
      fprintf(stdout, "msh>");
      fgets(msh.in, MAX_IN-1, stdin);

    }

    /* Create tokens
     * Takes raw user input and places each word separated by a space into a 
     *  2D char array (like main's argv)) */
    msh_gentok(&msh);
    
    /* Check for quit token (did user type 'exit' or 'quit'?)*/
    if (msh_quit(msh.argv[0])) {
      handled = 1;
      running = 0;
    }

    /* Check if native command */
    handled = msh_run(&msh, &ch, &ph);

    /* If not handled as native, try to spawn process */
    if (!handled) {
      handled = msh_runproc(&msh, &ph);
    }
    
    /* If not handled as process, command not found */
    if (!handled) {
      fprintf(stderr, "%s: Command not found.\n", msh.argv[0]);
    }
    
    /* Save input in history */
    msh_upcmdhis(&ch, &msh);

    /* history command */
    if (!strcmp(msh.argv[0], "history")) { msh_showcmdhis(&ch); }

  } /* End of program loop */

  /* End of program */
  return 0;

} /* main */


/**********************
 * Process management *
 **********************/

/* Handler for the SIGINT and SIGTSTP signals
 * Passes the two signals to the child process if one exists
 * Params: signal received
 * Outputs nothing */
static void sig_hnd(int sig) { } /* sig_hnd */

/* Responsible for spawning processes and logging their pid
 * Params: user input and PID history
 * Outputs 1 on handled; 0 on not handled */
int msh_runproc(msh_t* msh, pidhis_t* ph) {
  pid_t p = fork();
  if (p > 0) {
    /* Parent proc */
    int cr;
    wait(&cr);
    if (cr == (EXIT_FAILURE << 8)) return 0; /* _exit() shifts left by 8 */
    msh_uppidhis(ph, p); /* log pid if exec ran */
  } else if (p == 0) {
    /* Begin child proc */
    char* proc_argv[MAX_TOK+1];
    msh_genargvproc(proc_argv, msh);  /* Gen argv for exec */
    char loc[MAX_IN];
    int i;
    for(i = 0; i < envc; i++) { /* Try all locations */
      msh_genloc(loc, envp[i], msh->argv[0]);
      execv(loc, proc_argv);
    }
    msh_freeargvproc(proc_argv, msh->argc); /* Free argv for exec */
    _exit(EXIT_FAILURE); /* Return on exec failure */ 
    /* End child proc */
  } else {
    fprintf(stderr, "msh: could not spawn child process\n");
  }
  return 1;
} /* msh_runproc */

/* Generates the file location for exec
 * Params: location = path + command
 * Outputs nothing */
void msh_genloc(char* loc, const char* path, const char* cmd) {
  memset(loc, 0, MAX_IN);
  strcat(loc, path);
  strcat(loc, cmd);
} /* msh_genloc */

/* Generates an array of args for exec that are null terminated
 * Params: array to store args and msh_t with tokens
 * Outputs nothing */
void msh_genargvproc(char** argv, msh_t* msh) {
  if (argv == NULL || msh == NULL) { return; }
    int i;
    for (i = 0; i < msh->argc; i++) {
      argv[i] = (char*)malloc(sizeof(char)*MAX_IN);
      memset(argv[i], 0, sizeof(char)*MAX_IN);
      strcpy(argv[i], msh->argv[i]);
    }
    argv[msh->argc] = NULL;
} /* msh_genargvproc */

/* Frees arguments passed to exec
 * Params: array of args and arg count
 * Outputs nothing */
void msh_freeargvproc(char** argv, int argc) {
  if (argv == NULL) { return; }
  int i;
  for (i = 0; i < argc; i++) {
      free(argv[i]);
  }
} /* msh_freeargvproc */

/* Updates PID History
 * Params: PID history and pid to add
 * Outputs nothing */
void msh_uppidhis(pidhis_t* ph, pid_t pid) {
  if (ph == NULL || pid == 0) { return; }
  if (ph->n < MAX_PIDHIS) {
    ph->n++;
  } else {
    int i;
    for (i = 1; i < MAX_PIDHIS; i++) {
      ph->pid[i-1] = ph->pid[i];
    }
  }
  ph->pid[ph->n-1] = pid;
  ph->cur = pid;
} /* msh_uppidhis */

/* Shows list of past processes
 * Params: PID history
 * Outputs nothing */
void msh_showpidhis(pidhis_t* ph) {
  int i;
  for (i = 0; i < ph->n; i++) {
    fprintf(stdout, "%d: %d\n", i, ph->pid[i]);
  }
} /* msh_showpidhis */


/******************************
 * Handling internal commands *
 ******************************/

/* Analyzes first arg checking if we handle the command natively
 * Also performs action for said arg if can handle it natively
 * Params: user input, cmd history, pid history
 * Outputs 1 on handled, 0 if unhandled */
int msh_run(msh_t* msh, cmdhis_t* ch, pidhis_t* ph) {
  const char* cmd = msh->argv[0];

  /* If somehow escaped fgets call without pressing enter, add one newline */
  if (msh->in[0] == 0) {
    fprintf(stdout, "\n");
    return 1;
  }

  /* Do nothing on blank input */
  if (cmd[0] == 0) { return 1; }
    
  /* Check for quit command */
  if (msh_quit(cmd)) { return 1; }
    
  /* cd command encountered */
  if (!strcmp(cmd, "cd")) {
    msh_cd(msh);
    return 1;
  }

  /* history command
   * Must show history after updating, so is handled in main loop */
  if (!strcmp(cmd, "history")) { return 1; }

  /* showpids command */
  if (!strcmp(cmd, "showpids")) {
    msh_showpidhis(ph);
    return 1;
  }

  /* Repeat command in history */
  if (cmd[0] == '!') {
    int r = msh_getruncmd(cmd);
    if (!r) { return 0; }
    if (r > ch->n) { /* User passed some r greater than current max index n */
      fprintf(stderr, "Command not in history.\n");
      return 1;
    }
    ch->r = r;
    return 1;
  }
  
  /* bg command */
  if (!strcmp(cmd,"bg") && ph->cur) {
    kill(ph->cur, SIGCONT);   /* Sent signal to child process */
    ph->cur = 0;
    return 1;
  }

  return 0;
} /* msh_run */

/* Check for quit message
 * Params: input command (msh.argv[0])
 * Outputs 1 on quit command, 0 otherwise */
int msh_quit(const char* cmd) {
  if (!strcmp(cmd, "quit") || !strcmp(cmd, "exit")) { return 1; }
  return 0;
} /* msh_quit */


/* Handles switching directory
 * Params: user input
 * Outputs nothing */
void msh_cd(msh_t* msh) {
  if (msh == NULL) { return; }
  if (!strcmp(msh->argv[1], "..")) { /* Move up 1 dir */
    char cwd[MAX_IN] = {0};
    getcwd(cwd, sizeof(cwd)); /* get working dir */
    int i;
    for (i = strlen(cwd)-1; cwd[i] != '/'; i--) { /* strip cwd of last dir */
      cwd[i] = 0;
    }
    chdir(cwd);
  } else { /* Else, just cd to dir */
    int e = chdir(msh->argv[1]);
    if (e < 0) { /* Pass cd errors to user*/
      switch(errno) {
        case(ENOENT):
          fprintf(stderr, "msh: Directory does not exist\n");
          break;
        case(ENOTDIR):
          fprintf(stderr, "msh: Not a directory\n");
      }
    }
  }
} /* msh_cd */


/********************
 * Token generation *
 ********************/

/* Fills msh_t->argv & msh_t->argc based on msh_t->in
 * Params: user input
 * Ouputs nothing */
void msh_gentok(msh_t* msh) {
  if (msh == NULL) { return; }
  tok_t tok;
  memset(&tok, 0, sizeof(tok));
  const char* in = msh->in;
  int i;
  for (i = 0; in[i] == ' ' || in[i] == '\t'; i++); /* Remove prior spacing */
  while (i < (int)strlen(in) && msh->argc < MAX_TOK) {
    char c = in[i]; /* Current char */
    if (c == ' ' || c == '\t' || c == '\n') {
      msh_addtok(msh, &tok); /* Attempt to add token */
    } else if (c >= '!' || c <= '~') { /* ASCII range accepting */
      tok.str[tok.len] = c; /* Add char to token */
      tok.len++;
    }
    i++;
  }
} /* msh_gentok */

/* Adds token to argv and increments argc; memset's tok to 0
 * Params: user input, token to add
 * Outputs nothing */
void msh_addtok(msh_t* msh, tok_t* tok) {
  if (msh == NULL || tok == NULL) { return; }
  if (tok->str[0] == 0) { return; } /* return if blank token */
  sprintf(msh->argv[msh->argc], "%s", tok->str);
  msh->argc++;
  memset(tok, 0, sizeof(tok_t)); /* reset token struct */
} /* msh_addtok */


/****************************
 * History helper functions *
 ****************************/

/* Adds new entry to history
 * Params: cmd history, user input to add
 * Outputs nothing */
void msh_upcmdhis(cmdhis_t* ch, msh_t* msh) {
  if (ch == NULL || msh->in == NULL) { return; }
  if (msh->argv[0][0] == 0) { return; } /* Don't add empty input */
  if (ch->r) { return; } /* Don't add !x commands */
  const char* in = msh->in;
  if (ch->n < MAX_CMDHIS) {
    strcpy(ch->in[ch->n], in);
    ch->n++;
  } else { /* MAX_CMDHIS reached */
    int i;
    for (i = 0; i < MAX_CMDHIS; i++) { /* Push back history */
      memset(ch->in[i], 0, MAX_IN);
      strcpy(ch->in[i], ch->in[i+1]);
    }
    memset(ch->in[MAX_CMDHIS-1], 0, MAX_IN); /* Insert new input at end*/
    strcpy(ch->in[MAX_CMDHIS-1], in);
  }
} /* msh_upcmdhis */

/* Outputs command history
 * Params: cmd history
 * Outputs nothing */
void msh_showcmdhis(cmdhis_t* ch) {
  if (ch == NULL || ch->n == 0) { return; }
  int i;
  for (i = 0; i < ch->n; i++) {
    fprintf(stdout, "%d: %s", i+1, ch->in[i]);
  }
} /* msh_showcmdhis */

/* Returns history in[] index to rerun; otherwise 0
 * Params: user command (msh.argv[0]) (some !x)
 * Outputs the number entered by user after ! symbol */
int msh_getruncmd(const char* cmd) {
  if (cmd == NULL) { return 0; }
  char buff[3] = {0};
  int i;
  for (i = 1; cmd[i] != 0 && i < sizeof(buff); i++) {
    buff[i-1] = cmd[i];
  }
  int r = atoi(buff);
  if (r < 0 || r > MAX_CMDHIS) { return 0; } /* Out of bounds */
  return r;
} /* msh_getruncmd */
