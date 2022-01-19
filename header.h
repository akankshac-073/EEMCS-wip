// ==================
// MACRO DECLARATIONS
// ==================

// ------------------
// SYSTEM CONSTRAINTS
// ------------------

#define MAX_CORES 20                      // Maximum number of cores available 
#define MAX_TASKS 20                      // Maximum number of tasks that can be allocated to core
#define MAX_LEVELS 5                      // Maximum number of criticality levels supported by the system

// --------------------------------
// PRE-DETERMINED SYSTEM PARAMETERS
// --------------------------------

#define LPD_THRESHOLD 10                  // Minimum threshold value for (2*period - 2*wcet) for a task to be categorized as Low Period (LPD)
#define SHUTDOWN_THRESHOLD 10              // Minimum idle time required for a core to be able to SAVE energy by shutting down
                                          // (Just a dummy value --> the actual value can be pre-determined using Critical Frequency)
#define TIME_GRANULARITY 0.01             // Timecount granularity of the runtime scheduler
#define BASE_OPERATING_FREQUENCY 1.0      // All frequency values are normalized wrt the base operating frequency value

// ----------------------------------------
// TASK PARAMETERS - DEFAULT/SPECIAL VALUES
// ----------------------------------------

#define NOT_ALLOCATED -73                 // The allocated core id value is set to an invalid number when the task is yet to be allocated
#define IDLE_TASK_NO 0                    // Task number for an IDLE task is defined as IDLE_TASK_NO = 0

// ---------------------------------------------------------------
// DIFFERENT CORE TYPE VALUES (in the context of SHUTDOWN-ability)
// ---------------------------------------------------------------

#define NON_SHUTDOWNABLE 0                // Core type value 0 indicates that the core consists of LPD tasks, hence canNOT be SHUTDOWN
#define SHUTDOWNABLE 1                    // Core type value 1 indicates that the core consists of only HPD tasks, hence can be SHUTDOWN

// --------------------------------------------------------------
// DIFFERENT CORE STATUS VALUES (in the context of energy saving)
// --------------------------------------------------------------

#define SHUTDOWN 0                        // Status value 0 indicates that the core is powered down to save energy
#define ACTIVE 1                          // Status value 1 indicates that the core is actively scheduling and executing tasks     

// ----------------------------------------------------------
// DIFFERENT JOB STATUS VALUES (in the context of preemption)
// ----------------------------------------------------------

#define READY 0                           // Status flag in job structure is set to a default value of 0 upon arrival
#define PREEMPTED 1                       // Status flag in job structure is set to 1 if the job is preempeted - useful for printing and debugging

// -------------------------------------                      
// SCHEDULING DECISION POINT FLAG VALUES
// -------------------------------------

#define JOB_ARRIVAL 1                     // xxxx1 to indicate new/discarded job arrival
#define JOB_TERMINATION 2                 // xxx1x to indicate that currently executing job's completion
#define JOB_WCET_EXCEEDED 4               // xx1xx to indicate that the currently executing job exceeds its allocated wcet budget => CRITICALITY LEVEL CHANGE
#define JOB_OVERRUN 8                     // x1xxx to indicate that the currently executing job overruns its allocated wcet budget (at highest defined criticality level) 
#define WAKEUP_CORE 16                    // 1xxxx to indicate wakeup time for a core that is currently powered down

// Each flag value is added to core decision point event flag if the corresponding condition is satisfied
// Ex: 01001 would indicate that the next decision point is due to a new job arriving as well as the currently executing job overrunning

#define NA -1                             // Default slack value for SHUTDOWN cores

// ==============================
// ABSTRACT DATA TYPE DEFINITIONS
// ==============================

// ------------------------
// JOB STRUCTURE DEFINITION
// ------------------------

typedef struct {
    int job_no;                           // To identify a job structure - (to track no. of instances executed)
    int task_no;                          // Task number corresponding to the task set that generated the job 
    int allocated_core;                   // Stores the core number of the core it is allocated to
    int arrival_time;                     // Arrival time of the job
    double sched_deadline;                // Deadline according to which the scheduling is done (can be virtual/actual deadline of the job) 
    double execution_time;                // Remaining (actual) execution time of the job - execution times are generated randomly using rand fn
    int wcet_budget[MAX_LEVELS];          // To maintain the remaining execution time budget (timer) of the job at different criticality levels
    int job_criticality;                  // Criticality level of the job (same as the criticality level of the corresponding task set)  
    int status_flag;                      // Flag = 0: fresh arrival, Flag = 1: preempted - can be used to indicate other process states later on  
}Jobs;

// -------------------------
// TASK STRUCTURE DEFINITION
// -------------------------

