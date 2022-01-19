// TODO: Job migration to maximize shutdown time, DVFS, Load balancing for discarded job scheduling

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "header.h"

int current_level = 1;

// -----------------------------
// SUPER-HYPERPERIOD CALCULATION
// -----------------------------

// Helper function to calculate HCF (Highest Common Factor) of 2 given numbers

int hcf (int n1, int n2) {
    if (n2 == 0)
        return n1;
    else
        return hcf (n2, (n1 % n2));
}

// Calculate superhyperperiod of the entire input task set

int calculate_superhyperperiod (Tasks *task_arr, int num_tasks) {
    int i = 0;                      // Index to traverse through task structure array
    int superhyperperiod = 1;       // LCM of all the task periods 
    
    // For all tasks
    for (i = 0 ; i < num_tasks ; i++) {
    
        // Calculate superhyperperiod or cumulative LCM for each new task period as
        // LCM (T, t) = (LCM(T) * next task (t) period) / HCF (T, t) 
        superhyperperiod = superhyperperiod * task_arr[i].period / hcf (superhyperperiod, task_arr[i].period);
    }
    
    // Return superhyperperiod of the given taskset
    return superhyperperiod;
}

// ---------------------------------------------------------
// MERGE SORT RUN QUEUE IN INCREASING ORDER OF JOB DEADLINES
// ---------------------------------------------------------

// Partition the queue to be sorted into smaller queues (approx. halves)
 
RQ_NODE *partition (RQ_NODE *head) { 
    
    // Initialize fast and slow pointers to head node pointer
    RQ_NODE *fast = head;
    RQ_NODE *slow = head;
    
    // Fast pointer traverses through the queue 2 nodes at a time
    // Slow pointer traverses through the queue 1 node at a time 
    while ((fast->next != NULL) && (fast->next->next != NULL)) { 
        fast = fast->next->next; 
        slow = slow->next; 
    }
    
    // When fast pointer reaches the end, slow pointer reaches the middle
    // Store slow pointer value in temp  
    RQ_NODE *temp = slow->next; 
    
    // First half of queue
    slow->next = NULL; 
    
    // Return pointer to second half of the queue
    return temp; 
} 

// Merge the smaller queues in order of decreasing job deadlines
 
RQ_NODE *merge (RQ_NODE *first, RQ_NODE *second) {

    // If the first queue is empty, just return pointer to second queue   
    if (first == NULL) 
        return second; 
        
    // If the second queue is empty, just return pointer to first queue     
    if (second == NULL) 
        return first; 
        
    // Merge queue nodes in order of their job deadline fields
    if (first->job->sched_deadline < second->job->sched_deadline) { 
        first->next = merge (first->next, second); 
        first->next->prev = first; 
        first->prev = NULL; 
        return first; 
    } 
    else { 
        second->next = merge (first, second->next); 
        second->next->prev = second; 
        second->prev = NULL; 
        return second; 
    } 
}

// Rearrange queue wrt updated job deadlines - (Merge sort driver function)

RQ_NODE *merge_sort (RQ_NODE *primary_head) {

    // If queue is empty/contains only one node, return head node pointer as it is 
    if ((primary_head == NULL) || (primary_head->next == NULL)) 
        return primary_head; 
        
    // Else, partition the given queue into smaller (half) queues
    RQ_NODE *secondary_head = partition (primary_head);  
    
    // Recursively partition and merge individual nodes to get sorted queue
    primary_head = merge_sort (primary_head);              
    secondary_head = merge_sort (secondary_head); 
    
    // Return pointer to sorted queue head (via merge function)
    return merge (primary_head, secondary_head); 
} 

// -----------------------------
// RUN-TIME SCHEDULING FUNCTIONS
// -----------------------------

// Create an EMPTY run queue 

RQ_HEAD *create_run_queue () {

    // Allocate memory for the run queue head struct
    RQ_HEAD *head;
    head = (RQ_HEAD *) malloc (sizeof (RQ_HEAD));
    
    // Initialize queue parameters
    head->size = 0;                // Initialize run queue size to 0
    head->parameter = -1;          // Initialize the maximum deadline for empty queue to -1
    head->head_node = NULL;        // Initialize head node pointer as NULL
    
    // Return pointer to run queue head
    return head;
}

// Determine the threshold criticality level, all tasks with criticality level below this must be DISCARDED

int accept_above_criticality_level (int level, int threshold_criticality) {    

    // If current criticality <= EDF-VD threshold, all tasks below current criticality must be DISCARDED
    if (level <= threshold_criticality)
        return level; 
        
    // Else, all tasks below the EDF-VD threshold criticality must be DISCARDED
    else 
        return (threshold_criticality + 1);
}

// Determine the next job arrival instance in each task set and returns the (minimum) arrival time of the next job

