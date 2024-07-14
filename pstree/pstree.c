#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef struct process {
    bool istid;
    pid_t pid, ppid;
    char* comm;
    struct process* child_list;
    struct process* next_child;
    struct process* next_process;
} process;

void list_insert(process* list, process* p)
{
    for (; list->next_child; list = list->next_child)
        ;
    list->next_child = p;
}

void insert_child(process* root, process* p)
{
    for (process* tmp = root; tmp; tmp = tmp->next_process) {
        if (tmp->pid == p->ppid) {
            if (tmp->child_list == NULL)
                tmp->child_list = p;
            else
                list_insert(tmp->child_list, p);
            return;
        }
    }
}

bool flag_p = false;
bool flag_n = false;
void print(process* p, int tabs)
{
    for (int i = 0; i < tabs; i++)
        printf("\t");
    if (p->istid)
        printf("{%s}", p->comm);
    else
        printf("%s", p->comm);
    if (flag_p)
        printf("(%d)", p->pid);
    printf("\n");

    for (process* tmp = p->child_list; tmp; tmp = tmp->next_child)
        print(tmp, tabs + 1);
}

int main(int argc, char* argv[])
{
    const struct option table[] = {
        {"show-pids", no_argument, NULL, 'p'},
        {"numeric-sort", no_argument, NULL, 'n'},
        {"version", no_argument, NULL, 'V'},
        {0, 0, NULL, 0},
    };
    int o;
    while ((o = getopt_long(argc, argv, "pnV", table, NULL)) != -1) {
        switch (o) {
        case 'p':
            flag_p = true;
            break;
        case 'n':
            flag_n = true;
            break;
        case 'V':
            printf("pstree 1.0\n");
            return EXIT_SUCCESS;
        }
    }

    DIR* procs = opendir("/proc");
    char buf[8192]; // is 8192 a magic number?
    process* root = NULL;
    process *p, *prev = NULL;

    for (struct dirent* proc = readdir(procs); proc; proc = readdir(procs)) {
        if (!isdigit(proc->d_name[0]))
            continue;
        sprintf(buf, "/proc/%s/stat", proc->d_name);
        FILE* fp = fopen(buf, "r");
        if (!fp)
            continue;
        int n = fread(buf, 1, sizeof(buf), fp);
        fclose(fp);

        p = malloc(sizeof(process)); // malloc may fails
        memset(p, 0, sizeof(process));
        if (root == NULL)
            root = p;
        if (prev != NULL)
            prev->next_process = p;
        prev = p;

        sscanf(buf, "%d", &p->pid);
        char *s = buf, *e = s;
        while (*s++ != '(')
            ;
        for (char* c = s; c < buf + n; c++) {
            if (*c == ')')
                e = c;
        }
        p->comm = strndup(s, e - s);
        sscanf(e, ") %*c %d", &p->ppid);

        sprintf(buf, "/proc/%s/task", proc->d_name);
        DIR* tasks = opendir(buf);
        for (struct dirent* task = readdir(tasks); task; task = readdir(tasks)) {
            if (!isdigit(task->d_name[0]) || strcmp(proc->d_name, task->d_name) == 0)
                continue;

            p = malloc(sizeof(process));
            memset(p, 0, sizeof(process));
            prev->next_process = p;
            p->istid = true;
            sscanf(task->d_name, "%d", &p->pid);
            sscanf(proc->d_name, "%d", &p->ppid);
            p->comm = prev->comm;
            prev = p;
        }
        closedir(tasks);
    }
    closedir(procs);

    for (process* tmp = root->next_process; tmp; tmp = tmp->next_process)
        insert_child(root, tmp);

    print(root, 0);

    return EXIT_SUCCESS;
}
