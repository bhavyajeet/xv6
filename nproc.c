#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "new.h"
//#define FCFS
//#define RR


struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;


struct proc * mlfarr [NPROC][5];

shi


static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  
  acquire(&tickslock);
  //ticks++;
  p->creattime= ticks;
  release(&tickslock);
  p->priority =60;


  p->runtime = 0;
  p->iowaittime=0;
  p->endtime = 0;

  p->timespent[0]=0;
  p->timespent[1]=0;
  p->timespent[2]=0;
  p->timespent[3]=0;
  p->timespent[4]=0;

#ifdef MLFQ

  p->priority =0;
  p->qentime[0]=ticks;
  cprintf("BEG :%d %d\n",p->pid,p->qentime[0]);

#endif 

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;

  curproc->endtime=ticks;


  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.


void
scheduler(void)
{
//  cprintf("%s",SCHEDULER);
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

#ifdef MLFQ
  int limit[5]={1,2,4,8,16};
#endif

  
  for(;;){
    // Enable interrupts on this processor.
    sti();

   

#ifdef RR
    
//    cprintf("RR\n");
  
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      cprintf("RR : running pid %d on cpu %d\n",p->pid,c->apicid);
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }

#else 

#ifdef FCFS
    int found=0;
    struct proc * toexec=0;
    int minsttime=100000000;
    int minpid=100000000;
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
      else 
      {
          if (p->creattime < minsttime)
          {
              found++;
              toexec = p;
              minsttime=p->creattime;
              minpid = p->pid;
          }
          else if (p->creattime == minsttime)
          {
              if (p->pid < minpid)
              {
                  toexec=p;
                  minpid=toexec->pid;
              }
          }
      }
    }

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      if (found)
      {
     cprintf("FCFS: running pid %d on cpu %d starttime %d \n",
             toexec->pid,c->apicid,toexec->creattime);
      c->proc = toexec;
      switchuvm(toexec);
      toexec->state = RUNNING;
  
      swtch(&(c->scheduler), toexec->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
      }
#endif

#ifdef PBS


//    cprintf("FCFS\n");
      
    int found=0;
    struct proc * toexec=0;
    int minpriority=100000000;
    int minexec=100000000;
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
      else 
      {
          if (p->priority < minpriority)
          {
              found++;
              toexec = p;
              minpriority=p->priority;
              minexec = p->numexec;
          }
          else if (p->priority == minpriority)
          {
              if (p->numexec < minexec)
              {
                  toexec=p;
                  minexec=toexec->numexec;
              }
          }
      }
    }

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      if (found)
      {
      toexec->numexec ++;
//     cprintf("PBS: running pid %d on cpu %d starttime %d priority %d\n",
//             toexec->pid,c->apicid,toexec->creattime,toexec->priority);
      c->proc = toexec;
      switchuvm(toexec);
      toexec->state = RUNNING;

      swtch(&(c->scheduler), toexec->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
      }


#else 
#ifdef MLFQ

    int found=0;
    struct proc * toexec=0;
    int lastexecpid=0;
    int minpriority=100000000;
    int minqentime=100000000;
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
//        cprintf("iter %d\n",c->apicid);
      if(p->state != RUNNABLE)
      {
//        cprintf("iodfsagfter\n");
        continue;
      }
      else 
      {
//          cprintf("\n\nPROCESS %d RUNNABLE \n\n",p->pid);
          if (p->priority < minpriority)
          {
//              cprintf("found\n");
              found++;
              toexec = p;
              minpriority=p->priority;
              minqentime = p->qentime[p->priority];
          }
          else if (p->priority == minpriority)
          {
              if (p->qentime[minpriority] < minqentime)
              {
//                  cprintf("found again\n");
                  toexec=p;
                  minqentime=p->qentime[minpriority] ;
              }
          }
      }
    }

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      
      


      
      if (found)
      {
      if (lastexecpid==toexec->pid)
      {
          //same executed again
      }
      else 
      {
          lastexecpid=toexec->pid;
      }
      toexec->timespent[toexec->priority]++;
      toexec->ticks[toexec->priority]++;
      toexec->numexec ++;
//     cprintf("MLFQ: running pid %d on cpu %d qentime %d queue %d timespent %d\n",toexec->pid,c->apicid,toexec->qentime[toexec->priority],toexec->priority,toexec->timespent[toexec->priority]);
      c->proc = toexec;
      switchuvm(toexec);
      toexec->state = RUNNING;

      swtch(&(c->scheduler), toexec->context);
      switchkvm();

//      cprintf("came back\n");
      if (found )
      {
      if (toexec->state==SLEEPING  )
      {
          toexec->qentime[toexec->priority]=ticks;
          toexec->timespent[toexec->priority]=0;
      }
      
      if (toexec->timespent[toexec->priority]==limit[toexec->priority]  )
      {
          if (toexec->priority!=4)
          {
              toexec->priority ++;
              toexec->qentime[toexec->priority]=ticks;
              toexec->timespent[toexec->priority]=0;
          }
          else 
          {
              toexec->qentime[toexec->priority]=ticks;
              toexec->timespent[toexec->priority]=0;

          }
      }

      
      // Process is done running for now.
      // It should have changed its p->state before coming back.
      }  
      c->proc = 0;
      }
