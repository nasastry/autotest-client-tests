/*
 * (C) Copyright IBM Corp. 2004, 2005
 * Copyright (c) 2005, Intel Corporation
 * Copyright (c) 2005, University of New Hampshire
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Author(s):
 *     Kevin Gao <kevin.gao@intel.com>
 *     David Judkovics <djudkovi@us.ibm.com>
 *     Carl McAdams <carlmc@us.ibm.com>
 *     Xiaowei Yang <xiaowei.yang@intel.com>
 *     Donald A. Barre <dbarre@unh.edu>
 *
 * Spec:        HPI-B.01.01
 * Function:    saHpiEventGet
 * Description:   
 *      Call the function passing SAHPI_TIMEOUT_BLOCK
 * Line:        P62-18:P62-18
 */

#include <pthread.h>
#include <sys/select.h>
#include <stdio.h>
#include "saf_test.h"
#include <unistd.h>
#include <stdlib.h>

/*****************************************************************************
 * Constants
 *****************************************************************************/

#define MAX_REPEAT 8
#define MAX_PRE_EVENTS 128
#define WAIT_TIME 16 /* seconds */
#define EVENT_DATA "event test1"
#define EVENT_DATA_LENGTH strlen(EVENT_DATA)

/*****************************************************************************
 * Global variables
 *****************************************************************************/

SaHpiSessionIdT sessionId;
SaHpiBoolT runTimerThread;
long timerCounter;

/*****************************************************************************
 * Event to be added.
 *****************************************************************************/

SaHpiEventT new_event_1 = {
    .EventType = SAHPI_ET_USER,
    .Severity = SAHPI_INFORMATIONAL,
    .Source = SAHPI_UNSPECIFIED_RESOURCE_ID,
    .Timestamp = SAHPI_TIME_UNSPECIFIED,
    .EventDataUnion = {
        .UserEvent = {
            .UserEventData = {
                .DataType = SAHPI_TL_TYPE_TEXT,
                .Language = SAHPI_LANG_ZULU,
                .DataLength = EVENT_DATA_LENGTH,
                .Data = EVENT_DATA
            }
        }
    }
};

/*****************************************************************************
 * Sleep for a given number of seconds
 *
 * Since sleep() can have side effects due to system signals, use this
 * method to sleep for a given period of seconds.
 *****************************************************************************/

static void mySleep(unsigned int seconds) {
    struct timeval timeout;

    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    select( 0, NULL, NULL, NULL, & timeout );
}

/*****************************************************************************
 * Set the timer.  To prevent an event from being added to the system,
 * set the "timerCounter" to a negative value.  NOTE: Do not use zero due
 * to a possible race condition.
 *****************************************************************************/

static void setTimer(long seconds) {
    timerCounter = seconds;
}

/*****************************************************************************
 * Timer Thread function.
 *
 * Keep running until "runTimerThread" becomes false.  After sleeping for
 * one second, decrement the "timerCounter".  When it reaches zero, add an
 * event.  Note that if the "timerCounter" gets set to a negative value in
 * the above setTimer() function, no event will ever be added.  
 *****************************************************************************/

static void *timerThread(void *ptr) {

    while (runTimerThread) {
        mySleep(1);
        timerCounter--;
        if (timerCounter == 0) {
            m_print("No event generated, timeout!");
            saHpiEventAdd(sessionId, &new_event_1);
        }
    }

    return NULL;
}

/*****************************************************************************
 * Clear the Event Queue.
 *****************************************************************************/

static int clearEventQueue(SaHpiSessionIdT sessionId) {
    int retval = SAF_TEST_UNKNOWN;
    int i = 0;
    SaHpiEventT event;
    SaErrorT error;

    /* Clear the event queue */
    while (i++ < MAX_PRE_EVENTS) {
        error = saHpiEventGet(sessionId, SAHPI_TIMEOUT_IMMEDIATE,
                              &event, NULL, NULL, NULL);
        if (error == SA_ERR_HPI_TIMEOUT) {
            break;
        } else if (error != SA_OK) {
            retval = SAF_TEST_UNRESOLVED;
            e_print(saHpiEventGet, SA_OK, error);
            break;
        }
    }

    if (retval == SAF_TEST_UNKNOWN && i >= MAX_PRE_EVENTS) {
        retval = SAF_TEST_UNRESOLVED;
        m_print("Failed to clear event queue");
    }

    return retval;
}