typedef struct { 
    int task_no;                          // To identify a task structure
    int phase;                            // Task phase - release time
    int period;                           // Task period/Minimum inter-arrival time (we assume a periodic task set*)
    int *wcet;                            // Worst-case execution time (array pointer, size - dynamically allocated according to the maximum criticality level in which the task can execute)
    int criticality;                      // Criticality level designated to the task set
    int deadline;                         // Relative deadline
    double virtual_deadline;              // Virtual deadline of a task (determined by EDFVD offline preprocessing phase)
    double utilization[MAX_LEVELS];       // Task utilization
    int allocated_core;                   // Stores the core number of the core it is allocated to
}Tasks;

// ---------------------------------
// TASKSET INFO STRUCTURE DEFINITION
// ---------------------------------

typedef struct { 
    double hi_crit_util;                  // Total utilization of all HI criticality tasks (at their own criticality level) in the workload
    double lo_crit_util;                  // Total utilization of all LO criticality tasks (at their own criticality level) in the workload
    double lpd_hi_crit_util;              // Total utilization of all HI criticality low period tasks (at their own criticality level) in the workload
    double lpd_lo_crit_util;              // Total utilization of all LO criticality low period tasks (at their own criticality level) in the workload
}Taskset_info;

// ------------------------------
// RUN QUEUE STRUCTURE DEFINITION
// ------------------------------

// Run queue NODE structure
struct _node {                         
    Jobs *job;                            // Job structure pointer
    struct _node *prev;                   // Pointer to the previous node
    struct _node *next;                   // Pointer to the next node
};

typedef struct _node RQ_NODE;

// Run queue HEAD structure
typedef struct {                      
    int size;                             // Number of nodes in the run queue
    double parameter;                     // Stores maximum deadline of all jobs in queue (deadline of last job in EDF ordered queue) 
    RQ_NODE *head_node;                   // Pointer to run queue head node      
} RQ_HEAD;

// -----------------------------------
// DECISION POINT STRUCTURE DEFINITION
// -----------------------------------

typedef struct {                                      
    double decision_time;                 // Time at which the next scheduling decision point occurs
    unsigned int event:5;                 // Event causing the decision point: job arrival/job termination/criticality level change (wcet exceeded)/job overrun/core wakeup                           
} Decision_point;

// -------------------------
// CORE STRUCTURE DEFINITION
// -------------------------

typedef struct {

    // Allocation parameters
    int core_no;                          // To identify a given core
    double utilization;                   // Total utilization of the core
    double remaining_capacity;            // To determine how many more tasks can be allocated to this core (bin packing capacity)
    int tasks_alloc_count;                // Number of tasks allocated to the core       
    int tasks_alloc_ids[MAX_TASKS];       // An array that holds the task_no of tasks allocated to the given core
    int threshold_criticality;            // Threshold criticality of the core; beyond this level all low-criticality tasks discarded
    int core_criticality;                 // Criticality level of this core

    // DP & Slack scheduling parameters  
    double slack_available[MAX_LEVELS];   // Slack available with the core (at each criticality level) -- DP
    int core_type;                        // To indicate whether a core is SHUTDOWNABLE or NON-SHUTDOWNABLE 
    int status;                           // To indicate whether a core is currently ACTIVE or SHUTDOWN (power-saving mode)
    double wakeup_time;                   // Wakeup time for cores which have been SHUTDOWN, set to -1 for active cores

    // DVFS parameters
    // double x;                          // Deadline shortening factor (x), determined by the EDF-VD offline preprocessing phase
    // double x_ub;                       // Upper bound on deadline shortening factor (x)
    // double x_lb;                       // Lower bound on deadline shortening factor (x)
    double operating_frequency;           // Frequency at which this core is operating

    // Runtime Scheduler parameters
    Decision_point *decision_point;       // Decision point structure consisting of event causing the decision point and exact time at which it occurs
    RQ_HEAD *qhead;                       // Pointer to local run queue head
    Jobs *curr_exe_job;                   // Stores the structure of job currently executing on this core
    Jobs *preempted_job;                  // Stores the structure of job preempted from this core
    double idle_time;                     // To record the system idle time in one hyperperiod
} Cores;

// =====================
// FUNCTION DECLARATIONS
// =====================

// -----------
// READ INPUT
// -----------

// Fetch input task parameters from the given file, store parameters in the task structure array
void fetch_task_parameters (FILE* fptr, Tasks *tasks_arr, int num_tasks, int max_criticality);            

// -------------------------------------------------------------------------------------------------------------------------
// QUICK SORT TASKS IN DECREASING ORDER OF THEIR CRITICALITY LEVELS AND UTILIZATIONS (at highest level defined for the task)
// -------------------------------------------------------------------------------------------------------------------------