/*
    acquire(&ptable.lock);
    
    struct proc * torun ;
    int foundabove=0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
      if (p->priority == 0 )
      {
          foundabove++;
          torun =p;
      }
     
    }
    if (foundabove == 0 ){
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
            if(p->state != RUNNABLE)
                continue;
            if (p->priority == 1 )
            {
                foundabove++;
                torun =p;
                break;
            }
        }
    }
    if (foundabove == 0 ){
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
            if(p->state != RUNNABLE)
                continue;
            if (p->priority == 2 )
            {
                foundabove++;
                torun =p;
                break;
            }
        }
    }
    if (foundabove == 0 ){
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
            if(p->state != RUNNABLE)
                continue;
            if (p->priority == 3 )
            {
                foundabove++;
                torun =p;
                break;
            }
        }
    }
    if (foundabove == 0 ){
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
            if(p->state != RUNNABLE)
                continue;
            if (p->priority == 4 )
            {
                foundabove++;
                torun =p;
                break;
            }
        }
    }
      //look for 0 runnable 
      // look for 1 runnable if 0 not found 
      // look for 2 runnable if 0,1 not found 
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
       
      
      cprintf("milfq : running pid %d on cpu %d from Q %d\n",torun->pid,c->apicid,torun->priority);
      c->proc = torun;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
  */  



#endif

#endif

#endif 
  


    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}


// i added 


void
changeruntime(void)
{
    struct proc *p;
    acquire (&ptable.lock);
    for (p=ptable.proc;p<&ptable.proc[NPROC];p++)
    {
        if (p->state == RUNNING)
        {
            p->runtime++;
        }
    }
    release(&ptable.lock);
}


int 
noob()
{
    cprintf("trynna learn \n");
    return 1;
}


int
waitx(int * waittime , int * runtime )
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        *waittime = p->endtime-p->creattime-p->runtime;
        *runtime = p->runtime;


        cprintf("endtime =%d starttime=%d runtime=%d iotime=%d \n",
                p->endtime,p->creattime,p->runtime,p->iowaittime );


        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

int 
set_priority (int procid , int priority )
{
    struct proc * pr;
    int found=0;
    int toret =0;

    acquire(&ptable.lock);
    for (pr = ptable.proc;pr<&ptable.proc[NPROC];pr++)
    {
        if (pr->pid != procid)
            continue; 
        else if (pr->pid==procid)
        {
            toret=pr->priority ;
            pr->priority = priority;
            found++;
            cprintf("found pid %d and changed to pr %d\n",procid,pr->priority);
            break;
        }

    }
    release(&ptable.lock);
//    cprintf("here\n");
    if (found==0)
        return -1 ;
    else 
    {
//        cprintf("lol\n");
        return toret ; 
    }
}
int 
ps (void)
{
    struct proc *p ;
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
        if (p->state==SLEEPING)
            cprintf("%s \t %d \t SLEEPING \t%d\n ",p->name,p->pid,p->priority);
        else if (p->state==RUNNING)
            cprintf("%s \t %d \t RUNNING \t %d\n ",p->name,p->pid,p->priority);
        else if (p->state==RUNNABLE)
            cprintf("%s\t %d \t RUNNABLE \t %d\n ",p->name,p->pid,p->priority);
        else if (p->state==ZOMBIE)
            cprintf("%s \t %d \t ZOMBIE\t %d\n ",p->name,p->pid,p->priority);
    }
    release(&ptable.lock);
    return 0;
}



int 
getpinfo (struct proc_stat * argument )
{
    struct proc *p ;
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
        if (p->state==SLEEPING || p->state == RUNNING || p->state == RUNNABLE ||p->state ==ZOMBIE)
        {
            argument->pid=p->pid;
            //1 tick approx = 10 ms 
            argument->runtime=p->runtime/100;


            ///TODO NUMRUN;
#ifdef MLFQ
            argument->ticks[0]=p->ticks[0];
            argument->ticks[1]=p->ticks[1];
            argument->ticks[2]=p->ticks[2];
            argument->ticks[3]=p->ticks[3];
            argument->ticks[4]=p->ticks[4];
            argument->current_queue=p->priority;
#else
            argument->ticks[0]=-1;
            argument->ticks[1]=-1;
            argument->ticks[2]=-1;
            argument->ticks[3]=-1;
            argument->ticks[4]=-1;
            argument->current_queue=-1;
#endif
        }
    }
    release(&ptable.lock);
    return 0;
}



