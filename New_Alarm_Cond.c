/*
* alarm_cond.c
*
* This is an enhancement to the alarm_mutex.c program, which
* used only a mutex to synchronize access to the shared alarm
* list. This version adds a condition variable. The alarm
* thread waits on this condition variable, with a timeout that
* corresponds to the earliest timer request. If the main thread
* enters an earlier timeout, it signals the condition variable
* so that the alarm thread will wake up and process the earlier
* timeout first, requeueing the later request.
*/
#include <pthread.h>
#include <time.h>
#include "errors.h"

// #define DEBUG
/*
* The "alarm" structure now contains the time_t (time since the
* Epoch, in seconds) for each alarm, so that they can be
* sorted. Storing the requested number of seconds would not be
* enough, since the "alarm thread" cannot tell how long it has
* been on the list.
*/
typedef struct alarm_tag {
  struct alarm_tag    *link;
  int                 seconds;
  time_t              time;   /* seconds from EPOCH */
  char                message[128];

  /******* new additions to the alarm_tag structure ********/
  int               type; //identifies the type of alarm request ( type >= 1 )'
  int               status; // 0 == "unassigned" and 1 == "assigned"
  int               number; /* Message Number */
  int               request_type; // TypeA == 1 TypeB == 2 TypeC == 3
  /*******************end new additions***************/
} alarm_t;


pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alarm_cond = PTHREAD_COND_INITIALIZER;
alarm_t *alarm_list = NULL;
time_t current_alarm = 0;

const int TYPE_A = 1;
const int TYPE_B = 2;
const int TYPE_C = 3;



/***************************NEW CODE***************************/////////////////

/*
* Stores the thread id's indexed by MessageType (Type) to be accesed in
* O(1) time
*
* implementation changed from 1d array to 2d (sparce matrix)
* the first dimention stores the number of threads that exist for the specified
* message type. this value allows us to iteraten through the second dimention
* lot[type][0] many times to allow us to terminate all those threads
*/
pthread_t lot[999][999];

// testing array after creating or termination a thread
// FOR DEBUGGING PURPOSES
void test(int type){
  int i;
  int j = lot[type][0];

  for(i = 1; i <= j; i++ ){
    printf("%lu\n",lot[type][i]);
  }
}

/*
* Check the alarm list to see if a Type A alarm of this type number exists.
*
* return 1 if so and 0 otherwise.
*/
int check_type_a_exists(int type){
  int     status;
  alarm_t *next, **last;

  status = pthread_mutex_lock (&alarm_mutex);
  if (status != 0)
    err_abort (status, "Lock mutex");

  last = &alarm_list;
  next = *last;
  while (next != NULL) {
    if(next->type == type && next->request_type == TYPE_A){

      status = pthread_mutex_unlock (&alarm_mutex);
      if (status != 0)
        err_abort (status, "Unlock mutex");

      return 1;
    }

    last = &next->link;
    next = next->link;
  }
  status = pthread_mutex_unlock (&alarm_mutex);
  if (status != 0)
    err_abort (status, "Unlock mutex");
  return 0;
}

/*
* Check the alarm list to see if a Type A alarm of this message number exists.
*
* return 1 if so and 0 otherwise.
*/
int check_number_a_exists(int num){
  int     status;
  alarm_t *next, **last;

  status = pthread_mutex_lock (&alarm_mutex);
  if (status != 0)
    err_abort (status, "Lock mutex");

  last = &alarm_list;
  next = *last;
  while (next != NULL) {
    if(next->number == num && next->request_type == TYPE_A){

      status = pthread_mutex_unlock (&alarm_mutex);
      if (status != 0)
        err_abort (status, "Unlock mutex");

      return 1;
    }

    last = &next->link;
    next = next->link;
  }
  status = pthread_mutex_unlock (&alarm_mutex);
  if (status != 0)
    err_abort (status, "Unlock mutex");
  return 0;
}

/*
* Check the alarm list to see if an alarm with this type already exists.
* Takes the message type and request type as parameters
*
* return 1 if so and 0 otherwise.
*/
int check_dup(int type, int req){
  int     status;
  alarm_t *next, **last;

  status = pthread_mutex_lock (&alarm_mutex);
  if (status != 0)
    err_abort (status, "Lock mutex");

  last = &alarm_list;
  next = *last;
  while (next != NULL) {
    if(next->type == type && next->request_type == req){

      status = pthread_mutex_unlock (&alarm_mutex);
      if (status != 0)
        err_abort (status, "Unlock mutex");

      return 1; // it exists already
    }

    last = &next->link;
    next = next->link;
  }
  status = pthread_mutex_unlock (&alarm_mutex);
  if (status != 0)
    err_abort (status, "Unlock mutex");
  return 0; // It doesn't exist.
}

