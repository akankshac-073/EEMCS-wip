#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "header.h"

// --------------------------------------
// EDF-VD OFFLINE PREPROCESSING FUNCTIONS
// --------------------------------------

// Compute total utilization of tasks when executed at a given criticality level k

double calculate_utilization_ulk (Tasks *tasks_arr, int num_tasks, int lower_limit, int upper_limit, int core_no, int new_task_no) {
    int k = lower_limit - 1;       // * lower limit is threshold criticality + 1, k is threshold criticality
    double utilization_ulk = 0.0;

    // For all tasks
    for (int i = 0 ; i < num_tasks ; i++) {
    
        // If the task belongs to the given core / is being considered for allocation
        if ((tasks_arr[i].allocated_core == core_no) || (tasks_arr[i].task_no == new_task_no)) {

            // Add k-th level task utilizations for all tasks having criticality level between lower and upper limits
            if (tasks_arr[i].criticality >= lower_limit && tasks_arr[i].criticality <= upper_limit)
                utilization_ulk = utilization_ulk + tasks_arr[i].utilization[k - 1];
        }
    }
    return utilization_ulk;
}

// Compute total utilization of tasks when executed at their own criticality levels

double calculate_utilization_ull (Tasks *tasks_arr, int num_tasks, int lower_limit, int upper_limit, int core_no, int new_task_no) {
    double utilization_ull = 0.0;

    // For all tasks
    for (int i = 0 ; i < num_tasks ; i++) {

        // If the task belongs to given core / is being considered for allocation
        if ((tasks_arr[i].allocated_core == core_no) || (tasks_arr[i].task_no == new_task_no)) {

            // Add task utilizations (at their own criticality levels) for all tasks having criticality level between lower and upper limits
            if (tasks_arr[i].criticality >= lower_limit && tasks_arr[i].criticality <= upper_limit) {
                utilization_ull = utilization_ull + tasks_arr[i].utilization[(tasks_arr[i].criticality - 1)];
            }
        }
    }
    return utilization_ull;
}

// Check if the EDF-VD Schedulability condition holds for given core, assuming we add the new task to it 

int edfvd_schedulability_check (Tasks* tasks_arr, int num_tasks, int max_criticality, int core_no, int new_task_no) {

    int threshold_criticality = 0;     // EDF-VD threshold criticality
                                       // All tasks with criticality greater than this threshold are HI criticality tasks
    double x = 0.0;                    // Deadline shortening factor (0.0 < x <= 1.0)
    double x_ub = 0.0;                 // Upper bound on deadline shortening factor --> to ensure HI mode schedulability
    double x_lb = 0.0;                 // Lower bound on deadline shortening factor --> to ensure LO mode schedulability

    // Sum of all task utilizations (at their own criticality level) < 1 --> EDF schedulable
    if (calculate_utilization_ull (tasks_arr, num_tasks, 1, max_criticality, core_no, new_task_no) <= 1.0) {

        // Scheduling is done as per original deadlines for all tasks --> criticality agnostic EDF

        // For all tasks
        for (int i = 0 ; i < num_tasks; i++) {

            // If task is already allocated to the given core/ is the new task being considered for allocation
            if ((tasks_arr[i].allocated_core == core_no) || (tasks_arr[i].task_no == new_task_no))

                // Update virtual deadlines
                tasks_arr[i].virtual_deadline = tasks_arr[i].deadline;
        }

        // EDF condition holds, so EDF-VD threshold criticality is set to highest criticality level defined for the system
        return max_criticality;
    }

    // Else the taskset is not EDF schedulable, so we check for EDF-VD schedulability condition for all possible threshold criticalities
    else {

        // Check EDFVD conditions at each criticality level and set threshold criticality level for the core
        // All tasks having criticality > threshold criticality --> HI criticality, else LO criticality
        for (threshold_criticality = max_criticality - 1 ; threshold_criticality > 0 ; threshold_criticality--) {

            // EDFVD schedulability condition (part 1)
            // --> Sum of LO-criticality (criticality < threshold) tasks at their own levels must be less than 1.0
            if (calculate_utilization_ull (tasks_arr, num_tasks, 1, threshold_criticality, core_no, new_task_no) < 1.0) {

                // Calculate lower bound on deadline shortening factor to ensure schedulability in LO mode
                x_lb = calculate_utilization_ulk (tasks_arr, num_tasks, threshold_criticality + 1, max_criticality, core_no, new_task_no) /
                       (1.0 - calculate_utilization_ull (tasks_arr, num_tasks, 1, threshold_criticality, core_no, new_task_no));

                // Calculate upper bound on deadline shortening factor to ensure schedulability in HI mode
                x_ub = (1.0 - calculate_utilization_ull (tasks_arr, num_tasks, threshold_criticality + 1, max_criticality, core_no, new_task_no)) /
                       calculate_utilization_ull (tasks_arr, num_tasks, 1, threshold_criticality, core_no, new_task_no);

                // EDFVD schedulability condition (part 2) --> If a non-empty feasible range for x exists such that x_lb <= x <= x_ub
                if (x_lb <= x_ub) {

                    // TODO: TEMPORARY CODE - set x, update virtual deadlines
                    // --> To be implemented after DVFS offline part when x_optimal is determined

                    // Choose any x value that lies within the feasible range determined (set to mid for now)
                    x = (x_lb + x_ub) / 2;

                    // Scheduling is done as per the virtual deadline field
                    // For HI criticality (criticality > threshold) tasks virtual deadlines are set to x * original deadlines
                    // For LO criticality (criticality <= threshold) tasks virtual deadlines are set to original deadlines

                    // For all tasks
                    for (int i = 0 ; i < num_tasks; i++) {

                        // Check if task is already allocated to the given core/ or is the new task being considered for allocation in the given core
                        if ((tasks_arr[i].allocated_core == core_no) || (tasks_arr[i].task_no == new_task_no)) {

                            // Update virtual deadlines
                            if (tasks_arr[i].criticality <= threshold_criticality)
                                tasks_arr[i].virtual_deadline = tasks_arr[i].deadline;
                            else
                                tasks_arr[i].virtual_deadline = x * tasks_arr[i].deadline;
                        }
                    }

                    // Return the threshold criticality value for which the EDF-VD condition holds
                    return threshold_criticality;
                }
            }
        }

        // No schedulability condition (EDF or EDF-VD) holds, return INVALID threshold criticality to indicate that the task cannot be allocated
        return -73;
    }
}

