#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdbool.h>

#define DEFAULT_HISTORY_SIZE 5

typedef struct ShellVar
{
    char *name;
    char *value;
    struct ShellVar *next;
} ShellVar;

ShellVar *shellVars = NULL;

typedef struct CommandHistory
{
    char **commands;
    int maxSize;
    int currSize;
} CommandHistory;

CommandHistory history = {NULL, DEFAULT_HISTORY_SIZE, 0};

void freeHistory()
{
    for (int i = 0; i < history.currSize; ++i)
    {
        free(history.commands[i]);
    }
    free(history.commands);
}

void freeShellVars(ShellVar *var)
{
    // Recursively free the list of shell variables
    if (var == NULL)
        return;
    freeShellVars(var->next);
    free(var->name);
    free(var->value);
    free(var);
}

void handleError()
{
    exit(-1);
}

void builtin_exit()
{
    freeHistory();
    freeShellVars(shellVars);
    exit(0);
}

void initHistory()
{
    history.commands = calloc(DEFAULT_HISTORY_SIZE, sizeof(char *));
    history.maxSize = DEFAULT_HISTORY_SIZE;
    history.currSize = 0;
}

void addHistory(const char *command)
{
    if (command == NULL)
    {
        return;
    }
    if (history.currSize > 0 && strcmp(history.commands[0], command) == 0)
    {
        return;
    }
    if ((history.currSize == history.maxSize) && (history.maxSize != 0))
    {
        free(history.commands[history.maxSize - 1]);
        for (int i = history.maxSize - 1; i > 0; i--)
        {
            history.commands[i] = history.commands[i - 1];
        }
    }
    else if (history.maxSize == 0)
    {
        return;
    }
    else
    {
        history.currSize++;
        for (int i = history.currSize - 1; i > 0; i--)
        {
            history.commands[i] = history.commands[i - 1];
        }
    }

    history.commands[0] = strdup(command);
}

void resizeHistory(int newSize)
{
    if (newSize == 0)
    {
        freeHistory();
        initHistory();
    }

    char **newCommands = calloc(newSize, sizeof(char *));

    int copyCount = history.currSize < newSize ? history.currSize : newSize;

    for (int i = 0; i < copyCount; i++)
    {
        newCommands[i] = history.commands[i];
    }

    if (history.commands != NULL)
    {
        free(history.commands);
    }
    history.commands = newCommands;
    history.maxSize = newSize;
    history.currSize = copyCount;
}

void printHistory()
{
    for (int i = 0; i < history.currSize; ++i)
    {
        printf("%d) %s\n", i + 1, history.commands[i]);
    }
}

