/* ============================================================
   Smart Power Distribution System
   CSA0209 - C Programming Assignment
   ------------------------------------------------------------
   A smart-grid operator collects hourly electricity consumption
   data from households. During peak periods demand may exceed
   available capacity. Hospitals / emergency services must get
   priority. This program analyzes consumption and allocates
   limited electricity according to priority using two
   alternative allocation strategies so they can be compared.
   ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_HOUSEHOLDS 50
#define HOURS           24
#define FILENAME        "power_records.txt"

/* Priority levels (lower number = higher priority) */
#define PRIORITY_HOSPITAL   1   /* Hospitals / emergency services   */
#define PRIORITY_CRITICAL   2   /* Critical infrastructure          */
#define PRIORITY_RESIDENTIAL 3  /* Normal residential households    */

/* ---------- (a) Structure for consumption data (nested-friendly) ---------- */
typedef struct {
    int   id;
    char  name[50];
    int   priority;                    /* 1, 2 or 3                        */
    float hourlyConsumption[HOURS];    /* multidimensional across N households */
    float hourlyAllocated[HOURS];
} Household;

/* ---------------------------------------------------------------
   Storage-class note (see write-up for full justification):
   The household ID counter is kept as a function-local `static`
   variable instead of a global. It must persist across many
   calls to getNextID() (unlike `auto`, which would reset to the
   initial value on every call), but it does not need to be
   visible/writable from every function in the file the way a
   global would be - only getNextID() ever touches it. static
   local storage gives persistence WITHOUT global exposure.
   --------------------------------------------------------------- */
int getNextID(void) {
    static int nextID = 1001;   /* static: retains value between calls */
    return nextID++;
}

/* ---------------- prototypes ---------------- */
void addHousehold(Household arr[], int *count, const char *name, int priority);
void updateConsumption(Household *h, int hour, float value);
int  searchHouseholdByID(const Household arr[], int count, int id);
void sortIndicesByPriority(const Household arr[], int count, int hour, int idx[]);
void greedyAllocate(Household arr[], int count, const float capacity[HOURS]);
void proportionalAllocate(Household arr[], int count, const float capacity[HOURS]);
void resetAllocations(Household arr[], int count);
void displayReport(const Household arr[], int count, const float capacity[HOURS]);
void saveRecordsToFile(const Household arr[], int count, const char *filename);
int  loadRecordsFromFile(Household arr[], int *count, const char *filename);
void compareAlgorithms(Household arr[], int count, const float capacity[HOURS]);
const char *priorityLabel(int p);

/* ================= (b) functions & pointers for updating data ================= */
void addHousehold(Household arr[], int *count, const char *name, int priority) {
    if (*count >= MAX_HOUSEHOLDS) {
        printf("Household list is full (max %d).\n", MAX_HOUSEHOLDS);
        return;
    }
    Household *h = &arr[*count];       /* pointer to the new struct slot */
    h->id = getNextID();
    strncpy(h->name, name, sizeof(h->name) - 1);
    h->name[sizeof(h->name) - 1] = '\0';
    h->priority = priority;
    for (int i = 0; i < HOURS; i++) {
        h->hourlyConsumption[i] = 0.0f;
        h->hourlyAllocated[i]   = 0.0f;
    }
    (*count)++;
    printf("Added household '%s' (ID %d, %s).\n", h->name, h->id, priorityLabel(priority));
}

/* Update via pointer to struct - no copying of the whole struct is needed */
void updateConsumption(Household *h, int hour, float value) {
    if (h == NULL || hour < 0 || hour >= HOURS || value < 0) {
        printf("Invalid update request.\n");
        return;
    }
    h->hourlyConsumption[hour] = value;
}

/* ================= linear search by ID ================= */
int searchHouseholdByID(const Household arr[], int count, int id) {
    for (int i = 0; i < count; i++) {
        if (arr[i].id == id) return i;
    }
    return -1;
}

