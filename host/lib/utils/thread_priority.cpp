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
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
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
        policy = (realtime)? /*SCHED_RR*/SCHED_FIFO : SCHED_OTHER; // FIFO is better under OS X

        //we cannot have below normal priority, set to zero
        if (priority < 0) priority = 0;

        //get the priority bounds for the selected policy
        int min_pri = sched_get_priority_min(policy);
        int max_pri = sched_get_priority_max(policy);
        if (min_pri == -1 or max_pri == -1) throw uhd::os_error("error in sched_get_priority_min/max");

        fprintf(stderr, "Policy: %d, Min pri: %d, Max pri: %d\n", policy, min_pri, max_pri);

        int ret = 0;
        mach_msg_type_number_t count = THREAD_PRECEDENCE_POLICY_COUNT;
        boolean_t get_default = 0;

        struct thread_precedence_policy precedence;
        memset(&precedence, 0x00, sizeof(precedence));

        ret = thread_policy_get(pthread_mach_thread_np(pthread_self()), THREAD_PRECEDENCE_POLICY, (thread_policy_t)&precedence, &count, &get_default);
        if (ret != KERN_SUCCESS) throw uhd::os_error("Failed to get thread policy");
        fprintf(stderr, "Precedence (%i): %u\n", ret, precedence.importance);
/*
        get_default = 1;
        count = THREAD_PRECEDENCE_POLICY_COUNT;

        ret = thread_policy_get(pthread_mach_thread_np(pthread_self()), THREAD_PRECEDENCE_POLICY, (thread_policy_t)&precedence, &count, &get_default);
        if (ret != KERN_SUCCESS) throw uhd::os_error("Failed to get thread policy");
        fprintf(stderr, "Precedence (%i): %u\n", ret, precedence.importance);
*/
        ///////////////////////////////
/*
        precedence.importance = 1;
        ret = thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_PRECEDENCE_POLICY, (thread_policy_t)&precedence, THREAD_PRECEDENCE_POLICY_COUNT);
        if (ret != KERN_SUCCESS) throw uhd::os_error("Failed to set thread policy");
*/
        ///////////////////////////////
/*
        get_default = 0;
        count = THREAD_PRECEDENCE_POLICY_COUNT;
        ret = thread_policy_get(pthread_mach_thread_np(pthread_self()), THREAD_PRECEDENCE_POLICY, (thread_policy_t)&precedence, &count, &get_default);
        if (ret != KERN_SUCCESS) throw uhd::os_error("Failed to get thread policy");
        fprintf(stderr, "Precedence (%i): %u\n", ret, precedence.importance);

        get_default = 1;
        count = THREAD_PRECEDENCE_POLICY_COUNT;
        ret = thread_policy_get(pthread_mach_thread_np(pthread_self()), THREAD_PRECEDENCE_POLICY, (thread_policy_t)&precedence, &count, &get_default);
        if (ret != KERN_SUCCESS) throw uhd::os_error("Failed to get thread policy");
        fprintf(stderr, "Precedence (%i): %u\n", ret, precedence.importance);
*/
        ///////////////////////////////

        //set the new priority and policy
        sched_param sp;
        memset(&sp, 0x00, sizeof(sp));

        int _policy = 0;
        ret = pthread_getschedparam(pthread_self(), &_policy, &sp);
        fprintf(stderr, "Old policy (%i): policy: %d, priority: %d\n", ret, _policy, sp.sched_priority);

        memset(&sp, 0x00, sizeof(sp));
        sp.sched_priority = int(priority*(max_pri - min_pri)) + min_pri;
        ret = pthread_setschedparam(pthread_self(), policy, &sp);
        fprintf(stderr, "setschedparam: priority %d [min %d, max %d]\n", sp.sched_priority, min_pri, max_pri);
        if (ret != 0) throw uhd::os_error("error in pthread_setschedparam");

        _policy = 0;
        ret = pthread_getschedparam(pthread_self(), &_policy, &sp);
        fprintf(stderr, "New policy (%i): policy: %d, priority: %d\n", ret, _policy, sp.sched_priority);

