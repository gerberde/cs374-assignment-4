#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/wait.h> 
#include <unistd.h>

#define INPUT_LENGTH 2048
#define MAX_ARGS 512

volatile sig_atomic_t foregroundMode = 0; // required to be global

void handleSIGTSTP(int signo) {
    if (foregroundMode == 0) {
        // Enter forgound only mode

		// no printf 
		// write() is async signal safe
		char msg[] = "\nEntering foreground-only mode (& is now ignored)\n: ";
        write(STDOUT_FILENO, msg, sizeof(msg) - 1);
        foregroundMode = 1;
    } else {
        // Exit foreground only mode
		char msg[] = "\nExiting foreground-only mode\n: ";
        write(STDOUT_FILENO, msg, sizeof(msg) - 1);
        foregroundMode = 0;
    }
}

// Stuct from exmaple parsing file in canvas
struct commandLine {
	char *argv[MAX_ARGS + 1];
	int argc;
	char *input_file;
	char *output_file;
	bool is_bg;
};

// Parsing function from exmaple parsing file in canvas
struct commandLine *parse_input() {
	char input[INPUT_LENGTH];
	struct commandLine *currCommand = (struct commandLine *) calloc(1, sizeof(struct commandLine));

	// Get input
	printf(": ");
	fflush(stdout);
	fgets(input, INPUT_LENGTH, stdin);

	// Tokenize the input
	char *token = strtok(input, " \n");
	while(token){
		if(!strcmp(token,"<")){
			currCommand->input_file = strdup(strtok(NULL," \n"));
		} else if(!strcmp(token,">")){
			currCommand->output_file = strdup(strtok(NULL," \n"));
		} else if(!strcmp(token,"&")){
			currCommand->is_bg = true;
		} else{
			currCommand->argv[currCommand->argc++] = strdup(token);
		}
		token=strtok(NULL," \n");
	}
	return currCommand;
}

void freeCommand(struct commandLine *currCommand) {
    if (currCommand == NULL) {
        return;
    }

    for (int i = 0; i < currCommand->argc; i++) {
        free(currCommand->argv[i]);
    }
    free(currCommand->input_file);
    free(currCommand->output_file);
    free(currCommand);
}

// for debug
int writeCommand (struct commandLine *currCommand) {
    printf("argv: ");
	for (int i = 0; i < currCommand->argc; i++) {
		printf("%s ", currCommand->argv[i]);
	}

	printf("\n");
	printf("argc: %d\n", currCommand->argc);

	if(currCommand->input_file != NULL) {
		printf("input file: %s\n", currCommand->input_file);
	}

	if(currCommand->output_file != NULL) {
		printf("output_file file: %s\n", currCommand->output_file);
	}

	printf("Backround flag: %s\n", currCommand->is_bg ? "true" : "false");

    return 0;
}

void initializeSignals() {
    // SIGINT (ctl-C): Ignore in Parent
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = SIG_IGN; 
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);

    // SIGTSTP (ctl-Z): Redirect to handleSIGTSTP
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = handleSIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART; 
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
}

void initializeChildSignals(bool is_bg) {
	struct sigaction SIGINT_action = {0};
    struct sigaction SIGTSTP_action = {0};

    // child ignores SIGTSTP 
    SIGTSTP_action.sa_handler = SIG_IGN;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = 0;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // SIGINT handling
    if (!is_bg) {
        // Foreground: terminate
        SIGINT_action.sa_handler = SIG_DFL;
    } else {
        // Background: ignore
        SIGINT_action.sa_handler = SIG_IGN;
    }

    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);
}


int main(){
    int exitProgram = 0;
	int exitStatusCode = 0;
    struct commandLine *currCommand;
	
	initializeSignals();

	// main loop
    while (exitProgram == 0) {
		// Check backround statuses 
		int bgStatus;
		pid_t bgPid;

		while ((bgPid = waitpid(-1, &bgStatus, WNOHANG)) > 0) {
			if (WIFEXITED(bgStatus)) {

				printf("background pid %d is done: exit value %d\n", bgPid, WEXITSTATUS(bgStatus));

			} else if (WIFSIGNALED(bgStatus)) {

				printf("background pid %d is done: terminated by signal %d\n", bgPid, WTERMSIG(bgStatus));

			}

			fflush(stdout); 
		}

		// get command
        currCommand = parse_input();
        // writeCommand(currCommand);

		if (strcmp(currCommand->argv[0], "#") == 0) {

			freeCommand(currCommand);
			continue;

		} else if (strcmp(currCommand->argv[0], "exit") == 0) {

    		exitProgram = 1;

		} else if (strcmp(currCommand->argv[0], "cd") == 0) {
			if (currCommand->argv[1] == NULL) {
				char *homeDirectory = getenv("HOME");
				// printf("Home directory: %s\n", homeDirectory); // debug
				if (homeDirectory != NULL) {
					chdir(homeDirectory);
				}
			} else {
				chdir(currCommand->argv[1]);
			} 
			
		} else if (strcmp(currCommand->argv[0], "status") == 0) {

			printf("exit value %d\n", exitStatusCode);

		} else {

			if (foregroundMode == 1) {
				currCommand->is_bg = false;
			}

			pid_t newPid = fork();

			switch(newPid) {
				case -1:
					printf("fork() failed\n"); // debug
					exit(1);
					break;

				case 0:
					// Child process

					initializeChildSignals(currCommand->is_bg);
					
					if (currCommand->input_file != NULL) {
						int inFd = open(currCommand->input_file, O_RDONLY);

						if (inFd < 0) {
							printf("cannot open %s for input\n", currCommand->input_file);
							_exit(1);
						}

						if (dup2(inFd, STDIN_FILENO) < 0) {
							perror("Failed to read dup2 stdin\n");
							close(inFd);
							_exit(1);
						}

						close(inFd);
					} 

					if (currCommand->output_file != NULL) {
						int outFd = open(currCommand->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

						if (outFd < 0) {
							printf("cannot open %s for output\n", currCommand->output_file);
							_exit(1);
						}

						if (dup2(outFd, STDOUT_FILENO) < 0) {
							perror("Failed to read dup2 stdout");
							close(outFd);
							_exit(1);
						}
						close(outFd);
					} 
					
					

					if (execvp(currCommand->argv[0], currCommand->argv) == -1) {
						printf("%s: no such file or directory\n", currCommand->argv[0]);
						exitStatusCode = 1;
						_exit(1); // forces termination
					}

					break;

				default:
					// Parent process

					if (!currCommand->is_bg) {
						// Foregound Process: Wait for pid
						int childStatus;
						waitpid(newPid, &childStatus, 0); 

						// Get status
						if (WIFEXITED(childStatus)) {
							// Exited normally
							exitStatusCode = WEXITSTATUS(childStatus);
						} 
						else if (WIFSIGNALED(childStatus)) {
							// Terminated by signal 
							exitStatusCode = WTERMSIG(childStatus);
							printf("terminated by signal %d\n", exitStatusCode);
							fflush(stdout);
						}

						
					} else {
						// Backround process
						printf("Background pid is %d\n", newPid);
						fflush(stdout);
						waitpid(newPid, NULL, WNOHANG); 
					}
					break;
			}
		}

		freeCommand(currCommand);

    }

    return 0;
}
