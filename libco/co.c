#include "co.h"
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define STACK_SIZE 4 * 1024
#define CO_SIZE (sizeof(struct co))

enum co_status {
    CO_NEW = 1, // 新创建，还未执行过
    CO_RUNNING, // 已经执行过
    CO_WAITING, // 在 co_wait 上等待
    CO_DEAD,    // 已经结束，但还未释放资源
};

struct co {
    struct co* next;

    char* name;
    void (*func)(void*); // co_start 指定的入口地址和参数
    void* arg;

    enum co_status status;     // 协程的状态
    struct co* waiter;         // 是否有其他协程在等待当前协程
    jmp_buf context;           // 寄存器现场
    uint8_t stack[STACK_SIZE]; // 协程的堆栈
};

static int nr_co;
static struct co* current;
static struct co* co_list;
static struct co scheduler;

static inline void co_list_add(struct co* co)
{
    struct co* c = co_list;
    for (; c->next; c = c->next)
        ;
    c->next = co;
    nr_co++;
}

static inline void co_list_del(struct co* co)
{
    for (struct co* c = co_list; c->next; c = c->next) {
        if (c->next == co) {
            c->next = co->next;
            free(co->name);
            free(co);
            nr_co--;
            return;
        }
    }
    printf("delete a coroutine does not exist!\n");
    assert(0);
}

static inline void stack_switch_call(void* sp, void* entry, uintptr_t arg)
{
    asm volatile(
#if __x86_64__
        "movq %0, %%rsp; movq %2, %%rdi; jmp *%1"
        :
        : "b"((uintptr_t)sp), "d"(entry), "a"(arg)
        : "memory"
#else
        "movl %0, %%esp; movl %2, 4(%0); jmp *%1"
        :
        : "b"((uintptr_t)sp - 8), "d"(entry), "a"(arg)
        : "memory"
#endif
    );
}

static inline void co_wrapper()
{
    current->func(current->arg);
    current->status = CO_DEAD;
    if (current->waiter) {
        nr_co++;
        current->waiter->status = CO_RUNNING;
    }
    longjmp(scheduler.context, 1);
}

static inline struct co* co_schedule()
{
    struct co* co = current;
    do {
        co = co->next ? co->next : co_list;
    } while (co->status != CO_RUNNING && co->status != CO_NEW);

    // struct co* co = co_list;
    // do {
    //     co = co->next;
    // } while (co->status != CO_RUNNING && co->status != CO_NEW);
    // int n = rand() % nr_co;
    // while(n) {
    //     if (co->status == CO_NEW || co->status == CO_RUNNING)
    //         n--;
    //     co = co->next;
    // }

    return co;
}

static inline void co_scheduler()
{
    switch (setjmp(scheduler.context)) {
    case 1:
        current = co_schedule();
        if (current->status == CO_NEW) {
            current->status = CO_RUNNING;
            stack_switch_call(&current->stack + 1, co_wrapper, 0);
        } else if (current->status == CO_RUNNING) {
            longjmp(current->context, 1);
        } else {
            printf("should not reach here\n");
            assert(0);
        }
        break;
    }
}

struct co* co_start(const char* name, void (*func)(void*), void* arg)
{
    struct co* co = malloc(CO_SIZE);
    memset(co, 0, CO_SIZE);
    co->name = strdup(name);
    co->func = func;
    co->arg = arg;
    co->status = CO_NEW;

    co_list_add(co);
    return co;
}

void co_yield ()
{
    switch (setjmp(current->context)) {
    case 0:
        current->status = CO_RUNNING;
        longjmp(scheduler.context, 1);
        break;
    }
}

void co_wait(struct co* co)
{
    if (co->status != CO_DEAD) {
        co->waiter = current;
        current->status = CO_WAITING;
        nr_co--;
        switch (setjmp(current->context)) {
        case 0:
            longjmp(scheduler.context, 1);
            break;
        }
    }
    co_list_del(co);
}

__attribute__((constructor))
void co_init()
{
    struct co* main = malloc(CO_SIZE);
    memset(main, 0, CO_SIZE);
    main->name = strdup("main");
    main->status = CO_RUNNING;
    main->next = NULL;
    current = co_list = main;

    co_scheduler();
}
