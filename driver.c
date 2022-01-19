#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "header.h"

int main (int argc, char *argv[]) {

    Tasks *tasks_arr;              // Pointer to task structure array
    FILE *fptr;                    // Input file pointer
    int num_tasks = 0;             // Number of tasks in the input task set
    int max_criticality = 0;       // Maximum criticality level defined for the given task set    
    Cores core[MAX_CORES];         // Core structure array; MAX_CORES is the maximum number of cores available in the system
    int min_cores = 0;             // Minimum number of cores required for accommodating taskset as per the MCS feasibility condition
    int num_cores_reqd = 0;        // Number of cores required to accommodate the given task set as per the proposed task allocation algorithm
    int superhyperperiod = 0;      // Hyperperiod of the entire input task set (hyperperiod of tasks in all cores)
    
    // Open and read input file
    fptr = fopen("input.txt","r");
    if (fptr == NULL) {
        printf(" ERROR: Could not open the input file containing taskset parameters\n");
        return -1; 
    } 

    // Read number of tasks and maximum criticality level from input file
    fscanf(fptr,"%d\n%d", &num_tasks, &max_criticality);
    
    // Allocate memory for task structure array
    tasks_arr = malloc(num_tasks * sizeof (Tasks));

    // Fetch task set parameters from the input file and populate the task structure array
    fetch_task_parameters(fptr, tasks_arr, num_tasks, max_criticality);

    // Close input file
    fclose(fptr);
    
    // Sort task structure array in decreasing order of task criticality and utilization 
    quick_sort(tasks_arr, 0, num_tasks - 1);
    print_sorted_array(tasks_arr, num_tasks);

    // Determine the minimum number of cores required to schedule the given taskset as per the MCS Feasibility Condition
    min_cores = get_min_cores_reqd(tasks_arr, num_tasks, max_criticality);
    printf("\n Minimum number of cores required to satisfy the MCS feasibility condition for the given taskset: %d\n\n", min_cores); 
    
    // If the minimum number of cores required is less than the MAXIMUM CORES available, proceed with allocation and scheduling
    if (min_cores <= MAX_CORES) {
        
        // Allocate tasks to cores
        num_cores_reqd = offline_task_allocator(core, tasks_arr, num_tasks, min_cores, max_criticality);
        
        // If the allocation is done successfully (i.e all tasks are accommodated within the available number of cores)
        if(num_cores_reqd > 0 && num_cores_reqd <= MAX_CORES){
        
            // Print task allocations
            printf(" Task allocation complete ...\n\n Total number of cores required for allocation: %d\n", num_cores_reqd);
            print_task_allocations(core, num_cores_reqd);

            // Calculate the superhyeperperiod (hyperperiod of tasks in all cores)
            superhyperperiod = calculate_superhyperperiod (tasks_arr, num_tasks);
            printf(" Super-hyperperiod: %d\n\n", superhyperperiod);
            
             // Call runtime scheduler
             srand(time(0));    // Initializes random number generator for simulating actual execution time values    
             run_scheduler_loop (core, num_cores_reqd, tasks_arr, num_tasks, superhyperperiod, max_criticality);

        }
        else
            printf(" Number of cores required exceeds the maximum limit ...\n Input taskset cannot be scheduled.\n");
    }
    else
        printf(" MCS feasibility condition cannot be satisfied with the given number of cores.\n Input taskset cannot be scheduled.\n");    
    

    // Free all dynamically allocated memory
    free_wcet_mallocs(tasks_arr, num_tasks); 
    free(tasks_arr);

    return 0;
}

