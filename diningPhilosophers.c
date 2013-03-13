/*************************************************************************/
/*  diningPhiosphers.c                                                    */
/*                                                                       */
/*************************************************************************/


/* includes */
#include "vxWorks.h"
#include "stdio.h"
#include "stdlib.h"
#include "semLib.h"
#include "taskLib.h"

/* defines */
#define THINK_TIME   50     // ticks
#define EAT_TIME     10     // ticks
#define MAX_WAIT     100    // ticks
#define MAX_SIM      100    // seconds
#define STACK_SIZE   20000
#define MIN_PHILOS   3
#define MAX_PHILOS   20

/* task IDs */
int tidPhilosopher[MAX_PHILOS];

/* Semaphore IDs */
SEM_ID sidFork[MAX_PHILOS];	
SEM_ID waiter;	

/* function declarations */
void philosopher(int id, int max_philo, int delayTicks, int *eat_cnt);


/*************************************************************************/
/*  main task                                                            */
/*                                                                       */
/*************************************************************************/

int main (void) {
    int philo_cnt = 0;
    int wait_time = 0;
    int nseconds = 0;
    int eat_cnt[MAX_PHILOS];
    int i;
    
    for(i=0; i<MAX_PHILOS; i++) {
        eat_cnt[i] = 0;
    }
    
    /* get number of philosophers */ 
    while ((philo_cnt < MIN_PHILOS) || (philo_cnt > MAX_PHILOS)) {
        printf("Enter number of philosophers [%d-%d]: ", MIN_PHILOS, MAX_PHILOS);
        scanf("%d", &philo_cnt);
    };
    /* get the waiting time to grab the second fork */ 
    while ((wait_time < 1) || (wait_time > MAX_WAIT)) {
        printf("Enter waiting time [1-%d ticks]: ", MAX_WAIT);
        scanf("%d", &wait_time);
    };
    /* get the simulation time */ 
    while ((nseconds < 1) || (nseconds > MAX_SIM)) {
        printf("Enter overall simulation time [1-%d s]: ", MAX_SIM);
        scanf("%d", &nseconds);
    };

    printf("\nSimulating %d philosophers with waiting time %d ticks for %d seconds ...\n\n",
            philo_cnt, wait_time, nseconds);
    
    /* create binary semaphores */
    for (i=0; i<philo_cnt; i++) {
        sidFork[i] = semBCreate(SEM_Q_FIFO, SEM_FULL);
    }
    waiter = semCCreate(SEM_Q_FIFO, philo_cnt-1);

    /* spawn (create and start) tasks */
    for (i=0; i<philo_cnt; i++) {
        tidPhilosopher[i] = taskSpawn("tPhilosopher_" + (char)i, 200, 0, STACK_SIZE,
                (FUNCPTR)philosopher, i, philo_cnt, wait_time, (int)eat_cnt, 0, 0, 0, 0, 0, 0);
    }

    /* run for the given simulation time */
    taskDelay(nseconds*60);
    
    /* delete tasks and semaphores */
    for (i=0; i<philo_cnt; i++) {
        taskDelete(tidPhilosopher[i]);
    }
    for (i=0; i<philo_cnt; i++) {
        semDelete(sidFork[i]);
    }

    printf("\n\nAll philosophers stopped.\n");	
    printf("Eat counters:");	
    for(i=0; i<philo_cnt; i++) {
        printf(" %d", eat_cnt[i]);
    }
    printf("\n\n");	
    return(0);
}   


/*************************************************************************/
/*  task "tPhilosopher[i]"                                               */
/*                                                                       */
/*************************************************************************/

void philosopher(int id, int max_philo, int delayTicks, int *eat_cnt) {
    while (1) {
        int left, right;
        left = id;
        right = (id == 0) ? max_philo - 1 : id - 1;
        printf("Philosopher %d - start thinking.\n", id);
        taskDelay(THINK_TIME);
        // take the fork on the left
        semTake(waiter, WAIT_FOREVER);
        semTake(sidFork[left], WAIT_FOREVER);
        taskDelay(delayTicks);
        // take the fork on the right
        semTake(sidFork[right], WAIT_FOREVER);
        printf("Philosopher %d - start eating.\n", id);
        taskDelay(EAT_TIME);
        eat_cnt[id]++;
        semGive(sidFork[left]);
        semGive(sidFork[right]);
        semGive(waiter);
    };
}
