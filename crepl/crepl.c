#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define WRAPPER_NAME_FMT "_expr_wrapper_%d"
#define WRAPPER_FMT "int _expr_wrapper_%d() { return %s; }"

int build_shared_lib(const char *filename, const char *libname) {
    pid_t pid = fork();
    if(pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0) {
        int dev_null = open("/dev/null", O_WRONLY);
        if (dev_null < 0) {
            perror("open");
        } else {
            dup2(dev_null, STDOUT_FILENO);
            dup2(dev_null, STDERR_FILENO);
            close(dev_null);
        }
#if __x86_64__
        execlp("gcc", "gcc", "-fPIC", "-shared", "-xc", "-Wno-implicit-function-declaration", filename, "-o", libname, NULL);
#else
        execlp("gcc", "gcc", "-fPIC", "-shared", "-m32", "-xc", "-Wno-implicit-function-declaration", filename, "-o", libname, NULL);
#endif
    }

    int status;
    wait(&status);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    static char line[4096];

    int n = 0;
    char filename[32];
    char libname[36];
    char wrappername[32];
    char buf[4160];
    char *error;
    int is_expr = 1;
    void *handle;
    int (*wrapper)();

    while (1) {
        printf("crepl> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }

        strcpy(filename, "tmp/crepl_XXXXXX");
        int fd = mkstemp(filename);
        if (strncmp(line, "int", 3) == 0) {
            is_expr = 0;
            write(fd, line, strlen(line));
        }
        else {
            is_expr = 1;
            sprintf(wrappername, WRAPPER_NAME_FMT, n);
            sprintf(buf, WRAPPER_FMT, n, line);
            write(fd, buf, strlen(buf));
        }
        close(fd);

        sprintf(libname, "%s.so", filename);
        if (build_shared_lib(filename, libname) < 0) {
            printf("compile error!\n");
            continue;
        }

        handle = dlopen(libname, RTLD_LAZY | RTLD_GLOBAL);
        if (!handle) {
            continue;
        }
        dlerror();

        if (is_expr) {
             wrapper = dlsym(handle, wrappername);
            if ((error = dlerror()) != NULL)  {
                dlclose(handle);
                continue;
            }
            printf("%d\n", wrapper());
        }
        else {
            printf("OK.\n");
        }
    }
}
