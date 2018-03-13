  else{

    alarm_t *alarm = alarm_list, *next;
    struct timespec cond_time;
    time_t now;
    int status, expired, flag;

    int *arg_pointer = arg;
    int type = *arg_pointer; // parameter passed by the create thread call

    /*
    * Loop forever, processing Type A alarms of specified message type.
    * The alarm thread will be disintegrated when the process exits.
    * Lock the mutex at the start -- it will be unlocked during condition
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
      /////
      if (flag == 1){ // go back to the beginning
        alarm = alarm_list;
        flag = 0;
      }
      next = alarm->link; // go to the next node on the list
      if (next == NULL){
        flag = 1; // go back to the beginning pf the list
      }
      /////
      now = time (NULL);
      expired = 0;
      if (alarm->time > now && alarm->type == type){ // WAIT

        // #ifdef DEBUG
        // printf ("[waiting: %d(%d)\"%s\"]\n", (int)alarm->time,
        // (int)(alarm->time - time (NULL)), alarm->message);
        // #endif

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
          //alarm_insert (alarm); // dont remove the alarm
      }
      else if(alarm->type == type){ // EXPIRED
        expired = 1;
      }

      if (expired) { // PRINT MESSAGE // A.3.4.1
        printf("Alarm With Message Type (%d) and Message Number"
        " (%d) Displayed at <%d>: <alarm_request>\n",
        alarm->type, alarm->number, (int)time(NULL) );
        //printf ("(%d) %s\n", alarm->seconds, alarm->message);
        //free (alarm); // dont deallocate the alarm
      }
    }
  } // end PART 2 periodic display thread
