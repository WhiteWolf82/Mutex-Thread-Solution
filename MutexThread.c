#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<pthread.h>
#include<unistd.h>

#define MAXNUM 100      //assume maximum number of cars is 100

//mutex locks of four crossings
pthread_mutex_t mutex_a;
pthread_mutex_t mutex_b;
pthread_mutex_t mutex_c;
pthread_mutex_t mutex_d;
//conditions related to the above mutex locks
//when deadlock happens, only one car will be signaled by deadlock-detecting thread
//this car will signal cars in other directions
pthread_cond_t cond_wait_a;
pthread_cond_t cond_wait_b;
pthread_cond_t cond_wait_c;
pthread_cond_t cond_wait_d;

//mutex locks of staying behind a crossing
//only one car is allowed to wait behind the crossing in each direction
pthread_mutex_t mutex_behind_a;
pthread_mutex_t mutex_behind_b;
pthread_mutex_t mutex_behind_c;
pthread_mutex_t mutex_behind_d;
//conditions of next passing direction, related to the above mutex locks
pthread_cond_t firstNorth;
pthread_cond_t firstEast;
pthread_cond_t firstSouth;
pthread_cond_t firstWest;

//mutex locks of staying in a crossing
pthread_mutex_t mutex_in_a;   //staying in a, want b
pthread_mutex_t mutex_in_b;   //staying in b, want c
pthread_mutex_t mutex_in_c;   //staying in c, want d
pthread_mutex_t mutex_in_d;   //staying in d, want a
//conditions of whether can move on at the first crossing
//each car needs to pass two crossings, after passing one, it has to wait for signal
pthread_cond_t cond_in_a;    //waiting in a for passing b
pthread_cond_t cond_in_b;    //waiting in b for passing c
pthread_cond_t cond_in_c;    //waiting in c for passing d
pthread_cond_t cond_in_d;    //waiting in d for passing a

//the number of empty crossings and its mutex lock
volatile int emptyCrossNum = 4;
pthread_mutex_t mutex_emptyNum;

//condition and mutex lock of the deadlock-detecting thread
//it will only be woken up when emptyCrossNum = 0
pthread_cond_t deadlockCond;
pthread_mutex_t deadLockMutex;

//the car queues
struct queue
{
    int queueThreadId[MAXNUM];  //thread id array
    int front;
    int rear;
    int threadNum;  //number of threads in this queue
    int curThread;  //current thread number in the front of the queue
};
struct queue queueNorth = 
{
    .front = 0,
    .rear = 0,
    .threadNum = 0,
    .curThread = -1
};
struct queue queueEast =
{
    .front = 0,
    .rear = 0,
    .threadNum = 0,
    .curThread = -1
};
struct queue queueSouth =
{
    .front = 0,
    .rear = 0,
    .threadNum = 0,
    .curThread = -1
};
struct queue queueWest =
{
    .front = 0,
    .rear = 0,
    .threadNum = 0,
    .curThread = -1
};

//queue operations
int pop(struct queue *q)
{
    if (q->threadNum > 0)
    {
        q->threadNum--;
        return q->queueThreadId[q->front++];
    }
}

void push(struct queue *q, int id)
{
    if (q->threadNum < MAXNUM)
    {
        q->queueThreadId[q->rear++] = id;
        q->threadNum++;
    }
}


pthread_t deadLockThreadId;     //identifier of the deadlock-detecting thread
pthread_t threads[MAXNUM * 4];  //all the car threads
int totalThreadNum = 0;     //total number of threads
volatile int isNorth, isEast, isSouth, isWest;  //whether there is a car waiting in each direction
volatile int isNorthWait, isEastWait, isSouthWait, isWestWait;  //whether the car in each direction has been waiting for another car
volatile int lastArrive;     //1 represents North, 2 represents East, 3 represents South, 4 represents West
volatile int isDeadlock;     //whether a deadlock happens


