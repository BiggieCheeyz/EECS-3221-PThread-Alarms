/*
* New_Alarm_Cond.c

    AUTHORS **
  Francis Okoyo
  Tyler Noble
  Adham El-Shafie
  Lindan Thillanayagam


* This is an enhancement to the alarm_cond.c program, which
* used only a mutex and condition variables to synchronize acces
* to a list of alarms. This versions does the job by only using
* semaphores
*/
#include <pthread.h>
#include <time.h>
#include "errors.h"
#include <semaphore.h>

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
  int               type; //identifies the message type ( type >= 1 )
  int               prev_type; // previous message type
  int               is_new; // 1 == "new" and 0 == "old/not new"
  int               number; /* Message Number */
  int               request_type; // TypeA == 1 TypeB == 2 TypeC == 3
  int               expo; // signifies that the type change was confirmed
  /*******************end new additions***************/
} alarm_t;

/*
*
* Thread structure used to keep a linked list of thread id's which will be
* organized by their message type.
*
* This is a replacement of the sparce matrix used in the previous project
* "New_Alarm_Mutex.c". This is a lot more efficient as it does not Allocate
* an unneccesary amount of space. we do loose the O(1) acces time though.
*/
typedef struct thread_tag { // NEW STRUCT
  struct thread_tag     *link;
  pthread_t             thread_id;
  int                   type;
  int                   number;

} thread_t;

sem_t rw_sem;
int read_count = 0; // number o readers using the list
int writing = 0; //flag to notify that there is a writer writing to the list
int ready = 0; // flag to notify readers that a writer is about to write

alarm_t *alarm_list = NULL;
time_t current_alarm = 0;
thread_t *thread_list = NULL;  // List of Thread id's

const int TYPE_A = 1; // Constants to specify alarm request type
const int TYPE_B = 2;
const int TYPE_C = 3;

int insert_flag; //1 if a new alarm has been inserted. set to 0 after processing

/***************************HELPER CODE***************************//////////////
/*
* prints out contents of the thread list as well as the contents of the alarm
* list for debugging
*/
void display_lists(){
  thread_t **last, *next;
  alarm_t **alast, *anext;

  last = &thread_list;
  next = *last;

  alast = &alarm_list;
  anext = *alast;

  printf ("[Thread List: ");
  for (next = thread_list; next != NULL; next = next->link)
    printf ("{message type = %d thread_id = <%lu>} ",next->type,next->thread_id);
  printf ("]\n");

  printf ("[Alarm List: ");
    for (anext = alarm_list; anext != NULL; anext = anext->link)
      printf (" {Request Type = %d Alarm # = %d message type = %d} ",
    		  anext->request_type, anext->number, anext->type);
  printf ("]\n");
}


/*
* checks if an alarm's type has changed
*
* returns 1 if so and 0 otherwise
*/
int check_prev(alarm_t *a){

  if(a->type != a->prev_type)
    return 1;

  return 0;
}


/*
* Check the alarm list to see if a Type A alarm of this type number exists.
*
* return 1 if so and 0 otherwise.
*
*/
int check_type_a_exists(int type){
  int     status;
  alarm_t *next, **last;

  last = &alarm_list;
  next = *last;
  while (next != NULL) {
    if(next->type == type && next->request_type == TYPE_A){

      return 1;
    }

    last = &next->link;
    next = next->link;
  }

  return 0;
}

/*
* Check the alarm list to see if a Type A alarm of this message number exists.
*
* return 1 if so and 0 otherwise.
*
* Requires Mutex for alarm list to prevent writing while readers are reading
*/
int check_number_a_exists(int num){
  int     status;
  alarm_t *next, **last;

  last = &alarm_list;
  next = *last;
  while (next != NULL) {
    if(next->number == num && next->request_type == TYPE_A){
      return 1;
    }

    last = &next->link;
    next = next->link;
  }

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

  last = &alarm_list;
  next = *last;
  while (next != NULL) {
    if(next->type == type && next->request_type == req){
      return 1; // it exists already
    }

    last = &next->link;
    next = next->link;
  }
  return 0; // It doesn't exist.
}