double get_next_job_arrival (Tasks *task_arr, int task_array_idx, double timecount) {

    int next_job_instance = 0;       // Next job instance number (Job instance 0 corresponds to first arrival)
    double next_arrival = 0;         // Next job arrival time for given task
    // double modulo_result = 0.0;      // To store the return value of find_modulo function

    // If the next timecount value is greater than task phase 
    if (timecount + TIME_GRANULARITY - task_arr[task_array_idx].phase > 0) {

        // modulo_result = find_modulo ((timecount + TIME_GRANULARITY - task_arr[task_array_idx].phase), task_arr[task_array_idx].period);

        // Calculate the next job instance as ceil (new timecount/period)
        next_job_instance = (int)(ceil((timecount + TIME_GRANULARITY - task_arr[task_array_idx].phase) / task_arr[task_array_idx].period));
        // if (modulo_result != 0.0)
        //     next_job_instance = ((int)((timecount + TIME_GRANULARITY - task_arr[task_array_idx].phase)/ task_arr[task_array_idx].period)) + 1;
        // else
        //     next_job_instance = (int)((timecount + TIME_GRANULARITY - task_arr[task_array_idx].phase) / task_arr[task_array_idx].period);
    }

    // Else, next job instance will be the first job of that task
    else
        next_job_instance = 0; 

    // Calculate job arrival time as (task phase + (job instance * task period))
    next_arrival = task_arr[task_array_idx].phase + next_job_instance * task_arr[task_array_idx].period;
    
    // Return next job arrival time
    return next_arrival;
}

// Determine the next scheduling decision point = min {next decision points in all cores}
// Decision points: 1. Arrival 2. Current job termination 3. Criticality level change due to wcet budget overrun at current level 4. Overrun 5. Core Wakeup
// Set preferences within decision point events

double get_next_decision_point (Cores *core, int num_cores, Tasks *task_arr, int num_tasks, double timecount, int hyperperiod) {

    double next_arrival = 0.0;                      // Next job arrival time for given task
    double min_arrival = 0.0;                       // Minimum of next job arrival times among all tasks
    double job_termination = 0.0;                   // Currently executing job's termination time
    double criticality_level_change = 0.0;          // Time at which criticality level change is triggered 
                                                    // (in case currently executing job exceeds its wcet budget) 
    double next_decision_point = hyperperiod;       // = min {next decision points in all cores}
    int i = 0;                                      // Index to traverse through the task structure array
    int j = 0;                                      // Index to traverse through the core structure array
    
    // For all cores
    for (j = 0; j < num_cores; j++) {
    
        // Case 1: Job arrival 
        min_arrival = hyperperiod; 

        // For all tasks that belong to the given core 
        for (i = 0 ; i < num_tasks ; i++) {
            if (task_arr[i].allocated_core == core[j].core_no) {
        
                // Determine next job arrival time
                next_arrival = get_next_job_arrival (task_arr, i, timecount);
            
                // Update minimum arrival time if the next arrival time obtained in this iteration is smaller
                if (min_arrival > next_arrival)
                    min_arrival = next_arrival;
            }
        }
    
        // Set next decision point as minimum of next job arrival times for all tasks
        core[j].decision_point->decision_time = min_arrival;
        core[j].decision_point->event = JOB_ARRIVAL;
        
        // For ACTIVE cores    
        if (core[j].status == ACTIVE) {

            // If the currently executing job is not IDLE
            if (core[j].curr_exe_job->task_no != IDLE_TASK_NO) {
  
                // Case 2: Job termination 
        
                // If execution time is within allocated wcet budget: calculate job termination time
                if (core[j].curr_exe_job->execution_time <= core[j].curr_exe_job->wcet_budget[current_level - 1]) {
                    job_termination = timecount + core[j].curr_exe_job->execution_time;
            
                    // Update decision point value if the job termination time < next earliest job arrival
                    if (core[j].decision_point->decision_time > job_termination) {
                        core[j].decision_point->decision_time = job_termination;
                        core[j].decision_point->event = JOB_TERMINATION;
                    }
                    
                    // Add JOB_TERMINATION to the decision point event if job termination (completion) time = decision time calculated
                    else if (core[j].decision_point->decision_time == job_termination)
                        core[j].decision_point->event = core[j].decision_point->event + JOB_TERMINATION;
                }
        
                // If execution time exceeds wcet budget: calculate criticality level change time
                else {
                    criticality_level_change = timecount + core[j].curr_exe_job->wcet_budget[current_level - 1];
            
                    // Update decision point value if criticality level change time < next earliest job arrival 
                    if (core[j].decision_point->decision_time > criticality_level_change) {
                        core[j].decision_point->decision_time = criticality_level_change;
            
                        // Case 3: Criticality level change 
                        // If currently executing job exceeds wcet budget at criticality < its own criticality level 
                        // Criticality level change is triggered in the system 
                        if (core[j].curr_exe_job->job_criticality > current_level) 
                            core[j].decision_point->event = JOB_WCET_EXCEEDED;
            
                        // Case 4: Job overrun 
                        // If currently executing job exceeds wcet budget at its highest defined criticality level
                        // It is a job overrun, the job must be aborted, no criticality change is triggered 
                        // (unless criticality level change is triggered due to job exceeding wcet in some other core)
                        else 
                            core[j].decision_point->event = JOB_OVERRUN;
                    }
                    
                    // Add JOB_WCET_EXCEEDED / JOB_OVERRUN to the decision point event if criticality level change time = decision time calculated
                    else if (core[j].decision_point->decision_time == criticality_level_change) {                                            
                        if (core[j].curr_exe_job->job_criticality > current_level) 
                            core[j].decision_point->event = core[j].decision_point->event + JOB_WCET_EXCEEDED;
                        else 
                            core[j].decision_point->event = core[j].decision_point->event + JOB_OVERRUN;
                    }
                }
            }
        }
        
        else {
        
            // Case 5: Core wakeup -- for shutdown cores 
            if (core[j].decision_point->decision_time > core[j].wakeup_time) {
                core[j].decision_point->decision_time = core[j].wakeup_time;
                core[j].decision_point->event = WAKEUP_CORE;
            }
            
            // Add WAKEUP_CORE to the decision point event if core wakeup time = previously calculated decision point 
            else if (core[j].decision_point->decision_time == core[j].wakeup_time)
                core[j].decision_point->event = core[j].decision_point->event + WAKEUP_CORE;
        }
     
        // Update the next decision point min {next decision points in all cores}   
        if (next_decision_point > core[j].decision_point->decision_time)
            next_decision_point = core[j].decision_point->decision_time;
    }

    // Return next decision point for the scheduler
    return next_decision_point;
}