//different thread functions
void *northThread(void *param)
{
    //waiting until the car in the front of the queue be gone
    pthread_mutex_lock(&mutex_behind_c);
    pthread_cond_wait(&firstNorth, &mutex_behind_c);
    pthread_mutex_unlock(&mutex_behind_c);

    //now it's this car's turn
    int carId = queueNorth.curThread;
    isNorth = 1;     //now there is a car in the north
    isNorthWait = 0;
    lastArrive = 1;

    printf("car %d from North arrives at crossing.\n", carId);

    //it wants to pass c
    pthread_mutex_lock(&mutex_c);

    //decrease the number of empty crossings
    pthread_mutex_lock(&mutex_emptyNum);
    emptyCrossNum--;
    pthread_mutex_unlock(&mutex_emptyNum);

    if (emptyCrossNum == 0)     //a deadlock happens
    {
        pthread_cond_signal(&deadlockCond);     //wake up the deadlock-detecting thread

        //wait for being signaled
        //when waiting, mutex_c will be released, so the car on its left can go
        pthread_cond_wait(&cond_wait_c, &mutex_c);

        //woken up by the car on its right
        //now all the cars in the crossing have been gone
        pthread_mutex_lock(&mutex_d);

        isDeadlock = 0;     //deadlock has been solved

        pthread_mutex_unlock(&mutex_c);

        //since the car on its right woke up this car, this car needs to wake up a new car on the right queue
        usleep(2000);
        if (queueWest.threadNum > 0 && isWest == 0)
        {
            queueWest.curThread = pop(&queueWest);
            pthread_cond_signal(&firstWest);
        }
    }
    else
    {
        if (isWest == 1)    //if there is a car waiting on its right, let it go first
        {
            //wait in c for being signaled
            isNorthWait = 1;
            pthread_mutex_lock(&mutex_in_c);
            pthread_cond_wait(&cond_in_c, &mutex_in_c);

            //woken up by that car or the deadlock-detecting thread
            pthread_mutex_lock(&mutex_d);
            pthread_mutex_unlock(&mutex_in_c);
        }
        else    //if not, just move on and leave the crossing
        {
            pthread_mutex_lock(&mutex_d);
        }

        //it also has to wake up the car on the left that has been waiting
        if (isDeadlock == 1 && lastArrive == 2)    //if a deadlock happened and the car on its left arrived last
        {
            pthread_cond_signal(&cond_wait_b);
        }
        else if (isEast == 1 && isEastWait == 1)    //no deadlock or not the last arrived car
        {
            pthread_cond_signal(&cond_in_b);
        }

        pthread_mutex_unlock(&mutex_c);
    }

    //if there are more cars in the queue, they can move forward now
    isNorth = 0;

    //increase the number of empty crossings
    pthread_mutex_lock(&mutex_emptyNum);
    emptyCrossNum++;
    pthread_mutex_unlock(&mutex_emptyNum);  

    printf("car %d from North leaving crossing.\n", carId);

    usleep(2000);
    pthread_mutex_unlock(&mutex_d);

    //since the car on its right woke up this car, it needs to wake up a new car on the right queue
    if (isNorthWait == 1)    //more cars in the right queue
    {
        //wait until the deadlock has been solved
        while (isDeadlock == 1);
        //sleep for a while to wait till last west car be gone
        usleep(2000);
        if (queueWest.threadNum > 0 && isWest == 0)
        {
            queueWest.curThread = pop(&queueWest);
            pthread_cond_signal(&firstWest);
        }
    }
    //if there is no car on the right, and there are more cars in its own queue
    //it now has to wake up a new car in its own queue, otherwise that car will be waiting forever
    if (queueNorth.threadNum > 0)
    {
        //wait until the deadlock has been solved
        while (isDeadlock == 1);
        usleep(2000);
        if (isNorth == 0)
        {
            queueNorth.curThread = pop(&queueNorth);
            pthread_cond_signal(&firstNorth);
        }
    }

    return NULL;
}

