#include "my_threads.h"
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ucontext.h>
#include <unistd.h>

#define FIBER_STACK 1024 * 64 // 64 KB stack

jmp_buf child, parent;

/* The child thread will execute this function. */
void thread_func(void (*func)(void*))
{
    func(NULL); // run the function passed in

    // going back to the parent once everything is done
    if (setjmp(child))
        longjmp(parent, 1);

    longjmp(parent, 1);
}

void create_thread(void (*func)(void*))
{
    stack_t stack;

    // Create the new stack
    stack.ss_flags = 0;
    stack.ss_size = FIBER_STACK;

    if ((stack.ss_sp = malloc(FIBER_STACK)) == 0) {
        perror("Error: malloc could not allocate stack.\n");
        exit(EXIT_FAILURE);
    }

    sigaltstack(&stack, 0);

    if (setjmp(child))
        thread_func(func);

    // Execute the child context
    if (setjmp(parent)) {
        if (setjmp(parent) == 0)
            longjmp(child, 1);
    } else
        longjmp(child, 1);

    free(stack.ss_sp); // free the stack
}