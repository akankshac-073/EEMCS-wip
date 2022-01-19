#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "header.h"

// ---------------------------
// SLACK CALCULATION FUNCTIONS
// ---------------------------

// Copies all non-DISCARDED jobs present in the run queue to a dummy queue

void copy_jobs_to_dummy_queue (RQ_HEAD *head, RQ_HEAD *dummy_head, int threshold_criticality, int level) {

    RQ_NODE *temp;    // Temporary node variable

    // Traverse the entire run queue
    temp = head->head_node;
    while (temp != NULL) {
    
        // Copy all non-DISCARDED jobs from run queue to dummy queue
        // * NOTE: This check is required when the slack calculation is being done for criticality levels > current criticality level of the system 
        if (temp->job->job_criticality >= accept_above_criticality_level (level, threshold_criticality))
            update_run_queue (dummy_head, temp->job);
        temp = temp->next;
    }
}

// Anticipates jobs arriving before the specified max_arrival_time and adds them to the dummy queue in EDF order

void add_anticipated_arrivals (RQ_HEAD *dummy_head, double max_arrival_time, Tasks *task_ptr, int num_tasks, int threshold_criticality, int level, int core_no, int current_time) {

    double next_arrival = 0;    // Time-instant at which the next job arrives

    // Adding anticipated non-DISCARDED job arrivals: arrivals starting from current_time till max_arrival_time
    
    // For all tasks 
    for (int i = 0 ; i < num_tasks ; i++) {

        // Check if the task belongs to the given core and is a non-DISCARDED job at the specified criticality level 
        if (task_ptr[i].allocated_core == core_no && task_ptr[i].criticality >= accept_above_criticality_level (level, threshold_criticality)) {

            // Anticipate next job arrival for this task
            next_arrival = get_next_job_arrival (task_ptr, i, current_time); 

            // While the job arrival times < maximum arrival time specified 
            // TODO: Verify that it is strictly less than and not less than or equal to
            while (next_arrival < max_arrival_time) {

                // Allocate memory for a new job structure 
                Jobs *j;
                j = malloc (sizeof (Jobs));

                // Create a new job structure and set the job parameter values
                j = create_job_structure (task_ptr, i, threshold_criticality, core_no, next_arrival);

                // print_run_queue (dummy_head);

                // Add the job to dummy queue
                update_run_queue (dummy_head, j);

                // print_run_queue (dummy_head);

                // Anticipate next arrivals by adding task period
                next_arrival = next_arrival + task_ptr[i].period;               
            }
        }
    }
}

// Slack calculation (using Dynamic Procrastination): 
// Slack = (latest time by which run queue jobs must start executing in order to guarantee completion by deadline) - (window time consumed by the anticipated jobs)

