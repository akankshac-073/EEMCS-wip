#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "header.h"

// ---------------------------------------------------------------------------------------------
// FETCH INPUT TASK SET PARAMETERS FROM THE GIVEN FILE, STORE PARAMETERS IN TASK STRUCTURE ARRAY
// ---------------------------------------------------------------------------------------------

void fetch_task_parameters (FILE* fptr, Tasks *tasks_arr, int num_tasks, int max_criticality) {

    // For all tasks
    for (int i = 0 ; i < num_tasks ; i++) {

        // Record the task's phase, period, relative deadline and criticality level from the file
        fscanf(fptr,"%d\t%d\t%d\t%d", &tasks_arr[i].phase, &tasks_arr[i].period, &tasks_arr[i].deadline, &tasks_arr[i].criticality);

        // Assign a task number to each task structure
        tasks_arr[i].task_no = i + 1;

        // Initialize task's allocated core number to NOT_ALLOCATED
        tasks_arr[i].allocated_core = NOT_ALLOCATED;

        // Initialize the task's virtual deadline as its relative deadline
        tasks_arr[i].virtual_deadline = tasks_arr[i].deadline;

        // Allocate memory for WCET array; no. of elements = task's criticality level
        tasks_arr[i].wcet = malloc (tasks_arr[i].criticality * sizeof (int));

        // For each criticality level (defined for the given task)
        for (int j = 0 ; j < tasks_arr[i].criticality ; j++) {

            // Record task's WCET value for the current criticality level
            fscanf(fptr,"%d", &tasks_arr[i].wcet[j]);

            // Calculate the task's utilization for the current criticality level
            tasks_arr[i].utilization[j] = (double)(tasks_arr[i].wcet[j]) / (tasks_arr[i].period); 
        }

        // For criticality levels beyond the task's (maximum defined) criticality level,
        // the utilization is set to task utilization at it's own criticality level
        // (reqd for EDF-VD offline preprocessing)
        for (int j = tasks_arr[i].criticality ; j < max_criticality ; j++)
            tasks_arr[i].utilization[j] = (double)(tasks_arr[i].wcet[(tasks_arr[i].criticality - 1)]) / (tasks_arr[i].period);
    }
}

// -------------------------------------------------------------------------------------------------------------------------
// QUICK SORT TASKS IN DECREASING ORDER OF THEIR CRITICALITY LEVELS AND UTILIZATIONS (at highest level defined for the task)
// -------------------------------------------------------------------------------------------------------------------------

// Utility function to swap array elements

void swap (Tasks* A, Tasks* B) {

    Tasks temp = *A;
    *A = *B;
    *B = temp;
}
 
// Partitioning function for sorting task array in decreasing order of their criticalities and utilizations
// The pivot is placed at its correct position in sorted array by placing the "greater" elements to left and
// the "smaller" elements to right of pivot

int partition_array (Tasks *tasks_arr, int low, int high) {

    double pivot_utilization = tasks_arr[high].utilization[(tasks_arr[high].criticality - 1)];  // Last element's utilization is considered as pivot utilization
    int pivot_criticality = tasks_arr[high].criticality;                                        // Last element's criticality is considered as pivot criticality
    int i = (low - 1);                                                                          // Initialize pivot position to low

    // Determine pivot position in sorted array 
    // Sorting order: Decreasing criticality, and within same criticality decreasing utilization
    for (int j = low ; j <= high - 1 ; j++) {

        // If the criticality level of jth task is greater than that of pivot - increment position
        if (tasks_arr[j].criticality > pivot_criticality) {
            i++;
            swap (&tasks_arr[i], &tasks_arr[j]);
        }

        // If the criticality level of jth task is equal to that of pivot, but its utilization is greater than that of pivot - increment position
        else if ((tasks_arr[j].criticality == pivot_criticality) && (tasks_arr[j].utilization [(tasks_arr[j].criticality - 1)] > pivot_utilization)) { 
            i++;
            swap (&tasks_arr[i], &tasks_arr[j]); 
        }
    }
    
    // Swap to reposition pivot in the array 
    swap (&tasks_arr[i + 1], &tasks_arr[high]);
    
    // Return pivot's updated position 
    return (i + 1); 
}

// Quick sort driver function: sorts array in decreasing order of task criticality and utilization

void quick_sort (Tasks *tasks_arr, int low, int high) {
    int pi = 0;                     // Pivot index                           
    
    // Sort in decreasing order of criticality and then utilization
    if (low < high) {
    
        // Initialize pivot  
        pi = partition_array (tasks_arr, low, high); 
        
        // Recursive call to sort partitioned sub-arrays
        quick_sort (tasks_arr, low, pi - 1); 
        quick_sort (tasks_arr, pi + 1, high); 
    }
}

// --------------------------------------------------------------------------------------------------------------------
// GET TASK UTILIZATIONS INFO FOR GIVEN TASK SET, DETERMINE HI, LO UTILIZATIONS FOR (I) LOW PERIOD TASKS (II) ALL TASKS
// --------------------------------------------------------------------------------------------------------------------

