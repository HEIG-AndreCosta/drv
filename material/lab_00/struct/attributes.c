#include <stdio.h>
#include <stddef.h>

#define container_of(ptr, type, member) \
    (type *)(void *)((char *)ptr - offsetof(type, member))

typedef struct
{
    int a;
    char c;
    int b;
} __attribute__((__packed__))
s_t;
int main(int argc, char **argv)
{
    s_t s = {.a = 1, .b = 2, .c = 'a'};
    int *p = &s.b;
    s_t *ps = container_of(p, s_t, b);
    printf("%d %d %c\n", ps->a, ps->b, ps->c);
}