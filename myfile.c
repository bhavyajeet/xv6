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

int main (int argc,char * argv [] )
{
    long long int lol=3;
    printf(1,"beginning process \n");
    for (int i=0;i<10;i++)
    {
        for (long long int j=0;j<800000000;j+=1)
        {
            lol=5700%31*lol; 
        }
        printf(1,"iter %d %d \n",i,lol);
    }
    printf(1,"done \n");
    exit();
}