void *eastThread(void *param)
{
    //waiting until the car in the front of the queue be gone
    pthread_mutex_lock(&mutex_behind_b);
    pthread_cond_wait(&firstEast, &mutex_behind_b);
    pthread_mutex_unlock(&mutex_behind_b);

    //now it's this car's turn
    int carId = queueEast.curThread;
    isEast = 1;     //now there is a car in the east
    isEastWait = 0;
    lastArrive = 2;

    printf("car %d from East arrives at crossing.\n", carId);

    //it wants to pass b
    pthread_mutex_lock(&mutex_b);

    //decrease the number of empty crossings
    pthread_mutex_lock(&mutex_emptyNum);
    emptyCrossNum--;
    pthread_mutex_unlock(&mutex_emptyNum);

    if (emptyCrossNum == 0)     //a deadlock happens
    {
        pthread_cond_signal(&deadlockCond);     //wake up the deadlock-detecting thread

        //wait for being signaled
        //when waiting, mutex_b will be released, so the car on its left can go
        pthread_cond_wait(&cond_wait_b, &mutex_b);

        //woken up by the car on its right
        //now all the cars in the crossing have been gone
        pthread_mutex_lock(&mutex_c);

        isDeadlock = 0;

        pthread_mutex_unlock(&mutex_b);

        usleep(2000);
        if (queueNorth.threadNum > 0 && isNorth == 0)
        {
            queueNorth.curThread = pop(&queueNorth);
            pthread_cond_signal(&firstNorth);
        }
    }
    else
    {
        if (isNorth == 1)    //if there is a car waiting on its right, let it go first
        {
            //wait in b for being signaled
            isEastWait = 1;
            pthread_mutex_lock(&mutex_in_b);
            pthread_cond_wait(&cond_in_b, &mutex_in_b);

            //woken up by that car or the deadlock-detecting thread
            pthread_mutex_lock(&mutex_c);
            pthread_mutex_unlock(&mutex_in_b);
        }
        else    //if not, just move on and leave the crossing
        {
            pthread_mutex_lock(&mutex_c);
        }

        //it now has to wake up the car on the left that has been waiting
        if (isDeadlock == 1 && lastArrive == 3)    //if a deadlock happened, and the car on its left arrived last
        {
            pthread_cond_signal(&cond_wait_a);
        }
        else if (isSouth == 1 && isSouthWait == 1)
        {
            pthread_cond_signal(&cond_in_a);
        }

        pthread_mutex_unlock(&mutex_b);
    }

    //if there are more cars in the queue, they can move forward now
    isEast = 0;

    //increase the number of empty crossings
    pthread_mutex_lock(&mutex_emptyNum);
    emptyCrossNum++;
    pthread_mutex_unlock(&mutex_emptyNum);

    printf("car %d from East leaving crossing.\n", carId);

    usleep(2000);
    pthread_mutex_unlock(&mutex_c);

    //if the car on its right woke up this car, it needs to wake up a new car on the right queue
    if (isEastWait == 1)
    {
        while (isDeadlock == 1);
        usleep(2000);
        if (queueNorth.threadNum > 0 && isNorth == 0)
        {
            queueNorth.curThread = pop(&queueNorth);
            pthread_cond_signal(&firstNorth);
        }
    }
    //if not, wake up the car on its own queue
    if (queueEast.threadNum > 0)
    {
        while (isDeadlock == 1);
        usleep(2000);
        if (isEast == 0)
        {
            queueEast.curThread = pop(&queueEast);
            pthread_cond_signal(&firstEast);
        }
    }

    return NULL;
}