// ---------------------------------
// OFFLINE TASK ALLOCATION FUNCTIONS
// ---------------------------------

// Initialize all core structure parameters for allocation

void initialize_cores_offline (Cores *core, int max_criticality) {

    // For all cores
    for (int i = 0; i < MAX_CORES; i++) {
        core[i].core_no = i + 1;                                 // Assign a core number 
        core[i].utilization = 0.0;                               // Initialize core utilization to 0.0
        core[i].remaining_capacity = 1.0;                        // Initialize remaining capacity (for bin-packing) to 1.0
        core[i].tasks_alloc_count = 0;                           // Initialize tasks allocated count as 0
        for (int j = 0; j < MAX_TASKS; j++)                      // Initialize all allocated task ids to 0
            core[i].tasks_alloc_ids[j] = 0;
        core[i].threshold_criticality = max_criticality + 1;     // Initialize core threshold criticality to max criticality + 1
        core[i].operating_frequency = BASE_OPERATING_FREQUENCY;  // Initialize core's operating frequency to base operating frequency of the system
        core[i].core_type = SHUTDOWNABLE;                        // Initialize core type as SHUTDOWNABLE
    }
}

// Reset remaining core capacities when moving on to allocation of tasks of next (lower) criticality level --> to maintain MCS feasibility condition 

void reset_core_capacities (Cores *core, int num_cores, Tasks *tasks_arr, int criticality, int task_array_idx) {

    // For all (opened) cores
    for (int j = 0 ; j < num_cores ; j++) {

        // Reset remaining core capacity to 1.0
        core[j].remaining_capacity = 1.0;

        // From the cores to which at least one higher criticality task allocated
        for (int k = 0; k < task_array_idx; k++) {
            if (tasks_arr[k].allocated_core == core[j].core_no)

                // Subtract utilizations of allocated tasks at current (changed) criticality level --> to maintain MCS Feasibility condition
                core[j].remaining_capacity = core[j].remaining_capacity - tasks_arr[k].utilization[criticality - 1];
        }
    }
}

// Find the worst-fitting core (i.e. with maximum remaining capacity) that can accommodate the given task