// Create job structure and set the parmeter values 

Jobs *create_job_structure (Tasks *task_arr, int task_array_idx, int threshold_criticality, int core_no, double timecount) {

    // Allocating memory for job structure
    Jobs *job;
    job = malloc (sizeof (Jobs));

    // Assigning job parameter values
    
    // Job number gives the current job instance number - numbering starts from 0
    job->job_no = (timecount - task_arr[task_array_idx].phase) / task_arr[task_array_idx].period;
    
    // Task number indicates which task this job belongs to
    job->task_no = task_arr[task_array_idx].task_no;
    
    // A job's allocated core is initialized to the task's allocated core
    // Job migration may happen at runtime for load-balancing/maximizing shutdown time 
    job->allocated_core = core_no;
    
    // Job's arrival time is calculated as (task phase + (job instance * task period))
    job->arrival_time = task_arr[task_array_idx].phase + (job->job_no * task_arr[task_array_idx].period); 
    
    // Job criticality is same as the corresponding task criticality
    job->job_criticality = task_arr[task_array_idx].criticality;
    
    // Job status flag is initialized to READY
    job->status_flag = READY;

    // Wcet budgets of the job at each criticality are determined by task wcet
    for (int i = 0; i < MAX_LEVELS; i++) {
        if (i < job->job_criticality)
            job->wcet_budget[i] = task_arr[task_array_idx].wcet[i];
            
        // At criticality level higher than job criticality, wcet budget is considered at highest criticality
        // (Required to determine if the DISCARDED job can be scheduled in available slack)
        else
            job->wcet_budget[i]= task_arr[task_array_idx].wcet[job->job_criticality - 1];
    }

    // Sched_deadline: deadline (virtual/actual) that decides scheduling order
    // Virtual deadlines are considered if the system criticality is below EDF-VD threshold 
    if (current_level <= threshold_criticality)                        
        job->sched_deadline = job->arrival_time + task_arr[task_array_idx].virtual_deadline;
    
    // Else, original deadlines are considered 
    else  
        job->sched_deadline = job->arrival_time + task_arr[task_array_idx].deadline; 

    // Random values generated for actual execution times     
    // TODO: Modify to include a probabilistic random number generation i.e. exection time exceeds wcet with prob p 
    job->execution_time = (rand() % (task_arr[task_array_idx].wcet[(task_arr[task_array_idx].criticality) - 1])) + 1;  
    
    // Return job structure pointer
    return job;
}

// Run queue is updated by inserting all the ready jobs in the queue while maintaining the EDF order

void update_run_queue (RQ_HEAD *head, Jobs *j) { 

    int pos = 0, inserted = 0;
    RQ_NODE *temp, *add_node;
    add_node = (RQ_NODE *) malloc (sizeof (RQ_NODE));
    add_node->job = j;

    head->size = head->size + 1;
    temp = head->head_node;

    // If the queue is not empty
    if (temp != NULL) {

        while (temp != NULL) {
            if (temp->job->sched_deadline >= j->sched_deadline) {

                // Insert job at the head of a non-empty queue
                if (pos == 0) {
                    add_node->next = head->head_node;
                    add_node->prev = NULL;
                    head->head_node->prev = add_node;
                    head->head_node = add_node;
                    inserted = 1; break;
                }

                // Insert job somewhere in the middle of a non-empty queue
                else {
                    temp = temp->prev;
                    add_node->next = temp->next;
                    temp->next->prev = add_node;
                    temp->next = add_node;
                    add_node->prev = temp;
                    inserted = 1; break;                    
                }
             }        

             // Update temp
             temp = temp->next; pos++;
        }

        // Insert job at tail        
        if (temp == NULL && inserted == 0) {                             
            add_node->next = NULL;
	    temp = head->head_node;
	    while (temp->next != NULL)
	        temp = temp->next;
	    temp->next = add_node;
	    add_node->prev = temp;
        }
    }

    // If the queue is empty: insert job at head 
    else {                                                  
        add_node->next = head->head_node;
        add_node->prev = NULL;
        head->head_node = add_node;
        inserted = 1;
    }
}