/*****************************************************************************
 * Is this my event, i.e. is this event the one that was added by
 * the thread?
 *****************************************************************************/

static SaHpiBoolT isMyEvent(SaHpiEventT *event) {
    if (event->EventType == SAHPI_ET_USER) {
        SaHpiTextBufferT *userData = &(event->EventDataUnion.UserEvent.UserEventData);
        if ((userData->DataType == SAHPI_TL_TYPE_TEXT) &&
            (userData->Language == SAHPI_LANG_ZULU) &&
            (userData->DataLength == EVENT_DATA_LENGTH) &&
            (!memcmp(userData->Data, EVENT_DATA, EVENT_DATA_LENGTH))) {

            return SAHPI_TRUE;
        }
    }

    return SAHPI_FALSE;
}

/*****************************************************************************
 * Start the Timer Thread. 
 *****************************************************************************/

static void startTimerThread() {
    pthread_t thread1;
    pthread_attr_t tattr;

    // We will create a detached thread to add an event
    // to the HPI system.

    pthread_attr_init(&tattr);
    pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);

    runTimerThread = SAHPI_TRUE;
    timerCounter = -1;

    pthread_create(&thread1, &tattr, timerThread, NULL);
}

/*****************************************************************************
 * Stop the Timer Thread.
 *****************************************************************************/

static void stopTimerThread() {
    runTimerThread = SAHPI_FALSE;
}

/*****************************************************************************
 * Main Program
 *****************************************************************************/

int main() {

    SaHpiEventT event;
    SaErrorT error;
    int retval = SAF_TEST_UNKNOWN;
    int i = 0;

    error = saHpiSessionOpen(SAHPI_UNSPECIFIED_DOMAIN_ID, &sessionId, NULL);
    if (error != SA_OK) {
        retval = SAF_TEST_UNRESOLVED;
        e_print(saHpiSessionOpen, SA_OK, error);
    } else {
        error = saHpiSubscribe(sessionId);
        if (error != SA_OK) {
            retval = SAF_TEST_UNRESOLVED;
            e_print(saHpiSubscribe, SA_OK, error);
        } else {

            /* Try up to MAX_REPEAT times to test BLOCK.  If we successfully block
             * for WAIT_TIME seconds, then we assume that it is working properly.
             * When WAIT_TIME seconds expire, the "times_up()" method will be
             * invoked to indicate that the test passed.  If an event does
             * appear on the event queue, we need to reset the alarm
             * to WAIT_TIME seconds and try again.
             */

            startTimerThread();

            i = 0;
            while (i++ < MAX_REPEAT) {

                retval = clearEventQueue(sessionId);
                if (retval != SAF_TEST_UNKNOWN) {
                    break;
                } else {
                    setTimer(WAIT_TIME);

                    error = saHpiEventGet(sessionId, SAHPI_TIMEOUT_BLOCK,
                                          &event, NULL, NULL, NULL);

                    setTimer(-1);

                    if (error != SA_OK) {
                        retval = SAF_TEST_FAIL;
                        e_print(saHpiEventGet, SA_OK, error);
                        break;
                    } else if (isMyEvent(&event)) {
                        retval = SAF_TEST_PASS;
                        break;
                    }
                }
            }

            stopTimerThread();

            if (retval == SAF_TEST_UNKNOWN) {
                retval = SAF_TEST_UNRESOLVED;
                m_print("Too many system events occurred during the "
                        "%d second timeout period.", WAIT_TIME);
            }

            error = saHpiUnsubscribe(sessionId);
        }
        error = saHpiSessionClose(sessionId);
    }

    return retval;
}

