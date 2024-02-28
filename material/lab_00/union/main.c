#include <stdio.h>

typedef union
{
    int a;
    char c[sizeof(int)];
} u_t;

int main(int argc, char **argv)
{
    u_t u;
    u.a = 0xBEEFCAFE;
    for (int i = 0; i < sizeof(u.a); ++i)
    {
        printf("%x", u.c[i] & 0xff);
    }
    printf("\n");
    return 0;
}