void get_taskset_info (Tasks *tasks_arr, int num_tasks, Taskset_info* tasks_info, int hi_level_threshold) {

    int idx = 0;    // Index for getting utilization of a task at its own criticality level

    // Initializing all utilization stats
    tasks_info->hi_crit_util = 0.0;
    tasks_info->lo_crit_util = 0.0;
    tasks_info->lpd_hi_crit_util = 0.0;
    tasks_info->lpd_lo_crit_util = 0.0;

    // For all tasks
    for (int i = 0; i < num_tasks; i++) {

        idx = tasks_arr[i].criticality - 1;    // Set utilization array index to (task's criticality level - 1)

        // Update HI criticality utilization if task criticality is greater than hi_level_threshold
        if (tasks_arr[i].criticality > hi_level_threshold) {
            tasks_info->hi_crit_util = tasks_info->hi_crit_util + tasks_arr[i].utilization[idx];

            // Also update low period HI criticality utilization if task satifies the LPD condition
            if (2 * (tasks_arr[i].period - tasks_arr[i].wcet[0]) < LPD_THRESHOLD) // --> LPD condition
                tasks_info->lpd_hi_crit_util = tasks_info->lpd_hi_crit_util + tasks_arr[i].utilization[idx];
        }

        // Update LO criticality utilization if task is less than or equal to hi_level_threshold
        else {
            tasks_info->lo_crit_util = tasks_info->lo_crit_util + tasks_arr[i].utilization[idx];

            // Also update low period LO criticality utilization if task satifies the LPD condition
            if (2 * (tasks_arr[i].period - tasks_arr[i].wcet[0]) < LPD_THRESHOLD) // --> LPD condition
                tasks_info->lpd_lo_crit_util = tasks_info->lpd_lo_crit_util + tasks_arr[i].utilization[idx];
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// DETERMINE MINIMUM NUMBER OF CORES REQUIRED FOR ALLOCATION AS PER THE MC FEASIBILITY CONDITION CHECK 
// MC Feasibility condition: Total utilization of all tasks at any given level < 1
// ---------------------------------------------------------------------------------------------------

int get_min_cores_reqd (Tasks *tasks_arr, int num_tasks, int max_criticality) {

    int min_cores_reqd = 0;               // Minimum number of cores required for allocation
    double utilization_sum = 0.0;         // Sum of utilizations of all tasks at a given criticality level
    double max_utilization_sum = 0.0;     // Maximum sum of utilizations of all tasks among all criticality levels

    // Calculate sum of utilizations of all tasks each criticality level
    for (int level = 1; level <= max_criticality; level++) {

        // Initialize sum to zero for each level
        utilization_sum = 0.0;

        // For all tasks
        for (int i = 0; i < num_tasks; i++) {

            // If task's criticality level <= current level, add utilization at current level
            if (tasks_arr[i].criticality <= level)
                break;
            utilization_sum = utilization_sum + tasks_arr[i].utilization[level - 1];
        }
        
        // Update the maximum value after calculating sum at each criticality
        if (max_utilization_sum < utilization_sum) 
            max_utilization_sum = utilization_sum;
    }

    printf (" Maximum utilization among all criticalities: %lf\n\n", max_utilization_sum);

    // Minimum number of cores required will be the ceiling of maximum utilization sum among all criticalities
    min_cores_reqd = ceil(max_utilization_sum); 
    
    // Return minimum number of cores required for allocation as dictated by the MC Feasibility condition
    return min_cores_reqd;
}

// ----------------
// HELPER FUNCTIONS
// ----------------

// Helper function to print sorted task array

void print_sorted_array (Tasks *tasks_arr, int num_tasks) { 

    printf(" Sorted task structure array\n\n"); 
    for (int i = 0 ; i < num_tasks ; i++)
        printf(" Task %d \tCriticality: %d \tUtilization:%lf\n", tasks_arr[i].task_no, tasks_arr[i].criticality, tasks_arr[i].utilization[(tasks_arr[i].criticality - 1)]); 
    printf("\n"); 
}

// Helper funtion to print the taskset information for the given workload

void print_taskset_info (Taskset_info* tasks_info) {

    // Print entire taskset stats
    printf("\n Total Utilization: %lf\n", tasks_info->hi_crit_util + tasks_info->lo_crit_util);
    printf(" Total HI Utilization: %lf\n", tasks_info->hi_crit_util);
    printf(" Total LO Utilization: %lf\n", tasks_info->lo_crit_util);
    printf(" Proportion of HI criticality tasks in the given workload: %lf\n\n", tasks_info->hi_crit_util / (tasks_info->hi_crit_util + tasks_info->lo_crit_util));

    // Print low period tasks subset stats
    printf("\n Total LPD Utilization: %lf\n", tasks_info->lpd_hi_crit_util + tasks_info->lpd_lo_crit_util);
    printf(" Total LPD tasks HI Utilization: %lf\n", tasks_info->lpd_hi_crit_util);
    printf(" Total LPD tasks LO Utilization: %lf\n", tasks_info->lpd_lo_crit_util);

    double temp = tasks_info->lpd_hi_crit_util / (tasks_info->lpd_hi_crit_util + tasks_info->lpd_lo_crit_util);

    if ((tasks_info->lpd_hi_crit_util + tasks_info->lpd_lo_crit_util) != 0.0) // To rule out the divide by zero case
        printf(" Proportion of LPD HI criticality tasks in all LPD tasks in the given workload: %lf\n\n", temp);
    else
        printf(" Proportion of LPD HI criticality tasks in all LPD tasks in the given workload: NA\n\n");
}

// Helper funtion to free all the wcet mallocs

void free_wcet_mallocs (Tasks *task_ptr, int num_tasks) {

    // For all tasks
    for (int i = 0 ; i < num_tasks ; i++) 
        free(task_ptr[i].wcet); 
}
