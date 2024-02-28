#include <stdio.h>


int main(int argc, char** argv)
{
    int value = 0xBEEFCAFE;

    char *ptr = (char*) &value;
    printf("Reading %ld bytes from %p to %p\n", sizeof(value), ptr, ptr + 4);
    for(int i = 0; i < sizeof(value); ++i)
    {
        printf("%p: %x\n", ptr, (*ptr) & 0xFF);
        ptr++;
    }
}