// Helper function to swap to elements
void swap (Tasks* A, Tasks* B);        

// Partitioning function for sorting task array in decreasing order of their criticalities and utilizations                                
int partition_array (Tasks *task_ptr, int low, int high);    

// Quick sort driver function: sorts array in decreasing order of task criticality and utilization
void quick_sort (Tasks *task_ptr, int low, int high);                             

// --------------------------------------------------------------------------------------------------------------------
// GET TASK UTILIZATIONS INFO FOR GIVEN TASK SET, DETERMINE HI, LO UTILIZATIONS FOR (I) LOW PERIOD TASKS (II) ALL TASKS
// --------------------------------------------------------------------------------------------------------------------

// Get task utilization stats for the given input taskset
void get_taskset_info (Tasks *tasks_arr, int num_tasks, Taskset_info* tasks_info, int hi_level_threshold);

// ---------------------------------------------------------------------------------------------------
// DETERMINE MINIMUM NUMBER OF CORES REQUIRED FOR ALLOCATION AS PER THE MC FEASIBILITY CONDITION CHECK 
// ---------------------------------------------------------------------------------------------------

// Determine the minimum number of cores required for allocation as per MC Feasibility condition 
// (i.e. Total utilization of all tasks at any given level < 1)
int get_min_cores_reqd (Tasks *tasks_arr, int num_tasks, int max_criticality);

// --------------------------------------
// EDF-VD OFFLINE PREPROCESSING FUNCTIONS
// --------------------------------------

// Compute total utilization of tasks when executed at a given criticality level k
double calculate_utilization_ulk (Tasks *tasks_arr, int num_tasks, int lower_limit, int upper_limit, int core_no, int new_task_no);

// Compute total utilization of tasks when executed at their own criticality levels
double calculate_utilization_ull (Tasks *tasks_arr, int num_tasks, int lower_limit, int upper_limit, int core_no, int new_task_no);

// Check if the EDF-VD Schedulability condition holds for given core, assuming we add the new task to it 
int edfvd_schedulability_check (Tasks* tasks_arr, int num_tasks, int max_criticality, int core_no, int new_task_no);

// ---------------------------------
// OFFLINE TASK ALLOCATION FUNCTIONS
// ---------------------------------

// Initialize all core structure parameters for allocation
void initialize_cores_offline (Cores *core, int max_criticality);

// Reset remaining core capacities when moving on to allocation of tasks of next (lower) criticality level --> to maintain MCS feasibility condition 
void reset_core_capacities (Cores *core, int num_cores, Tasks *tasks_arr, int criticality, int task_array_idx);

// Find the worst-fitting core (i.e. with maximum remaining capacity) that can accommodate the given task
int get_worst_fit_core_idx (Cores *core, int num_cores, Tasks *tasks_arr, int num_tasks, int task_idx, int max_criticality);

// Find the first-fitting core (i.e. first core with remaining capacity > task utilization) that can accommodate the given task
int get_first_fit_core_idx (Cores *core, int num_cores, Tasks *tasks_arr, int num_tasks, int task_idx, int max_criticality);

// Allocate the given task to the core with index obtained from the allocation algorithm 
// Update task and core structure parameters accordingly
void allocate_task_to_core (Cores *core, Tasks *tasks_arr, int core_idx, int task_idx);

// Offline task allocation driver code
int offline_task_allocator (Cores *core, Tasks *tasks_arr, int num_tasks, int min_cores, int max_criticality);

// -----------------------------
// SUPER-HYPERPERIOD CALCULATION
// -----------------------------

// Helper function to calculate the Highest Common Factor (HCF)
int hcf (int n1, int n2);

// Calculate superhyperperiod of the entire input taskset
int calculate_superhyperperiod (Tasks *task_ptr, int num_tasks);

// ---------------------------------------------------------
// MERGE SORT RUN QUEUE IN INCREASING ORDER OF JOB DEADLINES
// ---------------------------------------------------------

// Partition the queue to be sorted into smaller queues (approx. halves)
RQ_NODE *partition (RQ_NODE *head);

// Merge the smaller queues in order of decreasing job deadlines 
RQ_NODE *merge (RQ_NODE *first, RQ_NODE *second);

// Rearrange queue wrt updated job deadlines - (Merge sort driver function)
RQ_NODE *merge_sort (RQ_NODE *primary_head);

// -----------------------------------------------------------------------
// RUN-TIME SCHEDULING FUNCTIONS (SCHEDULING ALGORITHM: partitioned-EDFVD)
// -----------------------------------------------------------------------

// Create an EMPTY run queue
RQ_HEAD *create_run_queue ();

