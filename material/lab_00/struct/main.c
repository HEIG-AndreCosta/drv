#include <stdio.h>

struct a
{
    int b;
    char c;
};
struct
{
    int b;
    char c;
} a;
int main(int argc, char **argv)
{
    a.b = 1;
    a.c = 'h';

    struct a aa = {2, 'e'};

    printf("%d %c\n", a.b, a.c);
    printf("%d %c\n", aa.b, aa.c);
}