int get_worst_fit_core_idx (Cores *core, int num_cores, Tasks *tasks_arr, int num_tasks, int task_idx, int max_criticality) {

    int worst_fit_idx = -1;                             // Worst-fitting core's index
    double max_remaining_capacity = -1;                 // To maintain maximum remaining capacity among all cores
    int idx = tasks_arr[task_idx].criticality - 1;      // Temp variable to store utilization array's index value for a task
    int new_threshold_crit = max_criticality;           // Core's newly calculated threshold criticality value

    // For all (open) cores
    for (int j = 0 ; j < num_cores ; j++) {

        // Check if the core can accommodate the given task and has more remaining capacity (after accommodating the task) than previously considered cores
        if (core[j].remaining_capacity >= tasks_arr[task_idx].utilization[idx] && core[j].remaining_capacity - tasks_arr[task_idx].utilization[idx] > max_remaining_capacity) {

            // If core utilization is going to exceed 1.0 by accommodating the given task, check EDFVD schedulability
            if (tasks_arr[task_idx].utilization[idx] + core[j].utilization > 1.00) {

                // Check if the EDFVD schedulability condition is satisfied by accommodating given task and determine the new threshold criticality for the core
                new_threshold_crit = edfvd_schedulability_check (tasks_arr, num_tasks, max_criticality, core[j].core_no, tasks_arr[task_idx].task_no);

                // If the new threshold criticality lies in valid range for EDF-VD schedulability
                // Update the core threshold criticality, maximum remaining capacity and the worst fitting index
                if (new_threshold_crit > 0 && new_threshold_crit < max_criticality) {
                    core[j].threshold_criticality = new_threshold_crit;
                    worst_fit_idx = j;
                    max_remaining_capacity = core[j].remaining_capacity - tasks_arr[task_idx].utilization[idx];
                }
                // Else, core cannot accomodate the given task in current core, move on to next core (i.e. next for loop iteration)
            }

            // Else, the core utilization is less than or equal to 1.0 by accommodating the given task --> EDF schedulable
            else {
                core[j].threshold_criticality = max_criticality;
                worst_fit_idx = j;
                max_remaining_capacity = core[j].remaining_capacity - tasks_arr[task_idx].utilization[idx];
            }
        }
    }

    // Return worst-fitting core's index
    // If no such core found, index returned is num_cores (out of valid range)
    return worst_fit_idx;
}

// Find the first-fitting core (i.e. first core with remaining capacity > task utilization) that can accommodate the given task

int get_first_fit_core_idx (Cores *core, int num_cores, Tasks *tasks_arr, int num_tasks, int task_idx, int max_criticality) {

    int first_fit_idx = -1;                             // First fitting core's index
    int idx = tasks_arr[task_idx].criticality - 1;      // Temp variable to store utilization array's index value for a task
    int new_threshold_crit = max_criticality;           // Core's newly calculated threshold criticality value

    // For all (open) cores
    for (int j = 0 ; j < num_cores ; j++) {

        // Check if the core can accommodate the given task
        if (core[j].remaining_capacity >= tasks_arr[task_idx].utilization[idx]) {

            // If core utilization is going to exceed 1.0 by accommodating the given task, check EDFVD schedulability
            if (tasks_arr[task_idx].utilization[idx] + core[j].utilization > 1.00) {

                // Check if the EDFVD schedulability condition is satisfied by accommodating given task and determine the new threshold criticality
                new_threshold_crit = edfvd_schedulability_check (tasks_arr, num_tasks, max_criticality, core[j].core_no, tasks_arr[task_idx].task_no);

                // If the new threshold criticality lies in valid range for EDF-VD schedulability
                // Update the core threshold criticality and the first fitting index
                if (new_threshold_crit > 0 && new_threshold_crit < max_criticality) {
                    core[j].threshold_criticality = new_threshold_crit;
                    first_fit_idx = j;
                    break;
                }
                // Else, core cannot accomodate the given task in current core, move on to next core (i.e. next for loop iteration)
            }

            // Else, the core utilization is less than or equal to 1.0 by accommodating the given task --> EDF schedulable 
            else { 
                core[j].threshold_criticality = max_criticality;
                first_fit_idx = j;
            }
        }
    }

    // Return first-fitting core's index
    // If no such core found, index returned is num_cores (out of valid range)
    return first_fit_idx; 
}

// Allocate the given task to the core with index obtained from the allocation algorithm 
// Update task and core structure parameters accordingly

void allocate_task_to_core (Cores *core, Tasks *tasks_arr, int core_idx, int task_idx) {

    int idx = tasks_arr[task_idx].criticality - 1;       // Temp variable to store utilization array's index value for a task

    // Update remaining core capacity
    core[core_idx].remaining_capacity = core[core_idx].remaining_capacity - tasks_arr[task_idx].utilization[idx];

    // Update total core utilization
    core[core_idx].utilization = core[core_idx].utilization + tasks_arr[task_idx].utilization[idx];

    // Increment the count of tasks allocated to this core by 1
    core[core_idx].tasks_alloc_count++;

    // Add the given task's id to the list of task ids allocated to the core
    core[core_idx].tasks_alloc_ids[(core[core_idx].tasks_alloc_count) - 1] = tasks_arr[task_idx].task_no;

    // Update task structure with allocated core's id
    tasks_arr[task_idx].allocated_core = core[core_idx].core_no;

    // Core criticality is already updated at the time of EDFVD schedulability check
}

