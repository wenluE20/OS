#include "kernel/types.h"
#include "user.h"

int main(int argc,char* argv[])
{
    if(argc != 2)
    {
        printf("Sleep needs one argument!\n");//check the number of parameters
        exit(-1); 
    }
    int ticks = atoi(argv[1]);//change the string into the int
    sleep(ticks);//use the system sleep
    printf("(nothing happens for a little while)\n");
    exit(0);//make sure it exit
    
}
