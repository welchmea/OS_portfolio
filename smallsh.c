#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdint.h>
#include <signal.h>

#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char * expand(char const *word);
int bg_flag = 0; // there can be multiple bg jobs
int fg_job = 0; //can only be one foreground job at a time; currently interacing with
char *input = NULL;
pid_t bg_job = 0;
pid_t bg_proc = 0;
pid_t p_pid = 0;
int inter_input = 0;

// set up function for signals
void sigint_handler(int sig) {
  sig = SIGINT;
}

int main(int argc, char *argv[])
{
  FILE *input = stdin;
  char *input_fn = "(stdin)";
  if (argc == 2) {
    input_fn = argv[1];
    input = fopen(input_fn, "re");
    if (!input) err(1, "%s", input_fn);
  } else if (argc > 2) {
    errx(1, "too many arguments");
  }

  char *line = NULL;
  size_t n = 0;
  
  // signal handling; C
  struct sigaction sigint = {0};
  struct sigaction oldact = {0};
 
   // signal handling for Z
  struct sigaction oldstp = {0};
  struct sigaction sigstp = {0};
  
  for (;;) {

  int child;
  int size_words = 0;

  prompt:;

  for (int i=0; i<size_words; i++){
    free(words[i]);   
    words[i] = NULL;
   }
   size_words = 0;

   // manage background processes -- check for UNWAITED FOR background processes in same process ID group
   if ((bg_proc = waitpid(0, &child, WNOHANG | WUNTRACED)) > 0) {
 
          if (WIFEXITED(child)) { 
            fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t)bg_proc, WEXITSTATUS(child)); 
          }
   
          else if (WIFSIGNALED(child)){
           fg_job = WTERMSIG(child); 
           fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t)bg_proc, fg_job);
          }
        
          else if (WIFSTOPPED(child)) { 
             fprintf(stderr,  "Child process %jd stopped. Continuing.\n", (intmax_t)bg_proc); 
             kill(bg_proc, SIGCONT);
          }
       }
 
    bg_flag = 0;
  
    // prompt for reading from stdin 
    if (input == stdin) {
      char *PS1 = getenv("PS1");
      if (PS1 == NULL) {
        PS1 = ""; 
      }
      // activate SIGINT/SIGTSTP for interactive mode 
      sigint.sa_handler = sigint_handler;
      sigaction(SIGINT, &sigint, &oldact);
      sigstp.sa_handler = SIG_IGN;
      sigaction(SIGTSTP, &sigstp, &oldstp);

      //print to stderr
      fprintf(stderr, "%s", PS1);
    }

    // read from input, take care of EOF, errors, and signals 
    ssize_t line_len = getline(&line, &n, input);
    sigint.sa_handler = SIG_IGN;
    sigaction(SIGINT, &sigint, NULL);
    sigaction(SIGTSTP, &sigstp, NULL);

    if (feof(input) != 0) exit(0);
  
    if (strcmp(line, "\n") == 0) goto prompt;
     
    // check errno for EINTR, go back to prompt wtih a newline 
    if (line_len < 0) { 

      //signal interruption for interactive mode
      if (errno == EINTR) {
        clearerr(input);
        fprintf(stderr, "%c", '\n');
        errno = 0; 
        goto prompt;
      }
      err(1, "%s", input_fn); 
      }
 
    // split words into a array 
    size_t nwords = wordsplit(line);
    size_words = nwords;
    
    //expansion 
    for (size_t i = 0; i < nwords; ++i) {  
      char *exp_word = expand(words[i]); 
      free(words[i]);
      words[i] = exp_word;
    }

    // exit built-in, more than 1 arg or not int are not allowed, exit with value
    if (strcmp(words[0], "exit") == 0) { 
      if (words[2] != NULL) {
        errx(1, "you are trying to pass in too many arguments");
      }
      if (words[1] == NULL) exit(fg_job);

      if (words[1] != NULL) exit(atoi(words[1]));     
    }

    // cd built-in, one argument, if not spec. use HOME, see chdir
    if (strcmp(words[0], "cd") == 0) {
    
       if (words[2] != NULL) {
         errx(1, "you are trying to pass in too many arguments");
       }
      
       else if (words[1] == NULL) { 
         chdir(getenv("HOME"));
       }

       else if (words[1] != NULL) {
          chdir(words[1]);
       } 
       else if (chdir(words[1]) != 0) {
         errx(1, "could not change directories");
       }
      goto prompt;
    }

   else {
      // go through wordslist and set flag for bg...must be done outside of child 
      for (int i=0; i < nwords; i++){   
         if (i == nwords-1 && (strcmp(words[i], "&") == 0)) {  
            words[i] = NULL;
            bg_flag = 1;
            nwords -= 1;
          } 
        }  
     
     // create a new process 
     pid_t p_pid = fork();
  
      switch (p_pid) {
        case -1: 
          errno; //need to print diagnostic messaged
          goto prompt;

        case 0:
         
          // signal handling for child process 
         sigaction(SIGTSTP, &oldstp, NULL);
         sigaction(SIGINT, &oldact, NULL); 
         
         // redireciton, remove operands before passing to execvp 
         for (int i=0; i < nwords; i++){
            if(strcmp(words[i], "<") == 0){
              int in_file = open(words[i+1], O_RDONLY);
              words[i] = NULL; 
              int res = dup2(in_file, 0);
              if (res == -1){
                perror("source dup2()");
                exit(2);
              } 
            }
            else if (strcmp(words[i], ">") == 0){
              int out_file = open(words[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0777);
              words[i] = NULL;
              int res2 = dup2(out_file, 1);
              if (res2 == -1){
                perror("target dup2()");
                exit(2);
            }
          }
            else if (strcmp(words[i], ">>") == 0) {
               int append_file = open(words[i+1], O_WRONLY | O_APPEND);
               words[i] = NULL;
               int res3 = dup2(append_file, 1);
               if (res3 == -1){
                perror("target dup2()");
                exit(2);
               }
             } 
           } 
 
          if (execvp(words[0], words) == -1) {
            int errcode=errno;
            fprintf(stderr, "%s", "Your child process was not created..."); //print to sterr, exit with nonzero status
            exit(errcode);
          } 
          
        default:
          // & not present means child is fg proc, perform a blocking wait
          if (bg_flag == 0) { 
          p_pid = waitpid(p_pid, &child, WUNTRACED);
          }
          if (WIFEXITED(child)) { 
            fg_job = WEXITSTATUS(child); 
          }
          if (WIFSIGNALED(child)){
            fg_job = WTERMSIG(child) + 128;
          }
          if (WIFSTOPPED(child)) { 
            fprintf(stderr,  "Child process %jd stopped. Continuing.\n", (intmax_t)p_pid); 
            kill(p_pid, SIGCONT);
            bg_job = p_pid;
          }

         else if (bg_flag == 1) { 
           // & is present child becomes a bg process  
           bg_job = p_pid;
         }

       goto prompt;
     }
   }
  }
  return 0;
  }