/*
* Check the alarm list to see if an alarm with this number already exists.
* Takes the message number and request type as parameters
*
* return 1 if so and 0 otherwise.
*/
int check_dup_2(int num, int req){
  int     status;
  alarm_t *next, **last;

  status = pthread_mutex_lock (&alarm_mutex);
  if (status != 0)
    err_abort (status, "Lock mutex");

  last = &alarm_list;
  next = *last;
  while (next != NULL) {
    if(next->number == num && next->request_type == req){

      status = pthread_mutex_unlock (&alarm_mutex);
      if (status != 0)
        err_abort (status, "Unlock mutex");

      return 1; // it exists already
    }

    last = &next->link;
    next = next->link;
  }
  status = pthread_mutex_unlock (&alarm_mutex);
  if (status != 0)
    err_abort (status, "Unlock mutex");
  return 0; // It doesn't exist.
}

/*
* ittereate through the sparce matrix lot[][] and terminate all the threads
* of MessageType(Type)
* also removes them from the sparce matrix lot[][]
*
* Note that every thread is allowded to complete its routine before terminatied
* this is to avoid the mutex being locked and not having a way to unlock it
*/
void terminate(int type){
  int i;
  int j = lot[type][0];

  for(i = 1 ; i <= j; i++){
    //printf("%lu\n", lot[type][i]); // test
    int success = pthread_cancel(lot[type][i]); //terminate that thread

    if(success != 0){ // checks if the thread was successfuly terminated
      err_abort (success, "thread was not canceled");
    }
    lot[type][i] = 0;             // reset the thread id in that location
  }
  lot[type][0] = 0; // number of thread of that type are now 0;
}

/*
* Removes all alarms of the message type specified in the parameter
* utilizes a modified version of the code that inserts alarms.
*
* **CITATION: the section of code withing this code block that handles
* deleting the nodes from alarm_list was addapted from :
* https://www.geeksforgeeks.org/delete-occurrences-given-key-linked-list/
*
*
*/
void remove_alarms(int type, alarm_t **head){

  int status = pthread_mutex_lock (&alarm_mutex);
  if (status != 0)
  err_abort (status, "Lock mutex");

  // Store head node
  alarm_t *curr = *head, *last;
  // If head node itself holds the type or multiple occurrences of type
  while (curr != NULL && curr->type == type){
    *head = curr->link;   // Changed head
    free(curr);               // free old head
    curr = *head;         // Change curr
  }

  // Delete occurrences other than head
  while (curr != NULL){

    // Search for the type to be deleted, keep track of the
    // previous node as we need to change 'last->next'
    while (curr != NULL && curr->type != type){
      last = curr;
      curr = curr->link;
    }

    // If type was not present in linked list
    if (curr != NULL){
      // Unlink the node from linked list
      last->link = curr->link;
      free(curr);  // Free memory
      //Update curr for next iteration of outer loop
      curr = last->link;
    }
  }

  status = pthread_mutex_unlock (&alarm_mutex);
  if (status != 0)
  err_abort (status, "Unlock mutex");


}
/***************************END NEW CODE***************************/////////////