// Determine the threshold criticality level, all tasks with criticality level below this must be DISCARDED
int accept_above_criticality_level (int level, int threshold_criticality);

// Determine the next job arrival instance in each task set and returns the (minimum) arrival time of the next job
double get_next_job_arrival (Tasks *task_ptr, int task_array_idx, double timecount);

// Determine the next scheduling decision point = min {next decision points in all cores}
// Decision points: 1. Arrival 2. Current job termination 3. Criticality level change due to wcet budget overrun at current level 4. Overrun 5. Core Wakeup
double get_next_decision_point (Cores *core, int num_cores, Tasks *task_ptr, int num_tasks, double timecount, int hyperperiod);

// Run queue is updated by inserting all the ready jobs in the queue while maintaining the EDF order
void update_run_queue (RQ_HEAD *head, Jobs *j);

// Create job structure and set the parmeter values 
Jobs *create_job_structure (Tasks *task_arr, int task_array_idx, int threshold_criticality, int core_no, double timecount);

// Add the jobs to run queue if core is ACTIVE; add the job to pending request queue if core is SHUTDOWN
void add_ready_jobs (Cores *core, RQ_HEAD **dhead, RQ_HEAD *prhead, Tasks *task_arr, int num_tasks, double timecount);

// Schedule next job by removing a job node from head of the run queue, returning the job struct to the runtime scheduler
Jobs* schedule_next_job (RQ_HEAD *head);

// Delete a particular job structure from the run queue
void delete_job_from_queue (RQ_HEAD *head, Jobs *job);

// Scan the run queue and discards jobs below acceptable criticality level --> when criticality level/mode is upgraded
void discard_below_criticality_level (RQ_HEAD *head, RQ_HEAD **dhead, int level);

// Get task array index corresponding to the task number specified
int get_task_array_index (Tasks *task_arr, int num_tasks, int task_no);

// Update job deadlines (wrt which we are ordering the run queue) - reset to original deadlines on mode change
void update_sched_deadlines (RQ_HEAD *head, Tasks *task_arr, int num_tasks);

// Runtime scheduler driver code
void run_scheduler_loop (Cores *core, int num_cores, Tasks *task_arr, int num_tasks, int hyperperiod, int max_criticality);

// ---------------------------
// SLACK CALCULATION FUNCTIONS
// ---------------------------

// Copies all non-DISCARDED jobs present in the run queue to a dummy queue
void copy_jobs_to_dummy_queue (RQ_HEAD *head, RQ_HEAD *dummy_head, int threshold_criticality, int level);

// Anticipates jobs arriving before the specified max_arrival_time and adds them to the dummy queue in EDF order
void add_anticipated_arrivals (RQ_HEAD *dummy_head, double max_arrival_time, Tasks *task_ptr, int num_tasks, int threshold_criticality, int level, int core_no, int timecount);

// Slack calculation (using Dynamic Procrastination): 
// Slack = (latest time by which run queue jobs must start executing in order to guarantee completion by deadline) - (window time consumed by the anticipated jobs)
double calculate_slack_available (RQ_HEAD *dummy_head, double latest_arrival, double max_deadline, int timecount, int level);

// --------------------------------
// DYNAMIC PROCRASTINATION FUNCTION
// --------------------------------

// Calculates the maximum available slack for given core to find its maximum SHUTDOWN interval
void get_dynamic_procrastination_slack (Cores *core, int core_idx, Tasks *task_arr, int num_tasks, double next_job_deadline, int max_criticality, int current_level, int hyperperiod, double current_time);

// -----------------------
// DISCARDED JOB SCHEDULER 
// -----------------------

// Schedules discarded job if enough slack is available for it to execute
void schedule_discarded_job (RQ_HEAD *head, RQ_HEAD **dhead, Tasks *task_ptr, int num_tasks, int threshold_criticality, int max_criticality, int current_level, int core_no, int hyperperiod, double timecount);

// -----------------
// HELPER FUNCTIONS
// -----------------

// Helper function to print sorted task array
void print_sorted_array (Tasks *task_ptr, int num_tasks); 

// Helper funtion to print the taskset information for the given workload
void print_taskset_info (Taskset_info* tasks_info);

// Helper funtion to free all the wcet mallocs
void free_wcet_mallocs (Tasks *task_ptr, int num_tasks);

// Helper function to print task allocations
void print_task_allocations (Cores *core, int num_cores);

// Helper function to find the modulo of two floating point numbers
double find_modulo (double a, double b);

// Helper function to print run queue
void print_run_queue (RQ_HEAD *head);

// Helper function to copy job structure from source pointer to destination pointer
void copy_job_structure (Jobs *dest, Jobs *src);

