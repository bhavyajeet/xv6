#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
//#include "proc.h"

int main (int argc,char * argv [] )
{
    int pid ;
    int status=-2;
    int runtime=0 ;
    int waitime =0;
    pid = fork();
    if (pid <0 )
    {
        printf(2,"forking error \n");
    }
    else if (pid ==0)
    {
        exec(argv[1],argv+1);
    }
    else 
    {
//        status = wait();
        status = waitx(&waitime,&runtime);
        printf(1,"wait time = %d and runtime = %d \n",waitime,runtime);
    }
    printf(1,"status %d \n",status);
    exit();
}