char *words[MAX_WORDS] = {0};


/* Splits a string into words delimited by whitespace. Recognizes
 * comments as '#' at the beginning of a word, and backslash escapes.
 *
 * Returns number of words parsed, and updates the words[] array
 * with pointers to the words, each as an allocated string.
 */
size_t wordsplit(char const *line) {
  size_t wlen = 0;
  size_t wind = 0;

  char const *c = line;
  for (;*c && isspace(*c); ++c); /* discard leading space */

  for (; *c;) {
    if (wind == MAX_WORDS) break;
    /* read a word */
    if (*c == '#') break;
    for (;*c && !isspace(*c); ++c) {
      if (*c == '\\') ++c;
      void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
      if (!tmp) err(1, "realloc");
      words[wind] = tmp;
      words[wind][wlen++] = *c; 
      words[wind][wlen] = '\0';
    }
    ++wind;
    wlen = 0;
    for (;*c && isspace(*c); ++c);
  }
  return wind;
}


/* Find next instance of a parameter within a word. Sets
 * start and end pointers to the start and end of the parameter
 * token.
 */
char
param_scan(char const *word, char const **start, char const **end)
{
  static char const *prev;
  if (!word) word = prev;
  
  char ret = 0;
  *start = 0;
  *end = 0;
  for (char const *s = word; *s && !ret; ++s) {
    s = strchr(s, '$');
    if (!s) break;
    switch (s[1]) {
    case '$':
    case '!':
    case '?':
      ret = s[1];
      *start = s;
      *end = s + 2;
      break;
    case '{':;
      char *e = strchr(s + 2, '}');
      if (e) {
        ret = s[1];
        *start = s;
        *end = e + 1;
      }
      break;
    }
  }
  prev = *end;
  return ret;
}

/* Simple string-builder function. Builds up a base
 * string by appending supplied strings/character ranges
 * to it.
 */
char *
build_str(char const *start, char const *end)
{ 
  static size_t base_len = 0;
  static char *base = 0;

  if (!start) {
    /* Reset; new base string, return old one */
    char *ret = base;
    base = NULL;
    base_len = 0;
    return ret;
  }
  /* Append [start, end) to base string 
   * If end is NULL, append whole start string to base string.
   * Returns a newly allocated string that the caller must free.
   */
  size_t n = end ? end - start : strlen(start); 
  size_t newsize = sizeof *base *(base_len + n + 1);
  void *tmp = realloc(base, newsize);
  if (!tmp) err(1, "realloc");
  base = tmp;
  memcpy(base + base_len, start, n);
  base_len += n;
  base[base_len] = '\0';
  return base;
}

/* Expands all instances of $! $$ $? and ${param} in a string 
 * Returns a newly allocated string that the caller must free
 */
char *
expand(char const *word)
{
  // convert pid status to a correct format 
  pid_t pid = getpid();
  char *a_pid = NULL;
  asprintf(&a_pid, "%d", pid);

  // convert last forground status to correct format
  char *a_fg_job = NULL;
  asprintf(&a_fg_job, "%d", fg_job);

  // convert bg_job into char
  char *a_bg_job = NULL;
  if (bg_job == 0) {
    a_bg_job = "";
  }
  else asprintf(&a_bg_job, "%d", bg_job);
   
  char const *pos = word;
  char const *start, *end;
  char c = param_scan(pos, &start, &end); 
  build_str(NULL, NULL);
  build_str(pos, start);
  while (c) {
    if (c == '!') build_str(a_bg_job, NULL);      //replaced with PID of most recent background process
    else if (c == '$') build_str(a_pid, NULL);
    else if (c == '?') build_str(a_fg_job, NULL); //replaced with exit status of last foreground command
    else if (c == '{') {
      char res[MAX_WORDS] = {'\0'};
      strncpy(res, start+2,((end-1)-(start+2)));
      char *envvar = getenv(res);  
      if (envvar == NULL) {
        envvar = "";
      }  
      build_str(envvar, NULL);
    
    }
    pos = end; 
    c = param_scan(pos, &start, &end);
    build_str(pos, start);

  }  
  return build_str(start, NULL);
}