// Create job stuctures for all READY jobs
// Add the jobs to run queue if core is ACTIVE; add the job to pending request queue if core is SHUTDOWN

void add_ready_jobs (Cores *core, RQ_HEAD **dhead, RQ_HEAD *prhead, Tasks *task_arr, int num_tasks, double timecount) {

    int accept_above_criticality_rval = 0;       // Temporary variable to store the "accept above" criticality level value  
                                                 // All jobs with criticality > accept_above_criticality_level will be added to respective core's run queue
                                                 // Else, added to discarded queue corresponding to the job's criticality level
    double modulo_result = 0;                    // To store return value of find modulo function
     
    // For all tasks
    for (int i = 0 ; i < num_tasks ; i++) {
  
        // Add ready jobs only for the core in consideration   
        if (task_arr[i].allocated_core == core->core_no) {

            modulo_result = find_modulo ((timecount - task_arr[i].phase), task_arr[i].period);
            
            // If the job arrival condition is satisfied 
            if (timecount - task_arr[i].phase >= 0 && !(modulo_result)) {   

                // Allocate memory for a new job structure
                Jobs *job;
                job = malloc (sizeof (Jobs));

                // Create a new job structure and set the job parameter values
                job = create_job_structure (task_arr, i, core->threshold_criticality, core->core_no, timecount);

                // Add the job to run queue/discarded queue/pending request queue
                accept_above_criticality_rval = accept_above_criticality_level (current_level, core->threshold_criticality);

                // If job criticality > accept_above_criticality_level 
                if (job->job_criticality >= accept_above_criticality_rval) {

                    // If the core is ACTIVE - add job to respective core's run queue
                    if (core->status == ACTIVE) 
                        update_run_queue (core->qhead, job);

                    // If the core is SHUTDOWN - add job to the pending request queue
                    else 
                        update_run_queue (prhead, job);
                }

                // Else, add job to the discarded job queue (corresponding to it's criticality level)
                else 
                    update_run_queue (dhead [(job->job_criticality) - 1], job);                               
            }
        }
    }
}

// Schedule next job by removing a job node from head of the run queue, returning the job struct to the runtime scheduler

Jobs* schedule_next_job (RQ_HEAD *head) {

    RQ_NODE *temp;       // Temporary node variable 

    // Allocate memory for the job to be scheduled next
    Jobs *next_job;
    next_job = malloc (sizeof (Jobs));

    // If the run queue is empty 
    // Return a job structure with IDLE_TASK_NO
    if(head->head_node == NULL) {                    
        next_job->task_no = IDLE_TASK_NO;
        return next_job;
    }

    // Else, dequeue and return the job structure at the head of the queue 
    temp = head->head_node;
    head->head_node = head->head_node->next;
    if(head->head_node != NULL)
        head->head_node->prev = NULL;
    
    // Update queue size
    head->size = head->size - 1;
    
    // Copy temp job structure info to the node structure to be returned
    copy_job_structure (next_job, temp->job);
    
    // Free memory allocated to temp (delete node)
    free (temp);    
    
    // Return pointer to structure containing next job info     
    return next_job; 
}

// Delete a particular job structure from the run queue

void delete_job_from_queue (RQ_HEAD *head, Jobs *job) {

    RQ_NODE *temp, *temp1;       // Temporary node variables

    // If the queue is not empty    
    if (head->head_node != NULL) {
    
        // Update queue size 
        head->size = head->size - 1;
        
        // Initialize temp to head node
        temp = head->head_node;

        // If the job to be deleted is present in the head node of the queue
        if (head->head_node->job->task_no == job->task_no && head->head_node->job->job_no == job->job_no) {

            // Delete job from head of non-empty queue
            head->head_node = head->head_node->next;
            if (head->head_node != NULL)
                head->head_node->prev = NULL;

            // Free temp
            free (temp);
        }
   
        // Else, traverse through the entire queue 
        else {
            while (temp->next != NULL) {
            
                // If the job to be deleted is present in temp->next
                if (temp->next->job->task_no == job->task_no && temp->next->job->job_no == job->job_no) {

                    // Delete job from non-empty queue
                    temp1 = temp->next;
                    temp->next = temp->next->next;
                    if(temp->next != NULL)         
                        temp->next->prev = temp; 

                    // Free temp->next
                    free (temp1);
                    break;
                }
                temp = temp->next;  
            } 
        }
    }
}