/*
* Insert alarm entry on list, in order of message number.
*/
void alarm_insert (alarm_t *alarm){
  int status;
  alarm_t **last, *next;

  /*
  * LOCKING PROTOCOL:
  *
  * This routine requires that the caller have locked the
  * alarm_mutex!
  */
  last = &alarm_list;
  next = *last;
  while (next != NULL) {

    /*
    * Replace existing alarm or insert the new alarm arranged by message number.
    *
    * If the alarm is a type B request, it will be inserted in the front as it
    * has a Message Number of 0.
    * If the alarm is a type C request, it will be inserted along Type A's.
    */
    if (next->number == alarm->number && alarm->request_type == TYPE_A){ //A.3.2.2

      // swap the nodes (Replacement)
      alarm->link = next->link;
      *last = alarm;
      free(next);
      printf("Type A Replacement Alarm Request With Message Number (%d) "
      "Received at <%d>: <A>\n", alarm->number, (int)time(NULL));
      break; // Add the Alarm.

    }else if (next->number > alarm->number){

      alarm->link = next;
      *last = alarm;
      break; // Add the Alarm.

     }

    last = &next->link;
    next = next->link;
  }
  /*
  * If we reached the end of the list, insert the new alarm
  * there.  ("next" is NULL, and "last" points to the link
  * field of the last item, or to the list header.)
  */
  if (next == NULL) {
    *last = alarm;
    alarm->link = NULL;
  }

  #ifdef DEBUG
    printf ("[list: ");
    for (next = alarm_list; next != NULL; next = next->link)
      printf ("%d(%d)[\"%s\"] ", next->number,
    next->type, next->message);
    printf ("]\n");
  #endif

  /*
  * Wake the alarm thread if it is not busy (that is, if
  * current_alarm is 0, signifying that it's waiting for
  * work), or if the new alarm comes before the one on
  * which the alarm thread is waiting.
  */
  if (current_alarm == 0 || alarm->time < current_alarm) {
    current_alarm = alarm->time;
    status = pthread_cond_signal (&alarm_cond);
    if (status != 0)
      err_abort (status, "Signal cond");
  }
}

/*
* The alarm thread's start routine.
*/
void *alarm_thread (void *arg){
  alarm_t *alarm;
  struct timespec cond_time;
  time_t now;
  int status, expired;

  /*
  * Loop forever, processing commands. The alarm thread will
  * be disintegrated when the process exits. Lock the mutex
  * at the start -- it will be unlocked during condition
  * waits, so the main thread can insert alarms.
  */
  status = pthread_mutex_lock (&alarm_mutex);
  if (status != 0)
    err_abort (status, "Lock mutex");
  while (1){
    /*
    * If the alarm list is empty, wait until an alarm is
    * added. Setting current_alarm to 0 informs the insert
    * routine that the thread is not busy.
    */
    current_alarm = 0;
    while (alarm_list == NULL){
      status = pthread_cond_wait (&alarm_cond, &alarm_mutex);
      if (status != 0)
        err_abort (status, "Wait on cond");
    }
    alarm = alarm_list;
    alarm_list = alarm->link;
    now = time (NULL);
    expired = 0;
    if (alarm->time > now){

                        #ifdef DEBUG
                          printf ("[waiting: %d(%d)\"%s\"]\n", (int)alarm->time,
                          (int)(alarm->time - time (NULL)), alarm->message);
                        #endif

      cond_time.tv_sec = alarm->time;
      cond_time.tv_nsec = 0;
      current_alarm = alarm->time;
      while (current_alarm == alarm->time){
        status = pthread_cond_timedwait (&alarm_cond, &alarm_mutex, &cond_time);
        if (status == ETIMEDOUT){
          expired = 1;
          break;
        }
        if (status != 0)
          err_abort (status, "Cond timedwait");
      }
      if (!expired)
        alarm_insert (alarm);
      } else{
        expired = 1;
      }
      if (expired) {
        printf ("(%d) %s\n", alarm->seconds, alarm->message);
        free (alarm);
      }
    }
  }