void executeExternalCommand(char **args, char *line)
{
    pid_t pid;
    int status;

    pid = fork();
    addHistory(line);

    if (pid == 0)
    {
        // Child process
        if (execvp(args[0], args) == -1)
        {
            perror("execvp");
        }
        exit(EXIT_FAILURE);
    }
    else if (pid < 0)
    {
        // Error forking
        perror("fork");
    }
    else
    {
        // Parent process
        do
        {
            waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }
}

char **parseInput(char *inputLine)
{
    char *token;
    char *rest = inputLine;
    char *delimiter = " ";

    char **tokens = malloc(256 * sizeof(char *));
    int i = 0;

    while ((token = strsep(&rest, delimiter)) != NULL)
    {
        if (token[0] == '\0')
            continue; // skip empty tokens
        tokens[i++] = token;
    }
    tokens[i] = NULL;

    return tokens;
}

void builtin_history(char **args, char *line)
{
    // check args[1] for NULL (print history)
    if (args[1] == NULL)
    {
        printHistory();
    }
    else if (strcmp(args[1], "set") == 0)
    {
        resizeHistory(atoi(args[2]));
    }
    else if ((atoi(args[1]) > history.maxSize) || (history.commands[atoi(args[1]) - 1] == NULL))
    {
        return;
    }
    else
    {
        line = NULL;
        char **historyArgs = parseInput(history.commands[atoi(args[1]) - 1]);

        executeExternalCommand(historyArgs, line);
    }
}

void builtin_cd(char **args)
{
    if (args[1] == NULL || args[2] != NULL)
    {
        handleError();
    }
    else
    {
        if (chdir(args[1]) != 0)
        {
            handleError();
        }
    }
}

void builtin_export(char **args)
{
    if (args[1] != NULL)
    {
        char *var = strtok(args[1], "=");
        char *value = strtok(NULL, "");
        if (var == NULL)
        {
            handleError();
        }
        else if (value == NULL)
        {
            if (unsetenv(var) != 0)
            {
                handleError();
            }
        }
        else
        {
            if (setenv(var, value, 1) != 0)
            {
                handleError();
            }
        }
    }
    else
    {
        handleError();
    }
}

void builtin_vars()
{
    if (shellVars == NULL)
    {
        return;
    }

    for (ShellVar *var = shellVars; var != NULL; var = var->next)
    {
        printf("%s=%s\n", var->name, var->value);
    }
}

void builtin_local(char **args)
{
    if (args[1] == NULL)
    {
        handleError();
    }

    char *assignment = args[1];
    char *name = strtok(assignment, "=");
    char *value = strtok(NULL, "");

    bool remove = false;

    if (name == NULL)
    {
        handleError();
    }

    ShellVar *prev = NULL;
    ShellVar *current = shellVars;

    if (value == NULL)
    {
        remove = true;
    }

    while (current != NULL)
    {
        if (strcmp(current->name, name) == 0)
        {
            if (remove)
            {
                if (prev == NULL)
                {
                    shellVars = current->next;
                }
                else
                {
                    prev->next = current->next;
                }
                free(current);
                return;
            }
            else
            {
                // Variable exists, update its value
                free(current->value);
                current->value = strdup(value);
                return;
            }
        }
        prev = current;
        current = current->next;
    }

    // Variable not found, add new variable
    ShellVar *newVar = (ShellVar *)malloc(sizeof(ShellVar));
    newVar->name = strdup(name);
    newVar->value = strdup(value);
    newVar->next = NULL;

    if (prev)
    {
        prev->next = newVar;
    }
    else
    {
        shellVars = newVar;
    }
}

void displayPrompt()
{
    printf("vnsh> ");
}

void executeCommand(char **args, char *line)
{
    if (args[0] == NULL)
    {
        // An empty command was entered.
        return;
    }

    // Check for built-in commands
    if (strcmp(args[0], "cd") == 0)
    {
        builtin_cd(args);
    }
    else if (strcmp(args[0], "exit") == 0)
    {
        builtin_exit();
    }
    else if (strcmp(args[0], "export") == 0)
    {
        // Handle export
        builtin_export(args);
    }
    else if (strcmp(args[0], "local") == 0)
    {
        // Handle local
        builtin_local(args);
    }
    else if (strcmp(args[0], "vars") == 0)
    {
        // Handle vars
        builtin_vars();
    }
    else if (strcmp(args[0], "history") == 0)
    {
        // Handle history
        builtin_history(args, line);
    }
    else
    {
        // If not a built-in command, execute an external command
        executeExternalCommand(args, line);
    }
}

void batchProcessInput(FILE *inputSource, bool isInteractive)
{
    char line[1024]; // Adjust size as needed for the maximum expected line length

    // Read lines from inputSource until EOF
    while (fgets(line, sizeof(line), inputSource) != NULL)
    {
        // Remove newline character at the end of the line, if present
        line[strcspn(line, "\n")] = 0;

        // Tokenize the input line
        char **tokens = parseInput(line);
        executeCommand(tokens, line);

        free(tokens); // Free the array of tokens
        // Reset line buffer if needed
        // memset(line, 0, sizeof(line));
    }

    if (feof(inputSource))
    {
        handleError();
    }
    else if (ferror(inputSource))
    {
        // Error reading from inputSource
        perror("Error reading input");
    }
}

char *getShellVarValue(const char *varName)
{
    for (ShellVar *var = shellVars; var != NULL; var = var->next)
    {
        if (strcmp(var->name, varName) == 0)
        {
            return var->value;
        }
    }
    return NULL;
}

char *expandVars(char *origInputLine)
{
    const char *delimiter = " ";
    char *lineCopy = strdup(origInputLine);
    char *token = strtok(lineCopy, delimiter);
    int bufferSize = strlen(origInputLine) + 1;
    char *expandedLine = malloc(bufferSize);
    if (!expandedLine)
    {
        free(lineCopy);
        handleError();
    }
    expandedLine[0] = '\0';

    while (token)
    {
        if ((token[0] == '$') && (strlen(token) > 1))
        {
            char *varName = token + sizeof(char);
            char *value = getenv(varName);

            if (!value /* not a env var*/)
            {
                value = getShellVarValue(varName);
            }
            if (!value /* not a shell var either*/)
            {
                value = "";
            }

            bufferSize += strlen(value);
            expandedLine = realloc(expandedLine, bufferSize);
            strcat(expandedLine, value);
        }
        else
        { // just normal token
            strcat(expandedLine, token);
        }

        token = strtok(NULL, delimiter);
        if (token)
        {
            strcat(expandedLine, " ");
        }
    }
    free(lineCopy);

    return expandedLine;
}

void executePipeCommands(char *pipeCommand)
{
    addHistory(pipeCommand);
    int pipeCount = 0;
    for (int i = 0; i < strlen(pipeCommand); i++)
    {
        if (pipeCommand[i] == '|')
        {
            pipeCount++;
        }
    }

    char *commandArray[pipeCount + 1];
    int numCommands = 0;
    char *token = strtok(pipeCommand, "|");

    while (token != NULL)
    {
        commandArray[numCommands] = token;
        token = strtok(NULL, "|");
        numCommands++;
    }

    // unfunctional pipe
    if (pipeCount == numCommands)
    {
        return;
    }

    int pipes[numCommands - 1][2];
    for (int i = 0; i < numCommands - 1; i++)
    {
        if (pipe(pipes[i]) == -1)
        {
            handleError();
        }
    }

    // execute pipes iteratively
    for (int i = 0; i < numCommands; i++)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            handleError();
        }
        else if (pid == 0)
        {
            if (i != 0)
            {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) == -1)
                {
                    handleError();
                }
                close(pipes[i - 1][0]);
            }

            if (i != numCommands - 1)
            {
                if (dup2(pipes[i][1], STDOUT_FILENO) == -1)
                {
                    handleError();
                }
                close(pipes[i][1]);
            }

            for (int j = 0; j < numCommands - 1; j++)
            {
                if (j != i)
                {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
            }

            int numSpaces = 0;
            for (int n = 0; n < strlen(commandArray[i]); n++)
            {
                if (commandArray[i][n] == ' ')
                {
                    numSpaces++;
                }
            }

            char *args[numSpaces + 2];
            char *token;
            int argc = 0;
            token = strtok(commandArray[i], " ");
            while (token != NULL)
            {
                args[argc] = token;
                argc++;
                token = strtok(NULL, " ");
            }
            args[argc] = NULL;
            if (execvp(args[0], args) == -1)
            {
                printf("execvp: No such file or directory\n");
                return;
            }
        }
    }

    for (int i = 0; i < numCommands - 1; i++)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    for (int i = 0; i < numCommands; i++)
    {
        wait(NULL);
    }
}