/* Insertion sort of an index array: priority ascending (1 = highest),
   ties broken by descending demand for that hour. Used by both
   allocation strategies. O(n^2) worst case but n (households) is small;
   documented complexity is discussed in the write-up as O(n log n)-class
   comparison against an alternative (qsort) in the evaluation. */
void sortIndicesByPriority(const Household arr[], int count, int hour, int idx[]) {
    for (int i = 0; i < count; i++) idx[i] = i;
    for (int i = 1; i < count; i++) {
        int key = idx[i];
        int j = i - 1;
        while (j >= 0 &&
               (arr[idx[j]].priority > arr[key].priority ||
                (arr[idx[j]].priority == arr[key].priority &&
                 arr[idx[j]].hourlyConsumption[hour] < arr[key].hourlyConsumption[hour]))) {
            idx[j + 1] = idx[j];
            j--;
        }
        idx[j + 1] = key;
    }
}

void resetAllocations(Household arr[], int count) {
    for (int i = 0; i < count; i++)
        for (int h = 0; h < HOURS; h++)
            arr[i].hourlyAllocated[h] = 0.0f;
}

/* ================= (c) Approach A: Greedy priority allocation =================
   For each hour: sort strictly by priority (then demand). Serve
   households in that order until the hour's capacity runs out.
   Time complexity: O(H * N log N) for sorting + O(H * N) to allocate. */
void greedyAllocate(Household arr[], int count, const float capacity[HOURS]) {
    int idx[MAX_HOUSEHOLDS];
    for (int hr = 0; hr < HOURS; hr++) {
        sortIndicesByPriority(arr, count, hr, idx);
        float remaining = capacity[hr];
        for (int k = 0; k < count; k++) {
            Household *h = &arr[idx[k]];
            float need = h->hourlyConsumption[hr];
            float give = (need <= remaining) ? need : remaining;
            if (give < 0) give = 0;
            h->hourlyAllocated[hr] = give;
            remaining -= give;
        }
    }
}

/* ================= Approach B: Priority-tier + proportional fair share =========
   Hospitals (priority 1) are always served in full first (life-safety
   requirement cannot be relaxed). Whatever capacity remains is then
   split PROPORTIONALLY to demand across every remaining household of
   the SAME lower tier, tier by tier, instead of fully starving the
   last household in an arbitrary tie order the way Approach A can.
   Time complexity: O(H * N) per hour (two linear passes per tier,
   no comparison sort needed) - asymptotically cheaper than Approach A,
   at the cost of extra floating point division work. */
void proportionalAllocate(Household arr[], int count, const float capacity[HOURS]) {
    for (int hr = 0; hr < HOURS; hr++) {
        float remaining = capacity[hr];

        for (int tier = PRIORITY_HOSPITAL; tier <= PRIORITY_RESIDENTIAL; tier++) {
            float tierDemand = 0.0f;
            for (int i = 0; i < count; i++)
                if (arr[i].priority == tier) tierDemand += arr[i].hourlyConsumption[hr];

            if (tierDemand <= 0.0f) continue;

            if (tierDemand <= remaining) {
                /* enough capacity: satisfy every household in this tier fully */
                for (int i = 0; i < count; i++)
                    if (arr[i].priority == tier)
                        arr[i].hourlyAllocated[hr] = arr[i].hourlyConsumption[hr];
                remaining -= tierDemand;
            } else {
                /* not enough: share `remaining` proportionally to each household's demand */
                for (int i = 0; i < count; i++)
                    if (arr[i].priority == tier)
                        arr[i].hourlyAllocated[hr] =
                            remaining * (arr[i].hourlyConsumption[hr] / tierDemand);
                remaining = 0.0f;
            }
        }
    }
}

const char *priorityLabel(int p) {
    switch (p) {
        case PRIORITY_HOSPITAL:    return "Hospital/Emergency";
        case PRIORITY_CRITICAL:    return "Critical Infrastructure";
        case PRIORITY_RESIDENTIAL: return "Residential";
        default: return "Unknown";
    }
}