int main (int argc, char *argv[]){
  int status;
  char line[128];
  alarm_t *alarm;
  pthread_t thread;

  while (1) {
    printf ("alarm> ");
    if (fgets (line, sizeof (line), stdin) == NULL) exit (0);
    if (strlen (line) <= 1) continue;
    alarm = (alarm_t*)malloc (sizeof (alarm_t));
    if (alarm == NULL)
    errno_abort ("Allocate alarm");

    /*
    * Parse input line into seconds (%d) and a message
    * (%64[^\n]), consisting of up to 64 characters
    * separated from the seconds by whitespace.
    *
    * Checks what type of alarm / message is being entered.
    *
    */
    /*************************TYPE A*************************/
    if (sscanf (line, "%d MessageType(%d, %d) %128[^\n]",
    &alarm->seconds, &alarm->type, &alarm->number, alarm->message) == 4 &&
    (alarm->seconds > 0 && alarm->number > 0 &&
      alarm->type > 0 && alarm->type <= 999)){ // A.3.2.1

        status = pthread_mutex_lock (&alarm_mutex);
        if (status != 0)
          err_abort (status, "Lock mutex");
        alarm->time = time (NULL) + alarm->seconds;
        alarm->request_type = TYPE_A;
        /*
        * Insert the new alarm into the list of alarms,
        */
        alarm_insert (alarm);
        printf("Type A Alarm Request With Message Number <%d> Received at"
        " time <%d>: <Type A>\n", alarm->number, (int)time(NULL));

        status = pthread_mutex_unlock (&alarm_mutex);
        if (status != 0)
          err_abort (status, "Unlock mutex");

      }
      /*********************END TYPE A*************************/
      /*************************TYPE B*************************/
      else if (sscanf(line,"Create_Thread: MessageType(%d)",&alarm->type) == 1 &&
        (alarm->type > 0 && alarm->type <= 999)){ // A.3.2.3 - A.3.2.5

        /*
        * Creates a Type B alarm that is then inserted into the alarm list.
        * Does not allow for duplicate type B alarms.
        * Only creates one if there exists a type A alarm of type B's
        * Message Type.
        */

        if(check_type_a_exists(alarm->type) == 0){ // A.3.2.3

          printf("Type B Alarm Request Error: No Alarm Request With Message Type"
          "(%d)!\n", alarm->type);
          free(alarm); // deallocate alarm that isn't used

        }else if(check_dup(alarm->type, TYPE_B) == 1){ // A.3.2.4
          // May need to fix as there is confusion between "Number" and "Type"
          printf("Error: More Than One Type B Alarm Request With"
            " Message Type (%d)!\n", alarm->type );
          free(alarm); // deallocate alarm that isn't used

        }else if(check_type_a_exists(alarm->type) == 1 &&
                check_dup(alarm->type, TYPE_B) == 0){ //A.3.2.5

          status = pthread_mutex_lock (&alarm_mutex);
          if (status != 0)
            err_abort (status, "Lock mutex");
          alarm->time = time (NULL) + alarm->seconds;
          alarm->request_type = TYPE_B;
          /*
          * Insert the new alarm into the list of alarms,
          */
          alarm_insert (alarm);
          printf("Type B Create Thread Alarm Request With Message Type (%d)"
          " Inserted Into Alarm List at <%d>!\n", alarm->type, (int)time(NULL));

          status = pthread_mutex_unlock (&alarm_mutex);
          if (status != 0)
            err_abort (status, "Unlock mutex");

        }
      }
      /*********************END TYPE B*************************/
      /*************************TYPE C*************************/
      else if (sscanf (line, "Cancel: Message(%d)", &alarm->number) == 1 &&
        alarm->number > 0 ){ //
        /*
        * Creates a Type C alarm that is then inserted into the alarm list.
        * Does not allow for duplicate type  alarms.
        * Only creates one if there exists a type A alarm of type C's Message
        * Type.
        */

        if (check_number_a_exists(alarm->number) == 0){ // A.3.2.6

          printf("Error: No Alarm Request With Message"
            " Number (%d) to Cancel!\n", alarm->number );
          free(alarm);

        }else if (check_dup_2(alarm->number, TYPE_C) == 1){ // A.3.2.7

          printf("Error: More Than One Request to Cancel Alarm Request With"
            " Message Number (%d)!\n", alarm->number);
          free(alarm);

        }else if (check_number_a_exists(alarm->number) == 1 &&
            check_dup_2(alarm->number, TYPE_C) == 0 ){ // A.3.2.8

          status = pthread_mutex_lock (&alarm_mutex);
          if (status != 0)
            err_abort (status, "Lock mutex");
          alarm->time = time (NULL) + alarm->seconds;
          alarm->request_type = TYPE_C;

          /*
          * Insert the new alarm into the list of alarms.
          */
          alarm_insert (alarm);
          printf("Type C Cancel Alarm Request With Message Number (%d)"
            " Inserted Into Alarm List at <%d>: <Type C>\n", alarm->number,
                (int)time(NULL));

          status = pthread_mutex_unlock (&alarm_mutex);
          if (status != 0)
            err_abort (status, "Unlock mutex");

        }

      }
      /*********************END TYPE C*************************/
      else{
        fprintf (stderr, "Bad command\n");
        free (alarm);
      }
  }
}


////// back up for code
// lot[type][0] += 1; // increment # of threads of that type by 1
// int ind = lot[type][0]; // the index to store our new thread's Id
// lot[type][ind] = thread;