#if defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__) || defined(__FreeBSD__)

        struct task_qos_policy qosinfo;
        count = TASK_QOS_POLICY_COUNT;
        memset(&qosinfo, 0x00, sizeof(qosinfo));
        get_default = 0;
        ret = task_policy_get(mach_task_self(), TASK_OVERRIDE_QOS_POLICY, (task_policy_t)&qosinfo, &count, &get_default);
        fprintf(stderr, "QoS info (%d): latency tier: %d, throughput tier: %d\n", ret, qosinfo.task_latency_qos_tier, qosinfo.task_throughput_qos_tier);
/*
        memset(&qosinfo, 0x00, sizeof(qosinfo));
        count = TASK_QOS_POLICY_COUNT;
        get_default = 1;
        ret = task_policy_get(mach_task_self(), TASK_OVERRIDE_QOS_POLICY, (task_policy_t)&qosinfo, &count, &get_default);
        fprintf(stderr, "QoS info (%d): latency tier: %d, throughput tier: %d\n", ret, qosinfo.task_latency_qos_tier, qosinfo.task_throughput_qos_tier);
*/
        count = TASK_CATEGORY_POLICY_COUNT;
        struct task_category_policy tcatpolicy;
        memset(&tcatpolicy, 0x00, sizeof(tcatpolicy));
        get_default = 0;
        ret = task_policy_get(mach_task_self(), TASK_CATEGORY_POLICY, (task_policy_t)&tcatpolicy, &count, &get_default);
        fprintf(stderr, "Task category (%d): role: %d\n", ret, tcatpolicy.role);
/*
        count = TASK_CATEGORY_POLICY_COUNT;
        memset(&tcatpolicy, 0x00, sizeof(tcatpolicy));
        get_default = 1;
        ret = task_policy_get(mach_task_self(), TASK_CATEGORY_POLICY, (task_policy_t)&tcatpolicy, &count, &get_default);
        fprintf(stderr, "Task category (%d): role: %d\n", ret, tcatpolicy.role);
*/
        if (realtime)
        {
            struct task_qos_policy qosinfo;
            memset(&qosinfo, 0x00, sizeof(qosinfo));
            qosinfo.task_latency_qos_tier = LATENCY_QOS_TIER_0;
            qosinfo.task_throughput_qos_tier = THROUGHPUT_QOS_TIER_0;
            ret = task_policy_set(mach_task_self(), TASK_OVERRIDE_QOS_POLICY, (task_policy_t)&qosinfo, TASK_QOS_POLICY_COUNT);
            if (ret == 0)
                fprintf(stderr, "Successfully set mach task QoS\n");
            else
                fprintf(stderr, "Failed to set mach task QoS!\n");

            struct task_category_policy tcatpolicy;
            memset(&tcatpolicy, 0x00, sizeof(tcatpolicy));
            tcatpolicy.role = TASK_FOREGROUND_APPLICATION;
            ret = task_policy_set(mach_task_self(), TASK_CATEGORY_POLICY, (thread_policy_t)&tcatpolicy, TASK_CATEGORY_POLICY_COUNT);
            if (ret == KERN_SUCCESS)
                fprintf(stderr, "Successfully set mach task category\n");
            else
                fprintf(stderr, "Failed to set mach task category!\n");

            // const int HZ = 100;
            /*
                50 Msps
                2044 * 4 = 8176
                24461.83953033268102 packets / sec
                0.00004088 s / packet
                40880 ns / packet
            */
/*            struct mach_timebase_info mti; // Returns Mach time for one nano-second
            memset(&mti, 0x00, sizeof(mti));
            ret = mach_timebase_info(&mti);
            fprintf(stderr, "Timebase (%d): %u / %u\n", ret, mti.numer, mti.denom);

            double c = 40880.0 * (double)mti.denom / (double)mti.numer;
            fprintf(stderr, "Computation: %f (%u)\n", c, (uint32_t)c);

            struct thread_time_constraint_policy ttcpolicy;
            memset(&ttcpolicy, 0x00, sizeof(ttcpolicy));

            // ttcpolicy.period      = 0; // HZ/160
            // ttcpolicy.computation = (uint32_t)c; // HZ/3300;
            // ttcpolicy.constraint  = ttcpolicy.computation; // HZ/2200;
            // ttcpolicy.preemptible = 1*0;

            // const uint32_t HZ = 133000000;
            // ttcpolicy.period      = HZ/160;
            // ttcpolicy.computation = HZ/3300;
            // ttcpolicy.constraint  = HZ/2200;
            // ttcpolicy.preemptible = 1;

            ttcpolicy.period      = 50 * 1000;
            ttcpolicy.computation = 50 * 1000;
            ttcpolicy.constraint  = 50 * 1000;
            ttcpolicy.preemptible = 0;

            ret = thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&ttcpolicy, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
            if (ret == KERN_SUCCESS)
                fprintf(stderr, "Successfully set real time category\n");
            else
                fprintf(stderr, "Failed to set real time category!\n");
*/        }
        else
        {
            // FIXME: TIER_2, normal app
        }

        count = TASK_QOS_POLICY_COUNT;
        memset(&qosinfo, 0x00, sizeof(qosinfo));
        get_default = 0;
        ret = task_policy_get(mach_task_self(), TASK_OVERRIDE_QOS_POLICY, (task_policy_t)&qosinfo, &count, &get_default);
        fprintf(stderr, "QoS info (%d): latency tier: %d, throughput tier: %d\n", ret, qosinfo.task_latency_qos_tier, qosinfo.task_throughput_qos_tier);