double calculate_slack_available (RQ_HEAD *dummy_head, double latest_arrival, double max_deadline, int current_time, int level) {

    RQ_NODE *temp;                                // Temporary node pointer to traverse through the dummy queue 
    RQ_NODE *temp1;                               // Temporary node pointer to hold temp->prev when deleting job at temp
    double latest_start_time = max_deadline;      // Latest point in time to which we can procrastinate job execution
    double window_time_consumed = 0.0;            // The window time reserved for other jobs to execute
                                                  // Window: {current_time, Discarded job deadline}
    double slack_available = 0.0;                 // Slack time obtained by procrastinating jobs in the run queue

    // Find the tail node of dummy queue
    temp = dummy_head->head_node;
    if (temp != NULL) {
        while (temp->next != NULL) 
            temp = temp->next;
    } 

    // Traverse the dummy queue from tail node all the way back to the head node
    while (temp != NULL) {

        // Case 1: Jobs arriving after latest arrival time having deadlines > max deadline --> need to partially execute by max deadline
        if (temp->job->sched_deadline > max_deadline) { 
            latest_start_time = latest_start_time - (double)((max_deadline - temp->job->arrival_time) * temp->job->wcet_budget [level - 1]) /(double)(temp->job->sched_deadline - temp->job->arrival_time);
            
            // Remove this job from dummy queue
            temp1 = temp->prev;
            delete_job_from_queue (dummy_head, temp->job);
        }
     
        // Case 2: Jobs (arriving before or after latest arrival time) with deadlines (di) such that: latest arrival time < di < max deadline --> need to execute completely
        else if (temp->job->sched_deadline > latest_arrival && temp->job->sched_deadline <= max_deadline) {
            
            // If the latest start time exceeds job deadline, reset the latest start time to job deadline
            if (latest_start_time > temp->job->sched_deadline)
                latest_start_time = temp->job->sched_deadline;

            // If the job has not yet arrived, reserve wcet at given level
            if (temp->job->arrival_time > current_time)
                latest_start_time = latest_start_time - temp->job->wcet_budget [level - 1];
            
            // If the job has already arrived, reserve time for remaining execution time 
            else 
                latest_start_time = latest_start_time - temp->job->execution_time;

            // Remove this job from dummy queue
            temp1 = temp->prev;
            delete_job_from_queue (dummy_head, temp->job);
        }

        // Case 3: Jobs with deadlines < latest arrival time --> need to execute completely, taking up time from the discarded job's execution window

        else if (temp->job->sched_deadline <= latest_arrival) {
        
            // If the job has not yet arrived, reserve wcet at given level 
            if (temp->job->arrival_time > current_time)
                window_time_consumed = window_time_consumed + temp->job->wcet_budget [level - 1]; 
                
            // If the job has already arrived, reserve time for remaining execution 
            else
                window_time_consumed = window_time_consumed + temp->job->execution_time;
                
            // Remove this job from dummy queue
            temp1 = temp->prev;
            delete_job_from_queue (dummy_head, temp->job);
        }  
        
        // Move to the previous node (temp = temp->prev)  
        temp = temp1;
    }
    
    // Calculate the slack available

    // If latest start time is >= discarded job deadline, just subtract window time consumed
    if (latest_start_time >= latest_arrival)
        slack_available = (latest_arrival - current_time) - window_time_consumed;
        
    // Else update window to [current_time, latest start time), and then subtract window time consumed
    else 
        slack_available = (latest_start_time - current_time) - window_time_consumed;

    // Slack available to execute the given job
    return slack_available;
}

// --------------------------------------------------
// DYNAMIC PROCRASTINATOR TO CALCULATE SHUTDOWN TIME
// --------------------------------------------------

void get_dynamic_procrastination_slack (Cores *core, int core_idx, Tasks *task_arr, int num_tasks, double next_job_deadline, int max_criticality, int current_level, int hyperperiod, double current_time) {

    double max_deadline[max_criticality - current_level + 1];    // Maximum deadline among all jobs arriving before latest_arrival
    RQ_NODE *temp;                                               // Temporary node to traverse through the dummy queue

    // Create dummy queues (for each criticality level >= current level)
    // Maintains all (already arrived + anticipated arrivals) jobs at given criticality level in EDF order
    RQ_HEAD *dummy_head[max_criticality - current_level + 1];
    for (int i = 0; i < (max_criticality - current_level + 1); i++)
        dummy_head[i] = create_run_queue ();
    
    // For all criticality levels >= current level
    for (int i = 0; i < (max_criticality - current_level + 1); i++) {
         
        // Add all jobs arriving before next arrival to dummy queue in EDF order
        copy_jobs_to_dummy_queue (core[core_idx].qhead, dummy_head[i], core[core_idx].threshold_criticality, current_level + i);
        add_anticipated_arrivals (dummy_head[i], next_job_deadline, task_arr, num_tasks, core[core_idx].threshold_criticality, current_level + i , core[core_idx].core_no, current_time);

        // Get maximum deadline among all dummy queue jobs 
        temp = dummy_head[i]->head_node;
        if (temp != NULL) {
            while (temp->next != NULL) 
                temp = temp->next;
            max_deadline[i] = temp->job->sched_deadline;
        }
        else 
            max_deadline[i] = hyperperiod;
            
        if (max_deadline[i] > hyperperiod)
            max_deadline[i] = hyperperiod;

        // Add anticipated all non-DISCARDED job arrivals (such that latest_arrival <= job arrival < max deadline at to the dummy queue in EDF order
        add_anticipated_arrivals (dummy_head[i], max_deadline[i], task_arr, num_tasks, core[core_idx].threshold_criticality, current_level + i, core[core_idx].core_no, next_job_deadline -  TIME_GRANULARITY);

        // Calculate the slack obtained by dynamically procrastinating jobs
        core[core_idx].slack_available[i] = calculate_slack_available (dummy_head[i], next_job_deadline, max_deadline[i], current_time, current_level + i); 
        
    }
}

