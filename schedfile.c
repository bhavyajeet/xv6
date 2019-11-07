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
#include "new.h"

int main (int argc,char * argv [] )
{
    volatile int j=561;
    printf(1,"beginning process \n");
    int procnu=10;


    for (int i=0;i<procnu;i++)
    {
        int pid ;
//        printf(1,"\n%d\n",(i*31)%13);
        pid = fork();
        if (pid==0)
        {
//            struct proc_stat * arg;
#ifdef PBS
            if (getpid()%3==0)
            {
                set_priority(getpid(),getpid()+60);
//                printf(1,"changed process %d\n",getpid());
            }
            else 
            {
                set_priority(getpid(),99);
            }
#endif
            sleep(100);
            for (volatile int i=0;i<2000000000;i++)
            {
                j+=(j%137)*i;
                if (i%1000==0)
                {
//                    getpinfo(arg);
//                    printf(1,"",arg->current_queue);

                }
            }
            printf(1,"\ncompleted process %d\n",getpid());
            exit();
        }

    }
#ifdef debug
    printf(1,"\nDONE DOEN DONE!\n\n");
#endif
    for (int i=0;i<procnu;i++)
    {
        wait();
    }
#ifdef debug
    printf(1,"done \n");
#endif
    exit();
}