/*
* Check the alarm list to see if an alarm with this number already exists.
* Takes the message number and request type as parameters
*
* return 1 if so and 0 otherwise.
*
*/
int check_dup_2(int num, int req){
  int     status;
  alarm_t *next, **last;

  last = &alarm_list;
  next = *last;
  while (next != NULL) {
    if(next->number == num && next->request_type == req){

      return 1; // it exists already
    }

    last = &next->link;
    next = next->link;
  }
  return 0; // It doesn't exist.
}

/*
* Removes a Type A alarm of the specified message number from the alarm list
*
* Returns the message type of the alarm that was just removed from alarm list
*
* Requires Mutex for alarm list to prevent writing while readers are reading
* Mutex is needed because this method removes from (writes to) the alarm list
*/
int remove_alarm(int number){
  alarm_t **last, *next;
  int val = 0;

  /*
  * LOCKING PROTOCOL:
  *
  * This routine requires that the caller have locked the
  * alarm_mutex!
  */
  last = &alarm_list;
  next = *last;

  /*
  * If list is empty, return 0;
  */
  if (next == NULL)
    return val;

  while (next != NULL){

    /*
    * if we find the alarm within the list, delete it.
    */
    if (next->number == number && next->request_type == TYPE_A){
      val = next->type;
      *last = next->link;
      free(next);
      break; // remove the thread the Alarm.
    }

    last = &next->link;
    next = next->link;
  }

  return val;

}

/*
* Removes a type B alarm request responsible for type A alarms with the
* specified type
*
* Requires Mutex for alarm list to prevent writing while readers are reading
* Mutex is needed because this method removes from (writes to) the alarm list
*/
void remove_alarm_B(int type){
  alarm_t **last, *next;
  /*
  * LOCKING PROTOCOL:
  *
  * This routine requires that the caller have locked the
  * alarm_mutex!
  */
  last = &alarm_list;
  next = *last;

  while (next != NULL){
    /*
    * if we find the alarm within the list, delete it.
    */
    if (next->request_type == TYPE_B && next->type == type){
      *last = next->link;
      free(next);
      break; // remove the thread the Alarm.
    }

    last = &next->link;
    next = next->link;
  }// End while
}


/*
* Removes a type C alarm request responsible for cancelling alarms with the
* specified alarm number type
*
* Requires Mutex for alarm list to prevent writing while readers are reading
* Mutex is needed because this method removes from (writes to) the alarm list
*/
void remove_alarm_C(int number){
  alarm_t **last, *next;
  /*
  * LOCKING PROTOCOL:
  *
  * This routine requires that the caller have locked the
  * alarm_mutex!
  */
  last = &alarm_list;
  next = *last;

  while (next != NULL){
    /*
    * if we find the alarm within the list, delete it.
    */
    if (next->request_type == TYPE_C && next->number == number){
      *last = next->link;
      free(next);
      break; // remove the thread the Alarm.
    }

    last = &next->link;
    next = next->link;
  }// End while
}

/*
* Insert alarm entry on list, in order of message number.
*
* Requires Mutex for alarm list to prevent writing while readers are reading
* Mutex is needed because this method removes from (writes to) the alarm list
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
    if (next->number == alarm->number && alarm->request_type == TYPE_A){//A.3.2.2

      // swap the nodes (Replacement)
      alarm->link = next->link;
      alarm->prev_type = next->type;
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
}

///THREAD STUFF

/*
* insert thread id into the thread list in order of Message Type
*
*/
void insert_thread(thread_t *thread){

  thread_t **last, *next;

  last = &thread_list;
  next = *last;
  while (next != NULL) {

    /*
    * insert the thread id into the thread list
    */
    if (next->type > thread->type){

      thread->link = next;
      *last = thread;
      break; // Add the Alarm.

     }

    last = &next->link;
    next = next->link;
  }
  /*
  * If we reached the end of the list, insert the new thread
  * there.  ("next" is NULL, and "last" points to the link
  * field of the last item, or to the list header.)
  */
  if (next == NULL) {
    *last = thread;
    thread->link = NULL;
  }
}