// Scan the run queue and discards jobs below acceptable criticality level --> when criticality level/mode is upgraded

void discard_below_criticality_level (RQ_HEAD *head, RQ_HEAD **dhead, int level) {

    RQ_NODE *temp;     // Temporary node variable

    // Initialize temp to run queue head node    
    temp = head->head_node;

    // Traverse through the core's run queue
    while (temp != NULL) {
    
        // If the job criticality is less than the given level
        if (temp->job->job_criticality < level) {
        
            // Add this job to the discarded queue corresponding to its criticality level
            update_run_queue (dhead[(temp->job->job_criticality) - 1], temp->job);
            
            // Delete job from run queue
            delete_job_from_queue (head, temp->job); 
        }

        // Update temp
        temp = temp->next;
    }
}

// Get task array index corresponding to the task number specified

int get_task_array_index (Tasks *task_arr, int num_tasks, int task_no) {

    int i = 0;     // Task array index

    // For all tasks
    for (i = 0; i < num_tasks; i++) {

        // If task_no of task in array matches the given task_no
        if (task_arr[i].task_no == task_no)
            break;
    }

    // Return task array index
    return i;
}

// Update job deadlines (wrt which we are ordering the run queue) - reset to original deadlines on mode change

void update_sched_deadlines (RQ_HEAD *head, Tasks *task_arr, int num_tasks) {

    int task_array_idx = 0;     // Variable to store task array index
    RQ_NODE *temp;              // Temporary node variable

    // Initialize temp
    temp = head->head_node;

    // Update deadlines for all jobs in the queue
    while (temp != NULL) {
        task_array_idx = get_task_array_index (task_arr, num_tasks, temp->job->task_no);
        temp->job->sched_deadline = temp->job->arrival_time + task_arr[task_array_idx].deadline;
        temp = temp->next; 
    }
}

// RUN-TIME SCHEDULER LOOP