// -----------------------
// DISCARDED JOB SCHEDULER
// -----------------------

// Schedules discarded job if enough slack is available for it to execute

void schedule_discarded_job (RQ_HEAD *head, RQ_HEAD **dhead, Tasks *task_ptr, int num_tasks, int threshold_criticality, int max_criticality, int current_level, int core_no, int hyperperiod, double current_time) {
    
    // Arrays to store parameter values required for slack calculation at different criticality levels (>= current level)

    double max_deadline[max_criticality - current_level + 1];              // Maximum deadline among all jobs arriving before the discarded job's deadline
    double slack_available[max_criticality - current_level + 1];           // Slack available for discarded job execution 
                                                                           // (as determined by proposed algorithm)
    double optimal_slack[max_criticality - current_level + 1];             // Optimal slack available for discarded job execution 
                                                                           // (as determined by anticipating all job arrivals till hyperperiod)
    double expected_completion_time[max_criticality - current_level + 1];  // If discarded job is scheduled, the expected time by which it will complete
    
    int next_arrival = 0;                                                  // Temporary variable to store next arrival times
    int i = 0;                                                             // Index to traverse through discarded queue heads (for different criticality levels) 
    int temp_count = 0;                                                    // Temporary count variable -- used to check if slack is available at all criticality levels
    RQ_NODE *temp;                                                         // Temporary variable to traverse through discarded queues
    
    // Discarded job struct
    Jobs *discarded_job;
    discarded_job = malloc (sizeof (Jobs));
    discarded_job->task_no = IDLE_TASK_NO;

    // Create dummy queues (for each criticality level >= current level) -- required for slack calculation 
    // Maintains all non-DISCARDED (already arrived + anticipated arrivals) jobs at given criticality level in EDF order
    RQ_HEAD *dummy_head[max_criticality - current_level + 1];
    for (i = 0; i < (max_criticality - current_level + 1); i++)
        dummy_head[i] = create_run_queue ();

    // Delete all jobs that are going to exceed/have already exceeded their deadlines

    // For all discarded job queues
    for (i = 0; i < current_level - 1; i++) {
        temp = dhead[i]->head_node;
        while (temp != NULL) {
        
            // Delete all jobs satisfying the condition (deadline + wcet [current_level]) < current_time
            if ((temp->job->sched_deadline - temp->job->wcet_budget[current_level - 1]) < current_time) 
                delete_job_from_queue (dhead[i], temp->job);
            temp = temp->next; 
        }
    }
 
    // Consider the highest criticality non-empty discarded queue for scheduling 
    for (i = current_level - 2; i >= 0 ; i--) { 
        if (dhead[i]->head_node != NULL)
            break;
    }
   
    // For the highest criticality non-empty discarded queue (if it exists)
    if (i >= 0) {

        // Schedule discarded job from the given queue
        while (dhead[i]->head_node != NULL) {
        
            // Pick the first (earliest deadline) job from discarded queue 
            discarded_job = schedule_next_job (dhead[i]);         // *** NOTE - schedule_next_job (head) is a misnomer, this fn only dequeues the first job in the queue, does NOT schedule it
            
            // For all criticality levels >= current level
            // Calculate slack, if job slack > discarded_job wcet for all levels ---> add to given core's run queue
            for (int ii = 0; ii < (max_criticality - current_level + 1); ii++) {

                // Add all jobs arriving before discarded job deadline to dummy queue in EDF order for slack calculation
                copy_jobs_to_dummy_queue (head, dummy_head[ii], threshold_criticality, current_level + ii);
                add_anticipated_arrivals (dummy_head[ii], discarded_job->sched_deadline, task_ptr, num_tasks, threshold_criticality, current_level + ii , core_no, current_time);
            
                // Get maximum deadline 
                temp = dummy_head[ii]->head_node;
                if (temp != NULL) {
                    while (temp->next != NULL)
                        temp = temp->next;
                    max_deadline[ii] = temp->job->sched_deadline;
                }
                
                // TODO: Confirm the slack calculation for this case
                // When no jobs present in dummy queue -- calculate optimal slack
                else {
                    max_deadline[ii] = hyperperiod;
                }
 
                if (max_deadline[ii] > hyperperiod)
                    max_deadline[ii] = hyperperiod;

                // Add anticipated all non-DISCARDED job arrivals (such that job arrival >= discarded job deadline) at to the dummy queue in EDF order
                add_anticipated_arrivals (dummy_head[ii], max_deadline[ii], task_ptr, num_tasks, threshold_criticality, current_level + ii, core_no, discarded_job->sched_deadline -  TIME_GRANULARITY);

                // Calculate the slack available for execution of discarded job at given level
                slack_available[ii] = calculate_slack_available (dummy_head[ii], discarded_job->sched_deadline, max_deadline[ii], current_time, current_level + ii);

                // Calculate the optimal slack available for execution of discarded job at given level
                // (Optimal slack is calculated by reserving execution times for all jobs arriving till hyperperiod)
                copy_jobs_to_dummy_queue (head, dummy_head[ii], threshold_criticality, current_level + ii);
                add_anticipated_arrivals (dummy_head[ii], hyperperiod, task_ptr, num_tasks, threshold_criticality, current_level + ii, core_no, current_time);
                optimal_slack[ii] = calculate_slack_available (dummy_head[ii], discarded_job->sched_deadline, hyperperiod, current_time, current_level + ii);

                printf("\n Slack calculated: %lf\t Optimal slack: %lf for discarded job (Task %d Job %d) at level %d in core %d\n", slack_available [ii], optimal_slack [ii], discarded_job->task_no, discarded_job->job_no, current_level + ii, core_no);

                // Ensure that scheduling the discarded job in consideration does not delay the completion of any higher criticality discarded job 
                // arriving in near future (that can be scheduled in the available slack time) 

                // 1. Get expected time of completion of the discarded job to be scheduled

                temp = head->head_node;
                expected_completion_time[ii] = current_time;   // Initialize expected completion time to current current_time
                
                // Traverse through the core's local run queue                
                while (temp != NULL) {
                
                    // Consider all run queue jobs with deadlines <= discarded job deadlines
                    if (temp->job->sched_deadline > discarded_job->sched_deadline) 
                        break;
                    
                    // Expected time of completion = (current_time + summation of wcets of all such jobs) 
                    // (No need to anticipate special cases - can be optimistic at the time of discarded job scheduling)   
                    expected_completion_time[ii] = expected_completion_time[ii] + temp->job->wcet_budget[current_level + ii - 1]; 
                    temp=temp->next;                                                                                  
                }
              
                // 2. Anticipating higher criticality discarded job arrivals

                // For all tasks
                for (int j = 0; j < num_tasks; j++) {
                
                    // If the task criticality > discarded job criticality level and < current level
                    if (task_ptr[j].criticality < current_level && task_ptr[j].criticality > i + 1) {

                        // Anticipate next job arrival
                        next_arrival = get_next_job_arrival (task_ptr, i, current_time);
                        
                        // If  job arrival time < expected time of completion for discarded job, subtract its wcet from slack available
                        if (next_arrival < expected_completion_time[ii]) 
                            slack_available[ii] = slack_available[ii] - task_ptr[j].wcet[current_level + ii - 1];
                    }
                }
            }

            // Temp_count incremented for each criticality level in which enough slack is available for discarded job to execute
            for (int ii = 0; ii < max_criticality - current_level + 1; ii++) {
                if (slack_available[ii] >= discarded_job->wcet_budget[(discarded_job->job_criticality) - 1]) 
                    temp_count++;
            }

            // If slack available in all criticality levels > discarded job wcet, add it to run queue and break; 
            if (temp_count == max_criticality - current_level + 1) {
                discarded_job->allocated_core = core_no;
                // print_run_queue(head);
                update_run_queue (head, discarded_job);
                printf(" Enough slack available. Scheduling the discarded job!\n\n");
                // print_run_queue(head);
                // break;
            }
            
            // Else remove from discarded queue (already done by schedule_next_job), consider next discarded job for scheduling

            // If all jobs from current queue are discarded due to not enough slack available (i.e. current queue empty)  
            if (dhead[i]->head_node == NULL && discarded_job->task_no == IDLE_TASK_NO) {
                
                // Move on to next (lower criticlality) non-empty discarded queue
                while (dhead[i]->head_node == NULL && i >= 0) 
                    i--;
                
                // If all queues are empty, break out of the loop
                if (i < 0) 
                    break;        
            }
        }
    }
}

