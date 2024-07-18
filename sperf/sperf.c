#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct stat_t {
    char *name;
    float time;
    struct stat_t *next;
} stat_t;

stat_t *root = NULL;

stat_t* stat_new(const char *name, float time) {
    stat_t *t = malloc(sizeof(stat_t));
    t->name = strdup(name);
    t->time = time;
    t->next = NULL;
    return t;
}

void stat_add(const char *name, float time) {
    if (!root) {
        root = stat_new(name, time);
        return;
    }
    if (strcmp(root->name, name) == 0) {
        root->time += time;
        return;
    }
    stat_t *current = root;
    stat_t *prev = NULL;
    for (; current; prev = current, current = current->next) {
        if (strcmp(current->name, name) == 0) {
            prev->next = current->next;
            current->time += time;
            break;
        }
    }
    if (!current)
        current = stat_new(name, time);

    if (root->time < current->time) {
        current->next = root;
        root = current;
    } else {
        for (prev = root; prev->next && prev->next->time > current->time; prev = prev->next)
            ;
        current->next = prev->next;
        prev->next = current;
    }
}

void printstat(int n) {
    float sum = 0, i = 0;
    for (stat_t *t = root; t && i < n; t = t->next, i++) {
        sum += t->time;
    }

    i = 0;
    for (stat_t *t = root; t && i < n; t = t->next, i++) {
        printf("%s (%d%%)\n", t->name, (int)(t->time / sum * 100));
    }
}

#define LEN(x) (sizeof(x) / sizeof(x[0]))
int exec_strace(int argc, char *__argv[], char *pipename) {
    extern char **environ;
    char name[255];
    char output[255];
    sprintf(output, "-o%s", pipename);

    char *_strace_args[] = { "strace", "-qqq", "-s1", "-T", "-Xraw", NULL };
    char* _argv[LEN(_strace_args) + argc];

    char **strace_args = _strace_args;
    char **argv = _argv, **t = _argv;

    while(*strace_args)
        *t++ = *strace_args++;
    *t++ = output;
    while(*__argv)
        *t++ = *__argv++;
    *t = NULL;

    char *path = getenv("PATH");
    if (!path) {
        return execve(argv[0], argv, environ);
    }
    for(char *p1 = path, *p2 = path; p2 < path + strlen(path) + 1; p2++) {
        if (*p2 == ':') {
            *stpncpy(name, p1, p2 - p1) = '\0';
            p1 = p2 + 1;
            strcat(name, "/strace");
            execve(name, argv, environ);
        }
        if (*p2 == '\0') {
            strcpy(name, p1);
            strcat(name, "/strace");
            execve(name, argv, environ);
        }
    }

    return -1;
}

int main(int argc, char *argv[]) {
    if (argc == 1) {
        printf("sperf COMMAND [ARG]\n");
        return 0;
    }
    char fifo_name[] = "/tmp/sperf_fifo";

    // Create a named pipe
    if (mkfifo(fifo_name, 0666) == -1) {
        perror("mkfifo");
        return EXIT_FAILURE;
    }

    switch (fork()) {
    case -1:
        perror("fork");
        return EXIT_FAILURE;
    case 0:
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull < 0) {
            perror("open");
            // do something
        } else {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
        }
        if (exec_strace(argc, argv + 1, fifo_name) < 0) {
            return EXIT_FAILURE;
        }
    }

    FILE *fp = fopen(fifo_name, "r");
    if (!fp) {
        perror("fopen");
        return EXIT_FAILURE;
    }

    char line[4096];
    char name[32];
    float time;

    struct timeval prev, tv, start;
    long duration;
    gettimeofday(&start, NULL);
    prev = start;

    while(1) {
        gettimeofday(&tv, NULL);
        duration = (tv.tv_sec - prev.tv_sec) * 1000 + (tv.tv_usec - prev.tv_usec) / 1000;
        if (duration > 100) {
            prev = tv;
            duration = (tv.tv_sec - start.tv_sec) * 1000 + (tv.tv_usec - start.tv_usec) / 1000;
            printf("Time: %.1fs\n", (float)duration / 1000);
            printstat(5);
            printf("================\n");
        }

        if (!fgets(line, sizeof(line), fp)) {
            break;
        }

        char *name_end = strchr(line, '(');
        if (!name_end) {
            printf("sperf error: %s", line);
            return EXIT_FAILURE;
        }
        *stpncpy(name, line, name_end - line) = '\0';
        char *time_start = strrchr(line, '<');
        if (!time_start)
            time = 0;
        else
            sscanf(time_start, "<%f>", &time);

        // printf("%s %f\n", name, time);
        stat_add(name, time);
    }

    duration = (tv.tv_sec - start.tv_sec) * 1000 + (tv.tv_usec - start.tv_usec) / 1000;
    printf("Time: %.1fs\n", (float)duration / 1000);
    printstat(5);
    printf("================\n");

    // for (stat_t *t = root; t; t = t->next) {
    //     printf("%s %f\n", t->name, t->time);
    // }

    fclose(fp);
    unlink(fifo_name);
    return 0;
}