void run_scheduler_loop (Cores *core, int num_cores, Tasks *task_arr, int num_tasks, int hyperperiod, int max_criticality) {

    double timecount = -1.0 * TIME_GRANULARITY;    // Timer value 
    double next_decision_point = 0.0;              // Next scheduler decision point at any given time = min {next decision points in all cores}
    double min_arrival = hyperperiod;              // Time-instant at which the next job arrives
    double next_arrival = 0.0;                     // Time-instant at which the next job of given task arrives
    int core_idx = 0;                              // Index to traverse through core structure array
    RQ_NODE *temp;                                 // Temporary node variable
    int min_idx = 0; 
    int i = 0;
    
    // INITITIALIZE RUNTIME SCHEDULER DATA STRUCTURES

    // Create GLOBAL discarded queues (per criticality level) to store all low-criticality discarded jobs
    RQ_HEAD *dhead [max_criticality - 1];
    for (int i = 0; i < max_criticality - 1; i++)
        dhead[i] = create_run_queue();
        
    // Create a GLOBAL pending request queue to add the job arrivals of all SHUTDOWN cores
    RQ_HEAD *prhead;
    prhead = create_run_queue();

    // Initialize cores for scheduling
    for (core_idx = 0 ; core_idx < num_cores ; core_idx++) {
        core[core_idx].qhead = create_run_queue();                        // Create a LOCAL run queues for each core
        core[core_idx].curr_exe_job = malloc (sizeof (Jobs));             // Allocate memory for currently executing job structure in each core
        core[core_idx].curr_exe_job->task_no = IDLE_TASK_NO;              // Currently executing job initialized to IDLE for each core
        core[core_idx].decision_point = malloc (sizeof (Decision_point)); // Allocate memory for decision point structure in each core
        core[core_idx].core_criticality = current_level;                  // Core criticality is initialized to current criticality level of the system
        core[core_idx].status = ACTIVE;                                   // Initialize core status as ACTIVE
        core[core_idx].wakeup_time = NA;                                  // Initialize core wakeup time to NA 
        for (int i = 0; i < max_criticality; i++)                         // Initialize slack for all criticality levels to NA
            core[core_idx].slack_available[i] = NA;
        core[core_idx].idle_time = 0.0;                                   // Core idle time initialized to 0
    }

    // RUNTIME SCHEDULER

    // Initialize timecount to first decision point --> min {first decision points in all cores}
    timecount = get_next_decision_point (core, num_cores, task_arr, num_tasks, timecount, hyperperiod);
    printf (" Timecount initialized to %lf\n", timecount);
    
    // Scheduler loop - executes at every decision point
    while (timecount < hyperperiod) { 
    
        // printf ("\n Running scheduler loop for timecount %lf\n", timecount);
    
        // PREEMPTION HANDLING
        
        // For all ACTIVE cores
        for (core_idx = 0 ; core_idx < num_cores ; core_idx++) {
            if (core[core_idx].status == ACTIVE) {

                // Preempt the currently executing job if it has not yet completed executing (i.e. remaining execution time != 0), 
                // by adding it back to the respective core's run queue
                if (core[core_idx].curr_exe_job->task_no != IDLE_TASK_NO  && core[core_idx].curr_exe_job->execution_time > 0) {
                    core[core_idx].preempted_job = malloc (sizeof (Jobs));
                    core[core_idx].curr_exe_job->status_flag = PREEMPTED;
                    copy_job_structure (core[core_idx].preempted_job, core[core_idx].curr_exe_job);

                    // Run queue updation
                    update_run_queue (core[core_idx].qhead, core[core_idx].preempted_job);
                }
            }
        }

        // SCHEDULING DECISION POINTS

        // JOB ARRIVAL -- RUN QUEUE UPDATION

        // For all cores        
        for (core_idx = 0 ; core_idx < num_cores ; core_idx++) {

            // If the decision point occurred due to JOB ARRIVAL in an ACTIVE core, add ready jobs to that core's local run queue/discarded queue/pending request queue
            if ((core[core_idx].decision_point->decision_time == timecount) && (core[core_idx].decision_point->event & JOB_ARRIVAL)) { 
                add_ready_jobs ((&core[core_idx]), dhead, prhead, task_arr, num_tasks, timecount);
            }
        }

        // JOB TERMINATION -- DYNAMIC PROCRASTINATION + SHUTDOWN (w/o job migration)

        // For all cores
        for (core_idx = 0 ; core_idx < num_cores ; core_idx++) {

            // If the decision point occurred due to JOB TERMINATION in an ACTIVE core, check if the core can SHUTDOWN / reduce its OPERATING FREQUENCY to save power
            if (core[core_idx].status == ACTIVE /* FIXME: && (core[core_idx].decision_point->decision_time == timecount) && (core[core_idx].decision_point->event & JOB_TERMINATION) */) {     

                // If the core's run queue is empty
                if (core[core_idx].qhead->head_node == NULL) {

                    // Anticipate the next job arrival
                    for (i = 0 ; i < num_tasks ; i++) {
                        if (task_arr[i].allocated_core == core[core_idx].core_no) {
                            if (task_arr[i].criticality >= accept_above_criticality_level (current_level, core->threshold_criticality)) {
                                next_arrival = get_next_job_arrival (task_arr, i, timecount);
                                if (min_arrival > next_arrival) {
                                    min_arrival = next_arrival;
                                    min_idx = i; 
                                }
                            }
                        }
                    }

                    // If the next arrival is anticipated at/after (timecount + SHUTDOWN_THRESHOLD)
                    // SHUTDOWN core till next arrival
                    if (min_arrival >= (timecount + SHUTDOWN_THRESHOLD)) {
                        core[core_idx].wakeup_time = min_arrival;
                        core[core_idx].status = SHUTDOWN;
                    }

                    // If the next arrival is anticipated before (timecount + SHUTDOWN_THRESHOLD)
                    // Calculate the amount of slack obtained by DYNAMICALLY PROCRASTINATING jobs arriving before next job's deadline
                    else {                    

                        get_dynamic_procrastination_slack (core, core_idx, task_arr, num_tasks, min_arrival + task_arr[min_idx].deadline, max_criticality, current_level, hyperperiod, timecount);

                        // Check if the slack available in all criticality levels is equal to/exceeds the SHUTDOWN_THRESHOLD
                        for (i = 0; i < max_criticality; i++) {
                            if (core[core_idx].slack_available[i] < SHUTDOWN_THRESHOLD)
                                break;
                        }

                        // If slack available in all criticality levels is equal to/exceeds the SHUTDOWN_THRESHOLD
                        // SHUTDOWN core for the slack time calculated at current level (OR min?)
                        if (i == max_criticality) {
                            core[core_idx].wakeup_time = core[i].slack_available[current_level - 1];
                            core[core_idx].status = SHUTDOWN;
                        }
                        // else {
                            // JOB MIGRATION / DVFS / DISCARDED JOB SCHEDULING --- Set priority
                        // }
                    }
                }

                // TODO: If not EMPTY?? --> DVFS?
            }
        }

        // JOB TERMINATION -- DISCARDED JOB SCHEDULING

        // Add discarded job to run queue for scheduling if enough slack is available for it to execute -- TODO: Load balancing for discarded job scheduling

        // For all cores
        for (core_idx = 0 ; core_idx < num_cores ; core_idx++) {

            // If the decision point occurred due to JOB TERMINATION in an ACTIVE core and the current level > 1, check if the core can accommodate a discarded job to improve runtime utilization
            if (current_level > 1 && core[core_idx].status == ACTIVE && (core[core_idx].decision_point->decision_time == timecount) /*&& (core[core_idx].decision_point->event & JOB_TERMINATION)*/)  
                schedule_discarded_job (core[core_idx].qhead, dhead, task_arr, num_tasks, core[core_idx].threshold_criticality, max_criticality, current_level, core[core_idx].core_no, hyperperiod, timecount);
        }
        
        // CRITICALITY LEVEL, MODE CHANGE/JOB OVERRUN

        // If decision event in any ACTIVE core is JOB BUDGET EXCEEDED, update system and all core criticalities
        for (core_idx = 0 ; core_idx < num_cores ; core_idx++) {
            if (core[core_idx].status == ACTIVE && (core[core_idx].decision_point->decision_time == timecount) && (core[core_idx].decision_point->event & JOB_WCET_EXCEEDED))
                break;
        } 
        
        if (core_idx < num_cores) {
            current_level++;
            printf("\n Current level updated to %d\n\n", current_level);

            for (core_idx = 0 ; core_idx < num_cores ; core_idx++) {
                core[core_idx].core_criticality++;

                // For cores in which criticality level change is triggered because of a job exceeding its wcet budget and NOT job overrun
                // The currently executing job is handled as a preemption added back to the core's run queue
                if (core[core_idx].status == ACTIVE && core[core_idx].curr_exe_job->task_no != IDLE_TASK_NO && core[core_idx].curr_exe_job->execution_time == 0 && (core[core_idx].decision_point->event & JOB_WCET_EXCEEDED) && (core[core_idx].decision_point->decision_time == timecount)) {
                    core[core_idx].curr_exe_job->status_flag = PREEMPTED;  
                    core[core_idx].preempted_job = malloc (sizeof (Jobs));
                    copy_job_structure (core[core_idx].preempted_job, core[core_idx].curr_exe_job);
                    update_run_queue (core[core_idx].qhead, core[core_idx].preempted_job);
                    core[core_idx].curr_exe_job->task_no = IDLE_TASK_NO;
                }
                
                // For cores in which criticality level change is triggered because of a job overrunning its budget at its highest criticality level
                // The currently executing job is simply discarded from the core's run queue
                else if (core[core_idx].status == ACTIVE && (core[core_idx].decision_point->event & JOB_OVERRUN) && (core[core_idx].decision_point->decision_time == timecount)) 
                    core[core_idx].curr_exe_job->task_no = IDLE_TASK_NO;
                    
                // Case 1: Criticality mode: LO 
                //--> discard jobs with criticality < current criticality level of the system (after updation) from the run queue 
                if (current_level <= core[core_idx].threshold_criticality) 
                    discard_below_criticality_level (core[core_idx].qhead, dhead, current_level);

                // Case 2: Criticality mode: HI 
                // --> discard jobs with criticality <= threshold criticality from the run queue, 
                //     update absolute deadlines for all jobs and reorder run queue as per updated deadlines
                if (current_level > core[core_idx].threshold_criticality) {
                    printf(" Criticality MODE updated to HI\n (All jobs will now be scheduled wrt their original deadlines)\n\n");
                    discard_below_criticality_level (core[core_idx].qhead, dhead, (core[core_idx].threshold_criticality + 1));
                    update_sched_deadlines (core[core_idx].qhead, task_arr, num_tasks);
                    core[core_idx].qhead->head_node = merge_sort (core[core_idx].qhead->head_node);    // sort run queue
                }
            }
        }
        
       // Handling job overruns (When the criticality level change event is triggered ONLY due to job overruns - no criticality level updation reqd)
        else {
            for (core_idx = 0 ; core_idx < num_cores ; core_idx++) {
                if (core[core_idx].status == ACTIVE && (core[core_idx].decision_point->event & JOB_OVERRUN) && (core[core_idx].decision_point->decision_time == timecount)) 
                    core[core_idx].curr_exe_job->task_no = IDLE_TASK_NO;
            }   
        }
        
        // CORE WAKEUP

        for (core_idx = 0 ; core_idx < num_cores ; core_idx++) {
            if (core[core_idx].status == SHUTDOWN && (core[core_idx].decision_point->decision_time == timecount) && (core[core_idx].decision_point->event & WAKEUP_CORE)) {
                core[core_idx].status = ACTIVE;
                
                // Copy pending request queue jobs to core run queue
                temp = prhead->head_node;
                while (temp != NULL) {
                    if (temp->job->allocated_core == core[core_idx].core_no) {
                        update_run_queue (core[core_idx].qhead, temp->job);
                        delete_job_from_queue (prhead, temp->job);
                    }
                    temp = temp->next;
                } 
            }
        }

        // SCHEDULE NEXT JOB
        
        // Schedule next job 
        for (core_idx = 0 ; core_idx < num_cores ; core_idx++) {

            // Schedule next job from run queue head 
            core[core_idx].curr_exe_job = schedule_next_job (core[core_idx].qhead); 
        }
/*
        // Print allocated wcet budgets and randomly generated actual execution times  
        for (core_idx = 0 ; core_idx < num_cores ; core_idx++) {
            if (core[core_idx].curr_exe_job->task_no != IDLE_TASK_NO) {
                if (core[core_idx].curr_exe_job->status_flag == READY) 
                    printf(" For task %d, job %d wcet: %d actual exe:%lf\n", core[core_idx].curr_exe_job->task_no, core[core_idx].curr_exe_job->job_no, core[core_idx].curr_exe_job->wcet_budget[current_level - 1], core[core_idx].curr_exe_job->execution_time);
                else if (core[core_idx].curr_exe_job->status_flag == PREEMPTED)
                    printf(" Preempted task %d, job %d continued with remaining wcet: %d and exe time: %lf\n", core[core_idx].curr_exe_job->task_no, core[core_idx].curr_exe_job->job_no, core[core_idx].curr_exe_job->wcet_budget[current_level - 1], core[core_idx].curr_exe_job->execution_time);
            }
        }     
*/
        // Calculate next decision point
        next_decision_point = get_next_decision_point (core, num_cores, task_arr, num_tasks, timecount, hyperperiod);
        
        // Not required for schedule --- just to stop printing at timecount = hyperperiod
        if (next_decision_point > hyperperiod)    
            next_decision_point = hyperperiod;

        
        // Update the wcet and actual execution times for the job
        for (core_idx = 0 ; core_idx < num_cores ; core_idx++) {
            if(core[core_idx].status == ACTIVE) {
                if (core[core_idx].curr_exe_job->task_no != IDLE_TASK_NO) {
                    core[core_idx].curr_exe_job->execution_time = core[core_idx].curr_exe_job->execution_time - (next_decision_point - timecount);    
                    for (int i = 0 ; i < MAX_LEVELS ; i++)   
                        core[core_idx].curr_exe_job->wcet_budget[i] = core[core_idx].curr_exe_job->wcet_budget[i] - (next_decision_point - timecount); 
                } 
                else
                    core[core_idx].idle_time = core[core_idx].idle_time + (next_decision_point - timecount);            
            }
        }
        
        // Print schedule timecount to next decision point
        printf(" Time: %lf to %lf \t", timecount, next_decision_point);
        for (core_idx = 0 ; core_idx < num_cores ; core_idx++) {
            
            if (core[core_idx].status == ACTIVE) {

                if (core[core_idx].curr_exe_job->task_no == IDLE_TASK_NO)
                    printf(" Core: %d IDLE task \t\t", core[core_idx].core_no);
                
                if (core[core_idx].curr_exe_job->task_no != IDLE_TASK_NO) {   
                    if (core[core_idx].curr_exe_job->status_flag != PREEMPTED)
                        printf(" Core: %d Task %d Job %d   \t", core[core_idx].core_no, core[core_idx].curr_exe_job->task_no, core[core_idx].curr_exe_job->job_no);
                    else 
                        printf(" Core: %d Task %d Job %d # \t",core[core_idx].core_no, core[core_idx].curr_exe_job->task_no, core[core_idx].curr_exe_job->job_no);
                }
            }
            
            else
                printf(" Core: %d POWERED DOWN \t\t", core[core_idx].core_no);
        }
        printf("\n");
                
        // Timecount = next decision point
        timecount = next_decision_point;
    }
}