// Offline task allocation driver code

int offline_task_allocator (Cores *core, Tasks *tasks_arr, int num_tasks, int min_cores, int max_criticality) {

    int num_cores = 0;                   // Number of cores required to schedule the given task set
    int min_LPD_cores = 0;               // Minimum number of cores reqd for low period tasks' allocation
    int wfd_threshold_crit = 0;          // All tasks above this level will be allocated using WFD bin-packing
    int core_idx = -1;                   // Worst fitting core's index
    int i = 0;                           // Index to traverse through task structure array

    // Get taskset info
    Taskset_info* tasks_info;
    tasks_info = malloc (sizeof (Taskset_info));
    get_taskset_info (tasks_arr, num_tasks, tasks_info, ((max_criticality / 2) + (max_criticality % 2)));

    // Initialize all the core structures
    initialize_cores_offline (core, max_criticality);

    // LOW PERIOD TASK ALLOCATION

    // Determine the minimum number of cores required to accommodate all low period tasks
    // (Ceiling of total low period tasks utilization)
    if ((tasks_info->lpd_hi_crit_util + tasks_info->lpd_lo_crit_util) > 0.0) {
        min_LPD_cores = ceil(tasks_info->lpd_hi_crit_util + tasks_info->lpd_lo_crit_util);
        printf(" Minimum number of cores reqd for LPD task allocation: %d\n", min_LPD_cores);

        // Determine allocation scheme for low period tasks
        // If the HI criticality utilization is at most 40% of the total utilization - WFD + FFD scheme for balanced HI criticality load
        if ((tasks_info->lpd_hi_crit_util > 0.0) && (tasks_info->lpd_hi_crit_util / (tasks_info->lpd_hi_crit_util + tasks_info->lpd_lo_crit_util) <= 0.40)) {
            wfd_threshold_crit = (max_criticality / 2) + (max_criticality % 2);
            printf("\n Proportion of HI criticality LPD tasks <= 0.40\n Allocation scheme selected for LPD task allocation is WFD + FFD\n");
        }

        // Else, only FFD scheme is followed to accommodate all the tasks in minimum number of cores
        else {
            wfd_threshold_crit = max_criticality;
            if (tasks_info->lpd_hi_crit_util > 0.0)
                printf("\n Proportion of HI criticality LPD tasks > 0.40\n Allocation scheme selected for LPD task allocation is FFD\n");
            else
                printf("\n Proportion of HI criticality LPD tasks = 0.00\n Allocation scheme selected for LPD task allocation is FFD\n");
        }

        // Begin low period task allocation with min LPD cores open
        num_cores = min_LPD_cores;

        // For all low period tasks
        for (i = 0; i < num_tasks; i++) {

            if (2 * (tasks_arr[i].period - tasks_arr[i].wcet[0]) < LPD_THRESHOLD) {

                // When moving on to tasks of next (lower) criticality level, reset bin capacities to maintain MCS feasibility condition in each core
                if (i != 0 && tasks_arr[i-1].criticality > tasks_arr[i].criticality)
                    reset_core_capacities (core, num_cores, tasks_arr, tasks_arr[i].criticality, i);

                // Find the worst-fitting core that can accommodate the given task
                if (tasks_arr[i].criticality > wfd_threshold_crit)
                    core_idx = get_worst_fit_core_idx (core, num_cores, tasks_arr, num_tasks, i, max_criticality);
                else
                    core_idx = get_first_fit_core_idx (core, num_cores, tasks_arr, num_tasks, i, max_criticality);

                // If such worst-fitting core exists, allocate task to this core
                if (core_idx >= 0 && core_idx < num_cores) {
                    allocate_task_to_core (core, tasks_arr, core_idx, i);
                    core[core_idx].core_type = NON_SHUTDOWNABLE;
                }

                // Else open a new core, and allocate the task to the newly opened core
                else {
                    num_cores++;                                             // Increment the number of cores required for allocation

                   // If num_cores exceeds the maximum number of cores available in the system
                   // Return -1, indicating allocation failure
                    if (num_cores > MAX_CORES)
                        return -1;

                    core_idx = num_cores - 1;                                // Else set core_idx to access newly opened core's structure
                    core[core_idx].remaining_capacity = 1.0;                 // Initialize newly opened core's capacity to 1.0
                    core[core_idx].threshold_criticality = max_criticality;  // Initialize core's threshold criticality to max_criticality (EDF schedulable)
                    allocate_task_to_core (core, tasks_arr, core_idx, i);
                    core[core_idx].core_type = NON_SHUTDOWNABLE;
                }
            }
        }
    }

    printf(" LPD task allocation complete..\n\n");

    // DETERMINE ALLOCATION SCHEME FOR THE REMAINING TASKS

    // If the HI criticality utilization is at most 40% of the total utilization - WFD + FFD scheme for balanced HI criticality load
    if ((tasks_info->hi_crit_util > 0.0) && (tasks_info->hi_crit_util / (tasks_info->hi_crit_util + tasks_info->lo_crit_util) <= 0.40)) {
        wfd_threshold_crit = (max_criticality / 2) + (max_criticality % 2);
        printf("\n Proportion of HI criticality tasks <= 0.40\n Allocation scheme selected for remaining task allocations is WFD + FFD\n");
    }

    // Else, only FFD scheme is followed to accommodate all the tasks in minimum number of cores
    else {
        wfd_threshold_crit = max_criticality;
        if (tasks_info->hi_crit_util > 0.0)
            printf("\n Proportion of HI criticality LPD tasks > 0.40\n Allocation scheme selected for remaining task allocations is FFD\n");
        else
            printf("\n Proportion of HI criticality LPD tasks = 0.00\n Allocation scheme selected for remaining task allocations is FFD\n");
    }

    // REMAINING TASKS' ALLOCATION

    // Begin the remaining task allocations with min_cores open
    // (if the num_cores already open < min_cores)
    if (num_cores < min_cores)
        num_cores = min_cores;

    printf(" Beginning remaining task allocations with %d cores...\n", num_cores);

    // For all the remaining tasks
    for (i = 0; i < num_tasks; i++) {
        if (tasks_arr[i].allocated_core == NOT_ALLOCATED) {

            // When moving on to tasks of next (lower) criticality level, reset bin capacities to maintain MCS feasibility condition in each core
            if (i != 0 && tasks_arr[i - 1].criticality > tasks_arr[i].criticality)
                reset_core_capacities (core, num_cores, tasks_arr, tasks_arr[i].criticality, i);

            // Find the worst-fitting core that can accommodate the given task
            if (tasks_arr[i].criticality > wfd_threshold_crit)
                core_idx = get_worst_fit_core_idx (core, num_cores, tasks_arr, num_tasks, i, max_criticality);
            else
                core_idx = get_first_fit_core_idx (core, num_cores, tasks_arr, num_tasks, i, max_criticality);

            // If such worst-fitting core exists, allocate task to this core
            if (core_idx >= 0 && core_idx < num_cores)
                allocate_task_to_core (core, tasks_arr, core_idx, i);

            // Else open a new core, and allocate the task to the newly opened core
            else {
                num_cores++;                                             // Inrement the number of cores required for allocation
                if (num_cores > MAX_CORES)
                    return -1;
                core_idx = num_cores - 1;                                // Else set core_idx to access newly opened core's structure
                core[core_idx].remaining_capacity = 1.0;                 // Initialize newly opened core's capacity to 1.0
                core[core_idx].threshold_criticality = max_criticality;  // Initialize core's threshold criticality to max_criticality (EDF schedulable)
                allocate_task_to_core (core, tasks_arr, core_idx, i);
            }
            
            // print_task_allocations (core, num_cores);
        }
    }

    // Return total number of cores required for allocation
    return num_cores;
}

// -----------------
// HELPER FUNCTIONS
// -----------------

// Helper function to print task allocations

void print_task_allocations (Cores *core, int num_cores) {

    // For all cores
    for (int i = 0; i < num_cores; i++) {

        // Print total number of tasks allocated
        printf("\n Core %d: %d tasks allocated\n", core[i].core_no, core[i].tasks_alloc_count);

        // Print total utilization and remaining capacity for the given core
        printf(" Total core utilization: %f\n Core remaining capacity: %f\n", core[i].utilization, core[i].remaining_capacity);

        // Print core threshold criticality
        printf(" Core threshold criticality: %d\n", core[i].threshold_criticality);

        // Print allocated task ids
        printf(" Task ids: ");
        for (int j = 0 ; j < core[i].tasks_alloc_count; j++)
            printf("%d \t", core[i].tasks_alloc_ids[j]);
        printf("\n\n");
    }

    printf(" ------------------------------------------------------------------------------\n");
}