void *southThread(void *param)
{
    //waiting until the car in the front of the queue be gone
    pthread_mutex_lock(&mutex_behind_a);
    pthread_cond_wait(&firstSouth, &mutex_behind_a);
    pthread_mutex_unlock(&mutex_behind_a);

    //now it's this car's turn
    int carId = queueSouth.curThread;
    isSouth = 1;     //now there is a car in the north
    isSouthWait = 0;
    lastArrive = 3;

    printf("car %d from South arrives at crossing.\n", carId);

    //it wants to pass a
    pthread_mutex_lock(&mutex_a);

    //decrease the number of empty crossings
    pthread_mutex_lock(&mutex_emptyNum);
    emptyCrossNum--;
    pthread_mutex_unlock(&mutex_emptyNum);

    if (emptyCrossNum == 0)     //a deadlock happens
    {
        pthread_cond_signal(&deadlockCond);     //wake up the deadlock-detecting thread

        //wait for being signaled
        pthread_cond_wait(&cond_wait_a, &mutex_a);

        //woken up by the car on its right
        //now all the cars in the crossing have been gone
        pthread_mutex_lock(&mutex_b);

        isDeadlock = 0;

        pthread_mutex_unlock(&mutex_a);

        usleep(2000);
        if (queueEast.threadNum > 0 && isEast == 0)
        {
            queueEast.curThread = pop(&queueEast);
            pthread_cond_signal(&firstEast);
        }
    }
    else
    {
        if (isEast == 1)    //if there is a car waiting on its right, let it go first
        {
            //wait in a for being signaled
            isSouthWait = 1;
            pthread_mutex_lock(&mutex_in_a);
            pthread_cond_wait(&cond_in_a, &mutex_in_a);

            //woken up by that car or the deadlock-detecting thread
            pthread_mutex_lock(&mutex_b);
            pthread_mutex_unlock(&mutex_a);
            pthread_mutex_unlock(&mutex_in_a);
        }
        else    //if not, just move on and leave the crossing
        {
            pthread_mutex_lock(&mutex_b);
            pthread_mutex_unlock(&mutex_a);
        }

        //it now has to wake up the car on the left that has been waiting
        if (isDeadlock == 1 && lastArrive == 4)    //a deadlock happened and the car on its left arrived last
        {
            pthread_cond_signal(&cond_wait_d);
        }
        else if (isWest == 1 && isWestWait == 1)
        {
            pthread_cond_signal(&cond_in_d);
        }
    }

    //if there are more cars in the queue, they can move forward now
    isSouth = 0;

    //increase the number of empty crossings
    pthread_mutex_lock(&mutex_emptyNum);
    emptyCrossNum++;
    pthread_mutex_unlock(&mutex_emptyNum);

    printf("car %d from South leaving crossing.\n", carId);

    usleep(2000);
    pthread_mutex_unlock(&mutex_b);

    if (isSouthWait == 1)
    {
        while (isDeadlock == 1);
        usleep(2000);
        if (queueEast.threadNum > 0 && isEast == 0)
        {
            queueEast.curThread = pop(&queueEast);
            pthread_cond_signal(&firstEast);
        }
    }
    if (queueSouth.threadNum > 0)
    {
        while (isDeadlock == 1);
        usleep(2000);
        if (isSouth == 0)
        {
            queueSouth.curThread = pop(&queueSouth);
            pthread_cond_signal(&firstSouth);
        }
    }

    return NULL;
}

