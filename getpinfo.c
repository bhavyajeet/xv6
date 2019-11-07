#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "new.h"

int main(int argc, char * argv[])
{
    struct proc_stat  argument;
    int pid=atoi(argv[1]);
    int k= getpinfo(pid,&argument);
    if (k==87 )
    {
        printf(1,"\n\n\n\n\n\n\n");
    }
    printf(1,"pid:%d Q:%d numrun:%d runtime%d\n",argument.pid,argument.current_queue,argument.num_run,argument.runtime);
    printf(1,"each Q:%d %d %d %d %d\n\n",argument.ticks[0],argument.ticks[1],argument.ticks[2],argument.ticks[3],argument.ticks[4]);
    printf(1,"%d\n");
    exit();

}