void processInput(FILE *inputSource)
{
    char *line = NULL;
    size_t bufsize = 0; // getline will allocate the buffer
    ssize_t lineSize;

    // Read the input line
    lineSize = getline(&line, &bufsize, inputSource);

    // Check for read success
    if (lineSize > 0)
    {
        // Successfully read a line

        // Remove the newline character at the end, if present
        if (line[lineSize - 1] == '\n')
        {
            line[lineSize - 1] = '\0';
        }

        char *expandedLine = expandVars(line);

        // struct CommandNode *head = NULL;

        if (strstr(expandedLine, "|") != NULL)
        {
            // pipe mode
            executePipeCommands(expandedLine);
        }
        else
        {
            // Tokenize the input line into arguments
            char **args = parseInput(expandedLine);

            // Execute the command with the parsed arguments
            executeCommand(args, line);

            // Free the tokenized arguments

            free(args); // Free the array of argument strings
        }
    }
    else if (feof(inputSource))
    {
        exit(0);
    }

    // Free the getline-allocated buffer
    free(line);
}

int main(int argc, char *argv[])
{

    // Determine the mode of operation based on argc
    if (argc > 2)
    {
        // Error: Too many arguments.
        handleError();
    }

    bool isInteractiveMode = (argc == 1);

    FILE *inputSource = NULL;
    if (isInteractiveMode)
    {
        // set input source to stdin.
        inputSource = stdin;
    }
    else
    {
        // open the batch file as input source.
        inputSource = fopen(argv[1], "r");
        if (!inputSource)
        {
            handleError();
        }
    }

    initHistory();

    // Main loop
    while (true)
    {
        if (isInteractiveMode)
        {
            // Display prompt only in interactive mode.
            displayPrompt();
        }

        // Parse the input line to identify the command and its arguments.
        processInput(inputSource);
    }

    // Close the batch file if in batch mode.
    if (!isInteractiveMode)
    {
        fclose(inputSource);
    }

    return 0;
}