void *westThread(void *param)
{
    //waiting until the car in the front of the queue be gone
    pthread_mutex_lock(&mutex_behind_d);
    pthread_cond_wait(&firstWest, &mutex_behind_d);
    pthread_mutex_unlock(&mutex_behind_d);

    //now it's this car's turn
    int carId = queueWest.curThread;
    isWest = 1;     //now there is a car in the north
    isWestWait = 0;
    lastArrive = 4;

    printf("car %d from West arrives at crossing.\n", carId);

    //it wants to pass d
    pthread_mutex_lock(&mutex_d);

    //decrease the number of empty crossings
    //decrease first because we want to detect a deadlock before it happens
    pthread_mutex_lock(&mutex_emptyNum);
    emptyCrossNum--;
    pthread_mutex_unlock(&mutex_emptyNum);

    if (emptyCrossNum == 0)     //a deadlock happens
    {
        pthread_cond_signal(&deadlockCond);     //wake up the deadlock-detecting thread

        //wait for being signaled
        pthread_cond_wait(&cond_wait_d, &mutex_d);

        //woken up by the car on its right
        //now all the cars in the crossing have been gone
        pthread_mutex_lock(&mutex_a);

        isDeadlock = 0;

        pthread_mutex_unlock(&mutex_d);

        usleep(2000);
        if (queueSouth.threadNum > 0 && isSouth == 0)
        {
            queueSouth.curThread = pop(&queueSouth);
            pthread_cond_signal(&firstSouth);
        }
    }
    else
    {
        if (isSouth == 1)    //if there is a car waiting on its right, let it go first
        {
            //wait in a for being signaled
            isWestWait = 1;
            pthread_mutex_lock(&mutex_in_d);
            pthread_cond_wait(&cond_in_d, &mutex_in_d);

            //woken up by that car or the deadlock-detecting thread
            pthread_mutex_lock(&mutex_a);
            pthread_mutex_unlock(&mutex_d);
            pthread_mutex_unlock(&mutex_in_d);
        }
        else    //if not, just move on and leave the crossing
        {
            pthread_mutex_lock(&mutex_a);
            pthread_mutex_unlock(&mutex_d);
        }

        //it now has to wake up the car on the left that has been waiting
        if (isDeadlock == 1 && lastArrive == 1)     //a deadlock happened, and the car on its left arrived last
        {
            pthread_cond_signal(&cond_wait_c);
        }
        else if (isNorth == 1 && isNorthWait == 1)
        {
            pthread_cond_signal(&cond_in_c);
        }
    }

    //if there are more cars in the queue, they can move forward now
    isWest = 0;

    //increase the number of empty crossings
    pthread_mutex_lock(&mutex_emptyNum);
    emptyCrossNum++;
    pthread_mutex_unlock(&mutex_emptyNum);

    printf("car %d from West leaving crossing.\n", carId);

    usleep(2000);
    pthread_mutex_unlock(&mutex_a);

    if (isWestWait == 1)
    {
        while (isDeadlock == 1);
        usleep(2000);
        if (queueSouth.threadNum > 0 && isSouth == 0)
        {
            queueSouth.curThread = pop(&queueSouth);
            pthread_cond_signal(&firstSouth);
        }
    }
    if (queueWest.threadNum > 0)
    {
        while (isDeadlock == 1);
        usleep(2000);
        if (isWest == 0)
        {
            queueWest.curThread = pop(&queueWest);
            pthread_cond_signal(&firstWest);
        }
    }

    return NULL;
}

void *deadlockThread(void *param)   //to detect whether there is a deadlock
{
    while (1)
    {
        //wait for being signaled
        pthread_mutex_lock(&deadLockMutex);
        pthread_cond_wait(&deadlockCond, &deadLockMutex);

        //woken up and mark the flag
        isDeadlock = 1;

        //if deadlock happens, let the last arrived car's left car go first
        printf("DEADLOCK: car jam detected, signaling ");
        if (lastArrive == 1)    //North
        {
            pthread_cond_signal(&cond_in_b);
            printf("East to go.\n");
        }
        else if (lastArrive == 2)   //East
        {
            pthread_cond_signal(&cond_in_a);
            printf("South to go.\n");
        }
        else if (lastArrive == 3)   //South
        {
            pthread_cond_signal(&cond_in_d);
            printf("West to go.\n");
        }
        else if (lastArrive == 4)   //West
        {
            pthread_cond_signal(&cond_in_c);
            printf("North to go.\n");
        }

        pthread_mutex_unlock(&deadLockMutex);
    }
}