/*
* ittereate through the thread list and terminate threads
* of MessageType(Type)
* also removes it from the thread list
*
* Note that every thread is allowed to complete its routine before terminatied
* this is to avoid the mutex being locked and not having a way to unlock it
*
*/
void terminate_thread(int type){
  thread_t **last, *next;

  last = &thread_list;
  next = *last;

  while (next != NULL){

    /*
    * if we find the thread within the list, delete it.
    */
    if (next->type == type){

      int success = pthread_cancel(next->thread_id); //terminate that thread
      if(success != 0) // checks if the thread was successfuly terminated
        err_abort (success, "thread was not canceled");

      *last = next->link;
      free(next);
      break; // remove the thread.

    }
    last = &next->link;
    next = next->link;
  }// End while

}

/*
*
* Check the thread list to see if there are any useless threads in the list.
* A thread is considered useless if there are no Type A alarms of its message
* type available to be printed
*
* terminate the thread if such thread exists and return 1. If no such thread
* exists, return 0
*
*/
int check_useless_thread(){
  thread_t **last, *next;

  last = &thread_list;
  next = *last;

  /*
  * loop throught the thread list and check the alarm list for Type A alarms
  * that have the same message type as the thread. if at least 1 exists, return
  * 0.
  */
  while(next != NULL){

    if(check_type_a_exists(next->type) == 0){
      terminate_thread(next->type);
      return 1;
    }
    next = next->link;
  }

  return 0;
}

/*
* same as above but no thread termination
*/
int check_useless_thread_no_term(){
  thread_t **last, *next;

  last = &thread_list;
  next = *last;

  /*
  * loop throught the thread list and check the alarm list for Type A alarms
  * that have the same message type as the thread. if at least 1 exists, return
  * 0.
  */
  while(next != NULL){
    if(check_type_a_exists(next->type) == 0){
      return 1;
    }
    next = next->link;
  }
  return 0;
}
/***************************END HELPER CODE***************************//////////


/* READER
*
* TYPE B CREATED THREAD (periodic display thread).
* responsible for periodically looking up a Type A alarm request with a
* Message Type in the alarm list, then printing, every Time seconds.
*
* A3.4
*/
void *periodic_display_thread(void *arg){
  alarm_t *alarm = alarm_list;
  struct timespec cond_time;
  time_t now;
  int status, expired, flag;

  int *arg_pointer = arg;
  int type = *arg_pointer; // parameter passed by the create thread call

  /*
  * Loop forever, processing Type A alarms of specified message type.
  * The alarm thread will be disintegrated when the process exits.
  */

  while (1){

    while(ready > 0){
      // wrtiter is ready to trite so don't do anything
    }

    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); //disable cancellation

    while (alarm_list == NULL){

      ///// ACHTUNG! /////
      pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL); //enable cancellation
      pthread_testcancel();
    }

    read_count++;
    /////
    if (flag == 1){
      alarm = alarm_list; // go back to the beginning
      flag = 0;
    }
    if (alarm->link == NULL){
      flag = 1; // go back to the beginning of the list
    }
    /////

    if(alarm->type != type && alarm->request_type == TYPE_A){ //A.3.4.2
      /*
      * check if its type has changed. if its type has changed from a different
      * one, notify the user that an alarm with the specified type which
      * previously had a different type has been assigned.
      */
      if(check_prev(alarm) == 1 && alarm->prev_type == type){
        if(alarm->expo == 0){ // check if alarm change has been acknowledged
          printf("Alarm With Message Type (%d) Replaced at <%d>: "
          "<Type A>\n", alarm->type, (int)alarm->time ); // A.3.4.2
          alarm->expo = 1; // alarm exposed (chanhe acknowledged)
        }
      }
    }
    else if(alarm->type == type && alarm->request_type == TYPE_A){ //A.3.4.1

      /*
      * Carry out the necessary operations to print out a message
      */
      now = time(NULL); //current time since EPOCH
      alarm->time = now + alarm->seconds; // alarm time from "now"

      while(alarm->time > now){ // wait
        now = time(NULL);

      } // wait done

     // PRINT MESSAGE // A.3.4.1
      printf("Alarm With Message Type (%d) and Message Number"
      " (%d) Displayed at <%d>: <Type A> :",
      alarm->type, alarm->number, (int)time(NULL) );
      printf ("\"%s\"\n", alarm->message);

    }
    alarm = alarm->link; // go to the next node on the list
    read_count--;

    /* used to avoid potential deadlock from thread termination
    */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL); //enable cancellation
    pthread_testcancel(); // set a cancellation point
  }// End While(1)
}