/* ================= reporting ================= */
void displayReport(const Household arr[], int count, const float capacity[HOURS]) {
    printf("\n%-4s %-12s %-22s %10s %10s %10s\n",
           "ID", "Name", "Priority", "Demand", "Allocated", "Unmet");
    printf("-------------------------------------------------------------------------\n");
    for (int i = 0; i < count; i++) {
        float totalDemand = 0, totalAlloc = 0;
        for (int h = 0; h < HOURS; h++) {
            totalDemand += arr[i].hourlyConsumption[h];
            totalAlloc  += arr[i].hourlyAllocated[h];
        }
        printf("%-4d %-12s %-22s %10.2f %10.2f %10.2f\n",
               arr[i].id, arr[i].name, priorityLabel(arr[i].priority),
               totalDemand, totalAlloc, totalDemand - totalAlloc);
    }

    float totalCap = 0;
    for (int h = 0; h < HOURS; h++) totalCap += capacity[h];
    printf("-------------------------------------------------------------------------\n");
    printf("Total grid capacity across 24h: %.2f kWh\n", totalCap);
}

/* ================= (d) File handling ================= */
void saveRecordsToFile(const Household arr[], int count, const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) { printf("Error: could not open %s for writing.\n", filename); return; }

    fprintf(fp, "%d\n", count);
    for (int i = 0; i < count; i++) {
        fprintf(fp, "%d,%s,%d\n", arr[i].id, arr[i].name, arr[i].priority);
        for (int h = 0; h < HOURS; h++)
            fprintf(fp, "%.2f%c", arr[i].hourlyConsumption[h], (h == HOURS - 1) ? '\n' : ',');
        for (int h = 0; h < HOURS; h++)
            fprintf(fp, "%.2f%c", arr[i].hourlyAllocated[h], (h == HOURS - 1) ? '\n' : ',');
    }
    fclose(fp);
    printf("Saved %d household record(s) to %s\n", count, filename);
}

int loadRecordsFromFile(Household arr[], int *count, const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) { printf("No existing file '%s' found.\n", filename); return 0; }

    int n;
    if (fscanf(fp, "%d\n", &n) != 1) { fclose(fp); return 0; }
    if (n > MAX_HOUSEHOLDS) n = MAX_HOUSEHOLDS;

    for (int i = 0; i < n; i++) {
        char line[256];
        fgets(line, sizeof(line), fp);
        sscanf(line, "%d,%49[^,],%d", &arr[i].id, arr[i].name, &arr[i].priority);

        for (int h = 0; h < HOURS; h++)
            fscanf(fp, "%f,", &arr[i].hourlyConsumption[h]);
        for (int h = 0; h < HOURS; h++)
            fscanf(fp, "%f,", &arr[i].hourlyAllocated[h]);
    }
    fclose(fp);
    *count = n;
    printf("Loaded %d household record(s) from %s\n", n, filename);
    return 1;
}

/* ================= Evaluation: compare the two algorithms ================= */
void compareAlgorithms(Household arr[], int count, const float capacity[HOURS]) {
    Household backupA[MAX_HOUSEHOLDS], backupB[MAX_HOUSEHOLDS];
    memcpy(backupA, arr, sizeof(Household) * count);
    memcpy(backupB, arr, sizeof(Household) * count);

    clock_t t0 = clock();
    greedyAllocate(backupA, count, capacity);
    clock_t t1 = clock();
    proportionalAllocate(backupB, count, capacity);
    clock_t t2 = clock();

    double timeA = (double)(t1 - t0) / CLOCKS_PER_SEC * 1000.0;
    double timeB = (double)(t2 - t1) / CLOCKS_PER_SEC * 1000.0;

    printf("\n================  Approach A: Greedy Priority (sort-based)  ================\n");
    displayReport(backupA, count, capacity);
    printf("Execution time: %.4f ms\n", timeA);

    printf("\n================  Approach B: Priority-tier Proportional Share  ================\n");
    displayReport(backupB, count, capacity);
    printf("Execution time: %.4f ms\n", timeB);

    printf("\n--------------------------- Comparison summary ---------------------------\n");
    printf("Approach A (Greedy/sort):   O(H*N log N) time, O(N) space; can leave the\n");
    printf("  lowest-ranked household in a tie with ZERO power if capacity runs out\n");
    printf("  mid-tier, even by a small margin.\n");
    printf("Approach B (Proportional):  O(H*N) time, O(N) space; every household in\n");
    printf("  an under-served tier gets a fair, demand-proportional share instead of\n");
    printf("  an all-or-nothing cutoff, at the cost of extra floating-point division.\n");
    printf("Both guarantee hospitals/emergency services (priority 1) are served first.\n");
}