int main()
{
    //initialize the crossing mutex locks and conditions
    pthread_mutex_init(&mutex_a, NULL);
    pthread_mutex_init(&mutex_b, NULL);
    pthread_mutex_init(&mutex_c, NULL);
    pthread_mutex_init(&mutex_d, NULL);
    pthread_cond_init(&cond_wait_a, NULL);
    pthread_cond_init(&cond_wait_b, NULL);
    pthread_cond_init(&cond_wait_c, NULL);
    pthread_cond_init(&cond_wait_d, NULL);

    //initialize the staying-behind mutex locks
    pthread_mutex_init(&mutex_behind_a, NULL);
    pthread_mutex_init(&mutex_behind_b, NULL);
    pthread_mutex_init(&mutex_behind_c, NULL);
    pthread_mutex_init(&mutex_behind_d, NULL);

    //initialize conditions of next passing direction
    pthread_cond_init(&firstNorth, NULL);
    pthread_cond_init(&firstEast, NULL);
    pthread_cond_init(&firstSouth, NULL);
    pthread_cond_init(&firstWest, NULL);

    //initilize mutex locks and conditions of whether can move on at the first crossing
    pthread_mutex_init(&mutex_in_a, NULL);
    pthread_mutex_init(&mutex_in_b, NULL);
    pthread_mutex_init(&mutex_in_c, NULL);
    pthread_mutex_init(&mutex_in_d, NULL);
    pthread_cond_init(&cond_in_a, NULL);
    pthread_cond_init(&cond_in_b, NULL);
    pthread_cond_init(&cond_in_c, NULL);
    pthread_cond_init(&cond_in_d, NULL);

    //initialize the mutex lock of empty crossing number
    pthread_mutex_init(&mutex_emptyNum, NULL);

    //initialize condition and mutex lock of the deadlock-detecting thread
    pthread_cond_init(&deadlockCond, NULL);
    pthread_mutex_init(&deadLockMutex, NULL);

    //there is no car in each direction at first
    isNorth = 0;
    isEast = 0;
    isSouth = 0;
    isWest = 0;
    //no deadlock
    isDeadlock = 0;

    printf("Enter the car directions.\n");
    char *buffer = (char*)malloc(sizeof(char) * 400);
    scanf("%s", buffer);
    int i;
    for (i = 0; i < strlen(buffer); i++)
    {
        switch(buffer[i])
        {
            case 'n':
            {   
                //create a new north-type thread
                push(&queueNorth, totalThreadNum + 1);
                isNorth = 1;
                pthread_create(&threads[totalThreadNum++], NULL, northThread, NULL);
                break;
            }
            case 'e':
            {
                //create a new east-type thread
                push(&queueEast, totalThreadNum + 1);
                isEast = 1;
                pthread_create(&threads[totalThreadNum++], NULL, eastThread, NULL);
                break;
            }
            case 's':
            {
                //create a new south-type thread
                push(&queueSouth, totalThreadNum + 1);
                isSouth = 1;
                pthread_create(&threads[totalThreadNum++], NULL, southThread, NULL);
                break;
            }
            case 'w':
            {
                //create a new west-type thread
                push(&queueWest, totalThreadNum + 1);
                isWest = 1;
                pthread_create(&threads[totalThreadNum++], NULL, westThread, NULL);
                break;
            }
            default:
            {
                printf("Invalid input!\n");
                exit(1);
            }
        }
    }

    //create the deadlock-detecting thread
    pthread_create(&deadLockThreadId, NULL, deadlockThread, NULL);

    //wait until all the threads start to wait
    usleep(6000);
    
    //wake up all the threads first
    if (queueNorth.threadNum > 0)
    {
        queueNorth.curThread = pop(&queueNorth);
        pthread_cond_signal(&firstNorth);  //North
    }
    if (queueEast.threadNum > 0)
    {
        queueEast.curThread = pop(&queueEast);
        pthread_cond_signal(&firstEast);  //East
    }
    if (queueSouth.threadNum > 0)
    {
        queueSouth.curThread = pop(&queueSouth);
        pthread_cond_signal(&firstSouth);  //South
    }
        
    if (queueWest.threadNum > 0)
    {
        queueWest.curThread = pop(&queueWest);
        pthread_cond_signal(&firstWest);  //West
    }

    //wait for the threads to exit
    for (i = 0; i < totalThreadNum; i++)
        pthread_join(threads[i], NULL);

    free(buffer);
    return 0;
}