/*WRITER
*
* The alarm thread's start routine.
*
* An initial thread which is responsible for looping through the
* alarm list and checking the status of each type A alarm, as well as performing
* type B or C requests as they are inserted.
*
* A3.3
*/
void *alarm_thread (void *arg){

  alarm_t **last, *next;
  struct timespec cond_time;
  time_t now;
  int status, expired;

  /*
  * Loop forever, processing commands. The alarm thread will
  * be disintegrated when the process exits.
  */

  while (1){

    /*
    * If a new alarm hasnt been added, wait until a new alarm is
    * added
    */

     while (insert_flag == 0) {
       //busy wait
     }

    /*
    * when a new alarm has been inserted, loop through the alarm list and find
    * and process the alarm
    */
    last = &alarm_list;
    next = *last;

    while(next != NULL){

      /*
      * upon finding a new type A alarm, checks if there exists a useless
      * periodic display thread and terminates it if such thread exists.
      */
      if(next->request_type == TYPE_A){ // A.3.3.1
        if(next->is_new == 1){
          next->is_new = 0; // alarm is no longer new

          status = check_useless_thread();
          break;
        }
      }// END TYPE A ***************************///////

      /*
      * upon finding a new type B alarm, creates a periodic display thread
      * responsible for printing messages its specified type
      */
      else if(next->request_type == TYPE_B){ // A.3.3.2
        if(next->is_new == 1){

          next->is_new = 0; // alarm is no longer new
          thread_t *thrd;
          pthread_t thread;


          thrd = (thread_t*)malloc (sizeof (thread_t)); //allocate thread struct
          if (thrd == NULL)
          errno_abort ("Allocate Thread");

          /* create a thread for periodically printing messages
          *  pass message type as an argument
          */
          status = pthread_create(&thread, NULL, periodic_display_thread,
          &next->type);
          if (status != 0)
            err_abort (status, "Create alarm thread"); // A.3.3.2 (a)
          thrd->type = next->type; // set the attributes for the thread struct
          thrd->thread_id = thread;

          insert_thread(thrd);

          printf("Type B Alarm Request Processed at <%d>: New Periodic Dis"
          "play Thread With Message Type (%d) Created.\n", (int)(time(NULL)),
          next->type ); // A.3.3.2 (b)

          break;
        }
      }// END TYPE B ***************************///////

      /*
      * upon finding a new type C alarm, removes the alarm of the message
      * number specified by the Type C alarm from the alarm list.
      *
      * if there are no more alarm requests in the alarm list the same type as
      * the one that was just removed, terminate the periodic display thread
      * responsible for displaying those messages.
      *
      * This is the only part of the  alarm thread that writes to the alarm list
      */
      else if(next->request_type == TYPE_C){ //A.3.3.3
        int val;
        if(next->is_new == 1){
          next->is_new == 0; // alarm is no longer new


          ready++; // the writer is ready to use the alarm list
          while(read_count > 0 || writing > 0){
            // busy waits for readers to be done
          }
          status = sem_wait(&rw_sem);
          if(status != 0)
            err_abort(status, "rw_sem wait");
          writing++; // writer has control of the data structure


          val = remove_alarm(next->number); // A.3.3.3 (a)
          if(val != 0){ // A.3.3.3 (c)
            
            remove_alarm_C(next->number);// remove alarm from the alarm list
            printf("Type C Alarm Request Processed at <%d>: Alarm Request"
            " With Message Number (%d) Removed\n", (int)(time(NULL)),
            next->number);
          }

          if(check_type_a_exists(val) == 0){ // A.3.3.3 (b)

            terminate_thread(val);
            remove_alarm_B(val); // remove the alarm from alarm list
            remove_alarm_C(next->number);// remove alarm from the alarm list

            printf("No More Alarm Requests With Message Type (%d):"
            " Periodic Display Thread For Message Type (%d)"
            " Terminated.\n", val, val); // A.3.3.3 (d)

          }

          writing--;
          status = sem_post(&rw_sem);
          if(status != 0)
            err_abort(status, "rw_sem post");
          ready--;
        }
        break;
      }// END TYPE C ***************************///////
      next = next->link; //go to the next node
    } // End list loop
    insert_flag = 0; // finished looping and processd new alarm
  }
}