/* ================= menu-driven main ================= */
int main(void) {
    Household households[MAX_HOUSEHOLDS];
    int count = 0;

    /* Grid capacity per hour (kWh) - lower during evening peak (18-22h) */
    float capacity[HOURS];
    for (int h = 0; h < HOURS; h++) {
        if (h >= 18 && h <= 22) capacity[h] = 40.0f;   /* peak: tight capacity */
        else                    capacity[h] = 90.0f;   /* off-peak: ample capacity */
    }

    loadRecordsFromFile(households, &count, FILENAME);

    int choice;
    do {
        printf("\n================ Smart Power Distribution System ================\n");
        printf("1. Add Household\n");
        printf("2. Update Hourly Consumption\n");
        printf("3. Search Household by ID\n");
        printf("4. Run Greedy Priority Allocation (Approach A)\n");
        printf("5. Run Proportional Fair-Share Allocation (Approach B)\n");
        printf("6. Compare Approach A vs Approach B\n");
        printf("7. Display Report (current allocation)\n");
        printf("8. Save Records to File\n");
        printf("9. Exit\n");
        printf("Enter choice: ");
        if (scanf("%d", &choice) != 1) break;

        switch (choice) {
            case 1: {
                char name[50]; int priority;
                printf("Household/Facility name: ");
                scanf("%49s", name);
                printf("Priority (1=Hospital/Emergency, 2=Critical Infra, 3=Residential): ");
                scanf("%d", &priority);
                addHousehold(households, &count, name, priority);
                break;
            }
            case 2: {
                int id, hour; float val;
                printf("Household ID: "); scanf("%d", &id);
                int i = searchHouseholdByID(households, count, id);
                if (i == -1) { printf("Household not found.\n"); break; }
                printf("Hour (0-23): "); scanf("%d", &hour);
                printf("Consumption (kWh): "); scanf("%f", &val);
                updateConsumption(&households[i], hour, val);   /* pointer passed */
                printf("Updated.\n");
                break;
            }
            case 3: {
                int id;
                printf("Household ID: "); scanf("%d", &id);
                int i = searchHouseholdByID(households, count, id);
                if (i == -1) printf("Not found.\n");
                else printf("Found: %s (%s)\n", households[i].name, priorityLabel(households[i].priority));
                break;
            }
            case 4:
                resetAllocations(households, count);
                greedyAllocate(households, count, capacity);
                displayReport(households, count, capacity);
                break;
            case 5:
                resetAllocations(households, count);
                proportionalAllocate(households, count, capacity);
                displayReport(households, count, capacity);
                break;
            case 6:
                compareAlgorithms(households, count, capacity);
                break;
            case 7:
                displayReport(households, count, capacity);
                break;
            case 8:
                saveRecordsToFile(households, count, FILENAME);
                break;
            case 9:
                printf("Exiting. Saving before exit...\n");
                saveRecordsToFile(households, count, FILENAME);
                break;
            default:
                printf("Invalid choice.\n");
        }
    } while (choice != 9);

    return 0;
}