/*
        memset(&qosinfo, 0x00, sizeof(qosinfo));
        count = TASK_QOS_POLICY_COUNT;
        get_default = 1;
        ret = task_policy_get(mach_task_self(), TASK_OVERRIDE_QOS_POLICY, (task_policy_t)&qosinfo, &count, &get_default);
        fprintf(stderr, "QoS info (%d): latency tier: %d, throughput tier: %d\n", ret, qosinfo.task_latency_qos_tier, qosinfo.task_throughput_qos_tier);
*/
        count = TASK_CATEGORY_POLICY_COUNT;
        memset(&tcatpolicy, 0x00, sizeof(tcatpolicy));
        get_default = 0;
        ret = task_policy_get(mach_task_self(), TASK_CATEGORY_POLICY, (task_policy_t)&tcatpolicy, &count, &get_default);
        fprintf(stderr, "Task category (%d): role: %d\n", ret, tcatpolicy.role);
/*
        count = TASK_CATEGORY_POLICY_COUNT;
        memset(&tcatpolicy, 0x00, sizeof(tcatpolicy));
        get_default = 1;
        ret = task_policy_get(mach_task_self(), TASK_CATEGORY_POLICY, (task_policy_t)&tcatpolicy, &count, &get_default);
        fprintf(stderr, "Task category (%d): role: %d\n", ret, tcatpolicy.role);
*/
        _policy = 0;
        ret = pthread_getschedparam(pthread_self(), &_policy, &sp);
        fprintf(stderr, "New policy (%i): policy: %d, priority: %d\n", ret, _policy, sp.sched_priority);
/*
        precedence.importance = 1 << 16;
        ret = thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_PRECEDENCE_POLICY, (thread_policy_t)&precedence, THREAD_PRECEDENCE_POLICY_COUNT);
        if (ret != KERN_SUCCESS) throw uhd::os_error("Failed to set thread policy");
*/
         get_default = 0;
        count = THREAD_PRECEDENCE_POLICY_COUNT;
        ret = thread_policy_get(pthread_mach_thread_np(pthread_self()), THREAD_PRECEDENCE_POLICY, (thread_policy_t)&precedence, &count, &get_default);
        if (ret != KERN_SUCCESS) throw uhd::os_error("Failed to get thread policy");
        fprintf(stderr, "Precedence (%i): %u\n", ret, precedence.importance);
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
