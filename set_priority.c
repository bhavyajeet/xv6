#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int 
main (int argc , char * argv[])
{
    int pid = atoi(argv[1]);
    int priority = atoi (argv[2]);

    printf(1,"pid=%d priority=%d  \n",pid,priority);
    set_priority(pid,priority);
    exit();
}