// -----------------
// HELPER FUNCTIONS
// -----------------

// Helper function to find the modulo of two floating point numbers

double find_modulo (double a, double b) {

    double a_modulo_b = 0.0;     // (a % b) result
    
    // Initialize (a % b) to magnitude of a
    if (a < 0.0)
        a_modulo_b = -a;
    else
        a_modulo_b =  a;

    // If b is negative, negate again to get magnitude
    if (b < 0.0)
        b = -b;

    if (b == 0.0)
        return 0.0;

    // Find a_modulo_b by subtracting b repeatedly till the remainder is < b
    while (a_modulo_b >= b && b != 0.0)
        a_modulo_b = a_modulo_b - b;
 
    // Sign of modulo is same as sign of a (convention)
    if (a < 0)
        return -a_modulo_b;

    // Return (a % b)
    return a_modulo_b;
}

// Helper function to print run queue

void print_run_queue (RQ_HEAD *head) {
    RQ_NODE *temp;
    temp = head->head_node;
    printf("\n");
    if (temp == NULL)
        printf (" The List is Empty\n");
    else {
        while (temp != NULL) {
            printf(" Task %d, %d (deadline %lf) -->",temp->job->task_no, temp->job->job_no, temp->job->sched_deadline);
            temp=temp->next;
        }
        printf("\n");
    }
} 

// Helper function to copy job structure from source pointer to destination pointer

void copy_job_structure (Jobs *dest, Jobs *src) {

    dest->job_no = src->job_no;
    dest->task_no = src->task_no;
    dest->allocated_core = src->allocated_core;
    dest->arrival_time = src->arrival_time;
    dest->sched_deadline = src->sched_deadline;
    dest->execution_time = src->execution_time;
    for (int i = 0 ; i < MAX_LEVELS ; i++)
        dest->wcet_budget[i] = src->wcet_budget[i];
    dest->job_criticality = src->job_criticality;
    dest->status_flag = src->status_flag;
}
