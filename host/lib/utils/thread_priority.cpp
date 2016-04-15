//
// Copyright 2010-2011,2015 Ettus Research LLC
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include <uhd/utils/thread_priority.hpp>
#include <uhd/utils/msg.hpp>
#include <uhd/exception.hpp>
#include <boost/format.hpp>
#include <iostream>

#if defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__) || \
    defined(__FreeBSD__)
#include <mach/mach_init.h>
#include <mach/task_policy.h>
#include <mach/task.h>
#endif

bool uhd::set_thread_priority_safe(float priority, bool realtime){
    try{
        set_thread_priority(priority, realtime);
        return true;
    }catch(const std::exception &e){
        UHD_MSG(warning) << boost::format(
            "Unable to set the thread priority. Performance may be negatively affected.\n"
            "Please see the general application notes in the manual for instructions.\n"
            "%s\n"
        ) % e.what();
        return false;
    }
}

static void check_priority_range(float priority){
    if (priority > +1.0 or priority < -1.0)
        throw uhd::value_error("priority out of range [-1.0, +1.0]");
}

/***********************************************************************
 * Pthread API to set priority
 **********************************************************************/
#ifdef HAVE_PTHREAD_SETSCHEDPARAM
    #include <pthread.h>

    void uhd::set_thread_priority(float priority, bool realtime){
        check_priority_range(priority);

        //when realtime is not enabled, use sched other
        int policy = SCHED_OTHER;
        policy = (realtime)? SCHED_RR/*SCHED_FIFO*/ : SCHED_OTHER;

        //we cannot have below normal priority, set to zero
        if (priority < 0) priority = 0;

        //get the priority bounds for the selected policy
        int min_pri = sched_get_priority_min(policy);
        int max_pri = sched_get_priority_max(policy);
        if (min_pri == -1 or max_pri == -1) throw uhd::os_error("error in sched_get_priority_min/max");

        int ret = 0;

        //set the new priority and policy
        sched_param sp;
        sp.sched_priority = int(priority*(max_pri - min_pri)) + min_pri;
        ret = pthread_setschedparam(pthread_self(), policy, &sp);
        fprintf(stderr, "setschedparam: priority %d [min %d, max %d]\n", sp.sched_priority, min_pri, max_pri);
        if (ret != 0) throw uhd::os_error("error in pthread_setschedparam");

#if defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__) || defined(__FreeBSD__)
        if (realtime)
        {
            struct task_qos_policy qosinfo;
            qosinfo.task_latency_qos_tier = LATENCY_QOS_TIER_0;
            qosinfo.task_throughput_qos_tier = THROUGHPUT_QOS_TIER_0;
            ret = task_policy_set(mach_task_self(), TASK_OVERRIDE_QOS_POLICY, (task_policy_t)&qosinfo, TASK_QOS_POLICY_COUNT);
            if (ret == 0)
                fprintf(stderr, "Successfully set mach task QoS\n");
            else
                fprintf(stderr, "Failed to set mach task QoS!\n");

            struct task_category_policy tcatpolicy;
            tcatpolicy.role = TASK_FOREGROUND_APPLICATION;
            ret = task_policy_set(mach_task_self(), TASK_CATEGORY_POLICY, (thread_policy_t)&tcatpolicy, TASK_CATEGORY_POLICY_COUNT);
            if (ret == KERN_SUCCESS)
                fprintf(stderr, "Successfully set mach task category\n");
            else
                fprintf(stderr, "Failed to set mach task category!\n");
        }
        else
        {
            // FIXME: TIER_2, normal app
        }
#endif
    }
#endif /* HAVE_PTHREAD_SETSCHEDPARAM */

/***********************************************************************
 * Windows API to set priority
 **********************************************************************/
#ifdef HAVE_WIN_SETTHREADPRIORITY
    #include <windows.h>

    void uhd::set_thread_priority(float priority, UHD_UNUSED(bool realtime)){
        check_priority_range(priority);

        /*
         * Process wide priority is no longer set.
         * This is the responsibility of the application.
        //set the priority class on the process
        int pri_class = (realtime)? REALTIME_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS;
        if (SetPriorityClass(GetCurrentProcess(), pri_class) == 0)
            throw uhd::os_error("error in SetPriorityClass");
         */

        //scale the priority value to the constants
        int priorities[] = {
            THREAD_PRIORITY_IDLE, THREAD_PRIORITY_LOWEST, THREAD_PRIORITY_BELOW_NORMAL, THREAD_PRIORITY_NORMAL,
            THREAD_PRIORITY_ABOVE_NORMAL, THREAD_PRIORITY_HIGHEST, THREAD_PRIORITY_TIME_CRITICAL
        };
        size_t pri_index = size_t((priority+1.0)*6/2.0); // -1 -> 0, +1 -> 6

        //set the thread priority on the thread
        if (SetThreadPriority(GetCurrentThread(), priorities[pri_index]) == 0)
            throw uhd::os_error("error in SetThreadPriority");
    }
#endif /* HAVE_WIN_SETTHREADPRIORITY */

/***********************************************************************
 * Unimplemented API to set priority
 **********************************************************************/
#ifdef HAVE_THREAD_PRIO_DUMMY
    void uhd::set_thread_priority(float, bool){
        throw uhd::not_implemented_error("set thread priority not implemented");
    }

#endif /* HAVE_THREAD_PRIO_DUMMY */