/*WRITER
* Parses inputs as specified in assaignment 3 outline
*
* Creates 3 different alarm requests (Type A - C) and inserts them into the
* alarm list. THe alarm thread then processes these alarm requests as they already
* inserted
*/
int main (int argc, char *argv[]){
  int status;
  char line[128];
  alarm_t *alarm;
  thread_t *thrd;
  pthread_t thread;

  status = sem_init(&rw_sem, 0, 1); // initialize reader writer Semaphore
  if(status != 0)
    err_abort(status, "Create READ-WRITE Semaphore");

  /*
  * Create the initial thread responsible for looping through the alarm list
  * and performing operations depening on the request type
  *
  * leaving the argument "NULL" would also imply that the initial thread
  */
  status = pthread_create (&thread, NULL, alarm_thread, NULL);
  if (status != 0) err_abort (status, "Create alarm thread");

  while (1) {
    printf ("alarm> ");
    if (fgets (line, sizeof (line), stdin) == NULL) exit (0);
    if (strlen (line) <= 1) continue;
    alarm = (alarm_t*)malloc (sizeof (alarm_t));
    if (alarm == NULL) errno_abort ("Allocate alarm");

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
    alarm->seconds > 0 && alarm->number > 0 && alarm->type > 0){ // A.3.2.1

      alarm->time = time (NULL) + alarm->seconds;
      alarm->request_type = TYPE_A;
      alarm->is_new = 1;
      alarm->prev_type = alarm->type;

      ready++; // the writer is ready to use the alarm list
      while(read_count > 0 || writing > 0){
        // wait for readers or writers to finish
      }
      status = sem_wait(&rw_sem);
      if(status != 0)
        err_abort(status, "rw_sem wait");
      writing++; // writer has control of the data structure
      /*
      * Insert the new alarm into the list of alarms, CRITICAL SECTION
      */
      alarm_insert (alarm);
      printf("Type A Alarm Request With Message Number <%d> Received at"
      " time <%d>: <Type A>\n", alarm->number, (int)time(NULL));

      insert_flag = 1; // a new alarm has been inserted

      writing--;
      status = sem_post(&rw_sem);
      if(status != 0)
        err_abort(status, "rw_sem post");
      ready--;

    }
    /*********************END TYPE A*************************/
    /*************************TYPE B*************************/
    else if (sscanf(line,"Create_Thread: MessageType(%d)",&alarm->type) == 1
    && alarm->type > 0){ // A.3.2.3 - A.3.2.5

      int exists = check_type_a_exists(alarm->type);
      int dup = check_dup(alarm->type, TYPE_B);
      /*
      * Creates a Type B alarm that is then inserted into the alarm list.
      * Does not allow for duplicate type B alarms.
      * Only creates one if there exists a type A alarm of type B's
      * Message Type.
      */

      if(exists == 0){ // A.3.2.3

        printf("Type B Alarm Request Error: No Alarm Request With Message Type"
        "(%d)!\n", alarm->type);
        free(alarm); // deallocate alarm that isn't used

      }else if(dup == 1){ // A.3.2.4
        // May need to fix as there is confusion between "Number" and "Type"
        printf("Error: More Than One Type B Alarm Request With"
          " Message Type (%d)!\n", alarm->type );
        free(alarm); // deallocate alarm that isn't used

      }else if(exists == 1 && dup == 0){ //A.3.2.5

        alarm->request_type = TYPE_B;
        alarm->is_new = 1;

        ready++; //
        while(read_count > 0 || writing > 0){
          // busy wait
        }
        status = sem_wait(&rw_sem);
        if(status != 0)
          err_abort(status, "rw_sem wait");
        writing++; // writer has control of the data structure

        /*
        * Insert the new alarm into the list of alarms
        * Insert the new thread into the list of threads
        */
        alarm_insert (alarm);
        printf("Type B Create Thread Alarm Request With Message Type (%d)"
        " Inserted Into Alarm List at <%d>!\n", alarm->type, (int)time(NULL));
        insert_flag = 1; // a new alarm has been inserted

        writing--;
        status = sem_post(&rw_sem);
        if(status != 0)
          err_abort(status, "rw_sem post");
        ready--;
      }
    }
    /*********************END TYPE B*************************/
    /*************************TYPE C*************************/
    else if (sscanf (line, "Cancel: Message(%d)", &alarm->number) == 1 &&
    alarm->number > 0 ){ //

      int exists = check_number_a_exists(alarm->number);
      int dup2 = check_dup_2(alarm->number, TYPE_C);

      /*
      * Creates a Type C alarm that is then inserted into the alarm list.
      * Does not allow for duplicate type alarms.
      * Only creates one if there exists a type A alarm of type C's Message
      * Type.
      */

      if (exists == 0){ // A.3.2.6

        printf("Error: No Alarm Request With Message"
          " Number (%d) to Cancel!\n", alarm->number );
        free(alarm);

      }else if (dup2 == 1){ // A.3.2.7

        printf("Error: More Than One Request to Cancel Alarm Request With"
          " Message Number (%d)!\n", alarm->number);
        free(alarm);

      }else if (exists == 1 && dup2 == 0 ){ // A.3.2.8

        alarm->request_type = TYPE_C;
        alarm->is_new = 1;


        ready++;
        while(read_count > 0 || writing > 0){
          // busy wait
        }
        status = sem_wait(&rw_sem);
        if(status != 0)
          err_abort(status, "rw_sem wait");
        writing++; // writer has control of the data structure

        /*
        * Insert the new alarm into the list of alarms.
        */
        alarm_insert (alarm);
        printf("Type C Cancel Alarm Request With Message Number (%d)"
          " Inserted Into Alarm List at <%d>: <Type C>\n", alarm->number,
              (int)time(NULL));

        insert_flag = 1; // a new alarm has been inserted

        writing--;
        status = sem_post(&rw_sem);
        if(status != 0)
          err_abort(status, "rw_sem post");
        ready--;
      }
    }
    /*********************END TYPE C*************************/
    else if (sscanf(line,"%d", &status) == 1 && status == 15){ // debugging
      printf("**DEBUG MODE ENGAGED**\n");
      display_lists();
      printf("Ready = %d read_count = %d writing = %d\n", ready,
       read_count, writing );
      printf("**DEBUG MODE DISENGAGED**\n");
    }
    else{
      fprintf (stderr, "Bad command\n");
      free (alarm);
    }
  }// end while
}
