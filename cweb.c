/*
 * cweb.c
 *
 * A meeting scheduler with a web interface, allowing users to schedule meetings
 * and reservations over four weeks (Monday–Thursday, 9:00–16:30, with a 12:00–13:00 break).
 * Users can add meetings, reserve time slots, view schedules, export to ICS (calendar format),
 * and clear the schedule via a web browser at http://localhost:8888.
 *
 * Compile with:
 *     gcc cweb.c -o cweb -lmicrohttpd
 *
 * Run:
 *     ./cweb
 *
 * Open http://localhost:8888 in your browser to use it.
 *
 * This version includes a fix for fortnightly meetings (they now occur every two weeks,
 * e.g., Week 1 and Week 3) and detailed comments for beginners learning C.
 */

 #include <microhttpd.h>  // Library for creating a web server
 #include <stdio.h>       // For printing and file operations
 #include <stdlib.h>      // For memory allocation (malloc) and random numbers (rand)
 #include <string.h>      // For string operations (strcmp, strcpy)
 #include <stdbool.h>     // For true/false values
 #include <time.h>        // For seeding random numbers and date handling
 
 // Define the port where the web server will listen (like a radio frequency for web requests)
 #define PORT 8888
 
 // -------------------------
 // SCHEDULER DATA AND SETUP
 // -------------------------
 
 // Constants to set limits and options
 #define MAX_DAYS 4         // We only schedule Monday to Thursday
 #define MAX_WEEKS 4        // Schedule covers 4 weeks
 #define MAX_SLOTS 14       // 14 time slots per day (30-min intervals, 9:00–16:30, excluding break)
 #define MAX_MEETINGS 100   // Max number of scheduled meetings
 #define MAX_RESERVATIONS 50 // Max number of reserved time slots
 #define MAX_STR 64         // Max length for strings (e.g., meeting names)
 
 // Lists of valid days, times, and options
 const char *DAYS[MAX_DAYS] = {"Monday", "Tuesday", "Wednesday", "Thursday"};
 const char *TIME_SLOTS[MAX_SLOTS] = {
     "09:00", "09:30", "10:00", "10:30", "11:00", "11:30",
     "13:00", "13:30", "14:00", "14:30", "15:00", "15:30",
     "16:00", "16:30"
 };
 const char *BREAK_SLOTS[2] = {"12:00", "12:30"}; // Times reserved for lunch break
 const char *FREQUENCIES[4] = {"weekly", "fortnightly", "third_week", "monthly"};
 const int DURATIONS[3] = {1, 2, 3}; // Duration in 30-min slots: 1=30 min, 2=60 min, 3=90 min
 
 // Struct to hold meeting details (like a form for a meeting request)
 typedef struct {
     char name[MAX_STR];          // Meeting name (e.g., "Team Sync")
     char type[MAX_STR];          // Type (e.g., "One-to-one")
     int duration;               // Duration in slots (1, 2, or 3)
     int preferred_hours[8];     // Preferred start times (slot indices, -1 ends list)
     char fixed_day[MAX_STR];    // Optional fixed day (e.g., "Monday")
     char fixed_time[MAX_STR];   // Optional fixed time (e.g., "10:00")
     char frequency[MAX_STR];    // How often it repeats (e.g., "weekly")
 } Meeting;
 
 // Struct for a reserved time slot (like booking a room)
 typedef struct {
     char day[MAX_STR];          // Day of reservation (e.g., "Tuesday")
     char start_time[MAX_STR];   // Start time (e.g., "14:00")
     int duration;              // Duration in slots
 } Reservation;
 
 // Struct for a scheduled meeting (what actually goes on the calendar)
 typedef struct {
     int week;                   // Week number (0 to 3, for Week 1 to 4)
     int day;                    // Day index (0=Monday, ..., 3=Thursday)
     int start_time;             // Time slot index (0=9:00, ..., 13=16:30)
     char name[MAX_STR];         // Meeting name
     char type[MAX_STR];         // Meeting type
     int duration;              // Duration in slots
     char frequency[MAX_STR];    // Frequency
 } ScheduleEntry;
 
 // Main scheduler struct to hold all data (like a big organizer)
 typedef struct {
     ScheduleEntry schedule[MAX_MEETINGS * MAX_WEEKS]; // Array of scheduled meetings
     int schedule_count;                              // How many meetings are scheduled
     Reservation reservations[MAX_RESERVATIONS];      // Array of reservations
     int reservation_count;                          // How many reservations exist
     double total_hours[MAX_DAYS];                   // Total hours booked per day
     double meeting_hours[MAX_DAYS];                 // Meeting hours per day
     bool blocked_slots[MAX_WEEKS][MAX_DAYS][MAX_SLOTS]; // Tracks booked slots
 } MeetingScheduler;
 
 // -------------------------
 // HELPER FUNCTIONS
 // -------------------------
 
 // Finds the index of a time in TIME_SLOTS (e.g., "09:30" -> 1)
 int find_slot_index(const char *time) {
     for (int i = 0; i < MAX_SLOTS; i++) {
         if (strcmp(TIME_SLOTS[i], time) == 0) // strcmp returns 0 if strings match
             return i;
     }
     return -1; // Return -1 if time not found
 }
 
 // Finds the index of a day in DAYS (e.g., "Tuesday" -> 1)
 int find_day_index(const char *day) {
     for (int i = 0; i < MAX_DAYS; i++) {
         if (strcmp(DAYS[i], day) == 0)
             return i;
     }
     return -1; // Return -1 if day not found
 }
 
 // Checks if a time is during the lunch break (12:00 or 12:30)
 bool is_break_slot(const char *time) {
     return (strcmp(time, BREAK_SLOTS[0]) == 0 || strcmp(time, BREAK_SLOTS[1]) == 0);
 }
 
 // Converts a slot index to hours (e.g., slot 2=10:00 -> 10.0, slot 3=10:30 -> 10.5)
 double slot_to_hour(int slot_idx) {
     int hour = slot_idx / 2 + 9; // Each slot is 30 min, starting at 9:00
     int minute = (slot_idx % 2) * 30; // Odd slots add 30 min
     if (slot_idx >= 6) // After 12:00 (slot 6), skip break hour
         hour++;
     return hour + minute / 60.0; // Convert to decimal (e.g., 10:30 = 10.5)
 }
 
 // Calculates end time from start slot and duration (e.g., start=2, duration=2 -> "11:00")
 void compute_end_time(int start_idx, int duration_slots, char *end_time) {
     double start_hour = slot_to_hour(start_idx); // Get start time in hours
     double end_hour = start_hour + duration_slots * 0.5; // Add duration (0.5 hr/slot)
     int end_h = (int) end_hour; // Whole hours
     int end_m = (int)((end_hour - end_h) * 60); // Minutes
     snprintf(end_time, 8, "%02d:%02d", end_h, end_m); // Format as "HH:MM"
 }
 
 // -------------------------
 // SCHEDULER FUNCTIONS
 // -------------------------
 
 // Initializes the scheduler to a clean state
 void init_scheduler(MeetingScheduler *scheduler) {
     scheduler->schedule_count = 0; // No meetings yet
     scheduler->reservation_count = 0; // No reservations yet
     memset(scheduler->total_hours, 0, sizeof(scheduler->total_hours)); // Clear hours
     memset(scheduler->meeting_hours, 0, sizeof(scheduler->meeting_hours)); // Clear meeting hours
     memset(scheduler->blocked_slots, 0, sizeof(scheduler->blocked_slots)); // Clear booked slots
 }
 
 // Reserves a time slot across all weeks (e.g., for external commitments)
 bool reserve_slot(MeetingScheduler *scheduler, const char *day, const char *start_time, int duration_minutes) {
     // Get day and time indices
     int day_idx = find_day_index(day);
     int start_idx = find_slot_index(start_time);
     
     // Check if inputs are valid
     if (day_idx == -1 || start_idx == -1 ||
         (duration_minutes % 30 != 0) || duration_minutes < 30 || duration_minutes > 90) {
         return false; // Invalid day, time, or duration (must be 30, 60, or 90 min)
     }
     
     int duration_slots = duration_minutes / 30; // Convert minutes to slots
     double start_hour = slot_to_hour(start_idx); // Start time in hours
     double end_hour = start_hour + duration_slots * 0.5; // End time in hours
     
     // Check if slot is after 5:00 PM or during break
     if (end_hour > 17.0 || is_break_slot(start_time))
         return false;
     
     // Check if any slot in duration is invalid or a break
     for (int i = 0; i < duration_slots; i++) {
         int slot_idx = start_idx + i;
         if (slot_idx >= MAX_SLOTS || is_break_slot(TIME_SLOTS[slot_idx]))
             return false;
     }
     
     // Check if slots are free in all weeks
     for (int week = 0; week < MAX_WEEKS; week++) {
         for (int i = 0; i < duration_slots; i++) {
             int slot_idx = start_idx + i;
             if (scheduler->blocked_slots[week][day_idx][slot_idx])
                 return false; // Slot already booked
             scheduler->blocked_slots[week][day_idx][slot_idx] = true; // Book slot
         }
     }
     
     // Add reservation to the list
     Reservation *res = &scheduler->reservations[scheduler->reservation_count++];
     strncpy(res->day, day, MAX_STR); // Copy day
     strncpy(res->start_time, start_time, MAX_STR); // Copy time
     res->duration = duration_slots; // Set duration
     scheduler->total_hours[day_idx] += duration_slots * 0.5 * MAX_WEEKS; // Update hours
     return true; // Success
 }
 
 // Checks if a time slot is free for a given week, day, and duration
 bool is_valid_slot(MeetingScheduler *scheduler, int week, int day_idx, int start_idx, int duration_slots) {
     // Check if start time is valid
     if (start_idx < 0 || start_idx >= MAX_SLOTS || is_break_slot(TIME_SLOTS[start_idx]))
         return false;
     
     double start_hour = slot_to_hour(start_idx); // Start time in hours
     double end_hour = start_hour + duration_slots * 0.5; // End time
     
     // Check if meeting ends after 5:00 PM
     if (end_hour > 17.0)
         return false;
     
     // Check each slot in duration
     for (int i = 0; i < duration_slots; i++) {
         int slot_idx = start_idx + i;
         if (slot_idx >= MAX_SLOTS || is_break_slot(TIME_SLOTS[slot_idx]) ||
             scheduler->blocked_slots[week][day_idx][slot_idx])
             return false; // Slot is booked or invalid
     }
     return true; // Slot is free
 }
 
 // Adds a meeting to the schedule, respecting constraints
// Adds a meeting to the schedule, respecting constraints
bool add_meeting(MeetingScheduler *scheduler, Meeting *meeting) {
    // Get duration and number of occurrences
    int duration_slots = meeting->duration; // Number of 30-min slots
    int occurrences = (strcmp(meeting->frequency, "weekly") == 0) ? 4 : // Every week
                     (strcmp(meeting->frequency, "fortnightly") == 0) ? 2 : // Every 2 weeks
                     (strcmp(meeting->frequency, "third_week") == 0) ? 1 : 1; // Monthly or third_week
    int fixed_day_idx = meeting->fixed_day[0] ? find_day_index(meeting->fixed_day) : -1; // Fixed day or -1
    int fixed_time_idx = meeting->fixed_time[0] ? find_slot_index(meeting->fixed_time) : -1; // Fixed time or -1
    int assigned_weeks[MAX_WEEKS] = {0}; // Tracks used weeks
    int chosen_day = -1, chosen_time = -1; // Best day/time found

    // Sort days by total_hours (less busy first)
    int day_order[MAX_DAYS];
    for (int i = 0; i < MAX_DAYS; i++) day_order[i] = i; // Initialize: 0,1,2,3
    for (int i = 0; i < MAX_DAYS - 1; i++) {
        for (int j = 0; j < MAX_DAYS - i - 1; j++) {
            if (scheduler->total_hours[day_order[j]] > scheduler->total_hours[day_order[j + 1]]) {
                // Swap to sort least busy days first
                int temp = day_order[j];
                day_order[j] = day_order[j + 1];
                day_order[j + 1] = temp;
            }
        }
    }

    // Define valid fortnight pairs (Week 1 & 3, Week 2 & 4; 0-based: 0=Week 1)
    int fortnight_pairs[2][2] = {{0, 2}, {1, 3}};
    // Limit loop to 2 pairs for fortnightly, 1 for others
    int max_pairs = (strcmp(meeting->frequency, "fortnightly") == 0) ? 2 : 1;

    double min_hours = 1e9; // Track least busy day (big number to start)
    // Loop over fortnight pairs (or once for non-fortnightly)
    for (int p = 0; p < max_pairs; p++) {
        // Set day range: fixed day or all days
        int day_start = (fixed_day_idx >= 0) ? fixed_day_idx : 0;
        int day_end = (fixed_day_idx >= 0) ? fixed_day_idx + 1 : MAX_DAYS;
        for (int d = day_start; d < day_end; d++) {
            int day_idx = (fixed_day_idx >= 0) ? fixed_day_idx : day_order[d];
            // Skip days with too many meetings (>2.5 hr/week average)
            if (scheduler->meeting_hours[day_idx] / 4 > 2.5)
                continue;
            // Set time range: fixed time, preferred times, or all slots
            int time_start = (fixed_time_idx >= 0) ? fixed_time_idx : 0;
            int time_end = (fixed_time_idx >= 0) ? fixed_time_idx + 1 : MAX_SLOTS;
            if (meeting->preferred_hours[0] >= 0 && fixed_time_idx < 0) {
                time_end = 0;
                while (meeting->preferred_hours[time_end] >= 0 && time_end < 8)
                    time_end++; // Use preferred times
            }
            for (int t = time_start; t < time_end && t < MAX_SLOTS; t++) {
                // Pick time: fixed, preferred, or sequential
                int time_idx = (fixed_time_idx >= 0) ? fixed_time_idx :
                              (meeting->preferred_hours[0] >= 0 ? meeting->preferred_hours[t] : t);
                if (time_idx < 0 || time_idx >= MAX_SLOTS)
                    continue;
                int valid_weeks = 0;
                double avg_hours = 0;
                if (strcmp(meeting->frequency, "fortnightly") == 0) {
                    // Check fortnight pair (e.g., Week 1 & 3)
                    int week1 = fortnight_pairs[p][0];
                    int week2 = fortnight_pairs[p][1];
                    if (is_valid_slot(scheduler, week1, day_idx, time_idx, duration_slots) &&
                        is_valid_slot(scheduler, week2, day_idx, time_idx, duration_slots)) {
                        valid_weeks = 2; // Both weeks free
                        avg_hours = scheduler->total_hours[day_idx] + duration_slots * 0.5;
                    }
                } else {
                    // Check all weeks for other frequencies
                    for (int w = 0; w < MAX_WEEKS; w++) {
                        if (assigned_weeks[w])
                            continue;
                        if (is_valid_slot(scheduler, w, day_idx, time_idx, duration_slots)) {
                            valid_weeks++;
                            avg_hours += scheduler->total_hours[day_idx] + duration_slots * 0.5;
                        }
                        if (valid_weeks >= occurrences)
                            break;
                    }
                }
                // Pick least busy day/time
                if (valid_weeks >= occurrences && avg_hours / valid_weeks < min_hours) {
                    min_hours = avg_hours / valid_weeks;
                    chosen_day = day_idx;
                    chosen_time = time_idx;
                }
            }
        }
    }

    // If no valid slot found, fail
    if (chosen_day == -1 || chosen_time == -1)
        return false;

    // Schedule the meeting
    if (strcmp(meeting->frequency, "fortnightly") == 0) {
        // Find first valid fortnight pair
        int chosen_pair = -1;
        for (int p = 0; p < 2; p++) {
            int week1 = fortnight_pairs[p][0];
            int week2 = fortnight_pairs[p][1];
            if (is_valid_slot(scheduler, week1, chosen_day, chosen_time, duration_slots) &&
                is_valid_slot(scheduler, week2, chosen_day, chosen_time, duration_slots)) {
                chosen_pair = p;
                break;
            }
        }
        if (chosen_pair == -1)
            return false;

        // Add meeting to both weeks in the pair
        for (int i = 0; i < 2; i++) {
            int week = fortnight_pairs[chosen_pair][i];
            ScheduleEntry *entry = &scheduler->schedule[scheduler->schedule_count++]; // Next slot
            entry->week = week;
            entry->day = chosen_day;
            entry->start_time = chosen_time;
            strncpy(entry->name, meeting->name, MAX_STR); // Copy name
            strncpy(entry->type, meeting->type, MAX_STR); // Copy type
            entry->duration = duration_slots;
            strncpy(entry->frequency, meeting->frequency, MAX_STR); // Copy frequency
            scheduler->total_hours[chosen_day] += duration_slots * 0.5; // Add hours
            scheduler->meeting_hours[chosen_day] += duration_slots * 0.5; // Add meeting hours
            // Mark slots as booked
            for (int j = 0; j < duration_slots; j++) {
                scheduler->blocked_slots[week][chosen_day][chosen_time + j] = true;
            }
        }
    } else {
        // For weekly/monthly: pick random weeks
        int weeks[MAX_WEEKS] = {0, 1, 2, 3};
        // Shuffle weeks for variety
        for (int i = MAX_WEEKS - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            int temp = weeks[i];
            weeks[i] = weeks[j];
            weeks[j] = temp;
        }
        // Schedule each occurrence
        for (int occ = 0; occ < occurrences; occ++) {
            int week = -1;
            bool found = false;
            for (int w = 0; w < MAX_WEEKS; w++) {
                week = weeks[w];
                if (assigned_weeks[week])
                    continue;
                if (is_valid_slot(scheduler, week, chosen_day, chosen_time, duration_slots)) {
                    found = true;
                    break;
                }
            }
            if (!found)
                return false; // No free week
            ScheduleEntry *entry = &scheduler->schedule[scheduler->schedule_count++]; // Next slot
            entry->week = week;
            entry->day = chosen_day;
            entry->start_time = chosen_time;
            strncpy(entry->name, meeting->name, MAX_STR);
            strncpy(entry->type, meeting->type, MAX_STR);
            entry->duration = duration_slots;
            strncpy(entry->frequency, meeting->frequency, MAX_STR);
            assigned_weeks[week] = 1; // Mark week used
            scheduler->total_hours[chosen_day] += duration_slots * 0.5;
            scheduler->meeting_hours[chosen_day] += duration_slots * 0.5;
            // Mark slots as booked
            for (int i = 0; i < duration_slots; i++) {
                scheduler->blocked_slots[week][chosen_day][chosen_time + i] = true;
            }
        }
    }
    return true; // Success
}
 
 // -------------------------
 // OUTPUT GENERATION
 // -------------------------
 
 // Generates an HTML page showing the schedule as tables
 char *generate_schedule_html(MeetingScheduler *scheduler) {
     // Allocate memory for HTML (big enough for our page)
     char *buffer = malloc(16384);
     if (!buffer)
         return NULL; // Out of memory
     buffer[0] = '\0'; // Start with empty string
 
     // Add HTML header with Bootstrap for styling
     strcat(buffer, "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Schedule</title>");
     strcat(buffer, "<link rel='stylesheet' href='https://stackpath.bootstrapcdn.com/bootstrap/4.5.2/css/bootstrap.min.css'>");
     strcat(buffer, "<style>@media print { .no-print { display: none; } }</style>");
     strcat(buffer, "</head><body><div class='container'><h1>Weekly Meeting Schedule</h1>");
 
     char temp[512]; // Temporary buffer for building table rows
 
     // Loop through each week
     for (int week = 0; week < MAX_WEEKS; week++) {
         snprintf(temp, sizeof(temp), "<h3>Week %d</h3>", week + 1); // Week header
         strcat(buffer, temp);
         // Start table
         strcat(buffer, "<table class='table table-bordered'><thead><tr>");
         strcat(buffer, "<th>Day</th><th>Start Time</th><th>End Time</th><th>Name</th><th>Type</th><th>Duration (min)</th><th>Frequency</th>");
         strcat(buffer, "</tr></thead><tbody>");
 
         // Loop through each day
         for (int day = 0; day < MAX_DAYS; day++) {
             // Alternate colors for readability
             const char *day_color = (day % 2 == 0) ? "#ffffff" : "#f2f2f2";
             // Add meetings for this week/day
             for (int i = 0; i < scheduler->schedule_count; i++) {
                 if (scheduler->schedule[i].week == week && scheduler->schedule[i].day == day) {
                     char end_time[8];
                     compute_end_time(scheduler->schedule[i].start_time, scheduler->schedule[i].duration, end_time);
                     // Build table row
                     snprintf(temp, sizeof(temp),
                              "<tr style='background-color:%s;'><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td>"
                              "<td>%d</td><td>%s</td></tr>",
                              day_color, DAYS[day], TIME_SLOTS[scheduler->schedule[i].start_time],
                              end_time, scheduler->schedule[i].name, scheduler->schedule[i].type,
                              scheduler->schedule[i].duration * 30, scheduler->schedule[i].frequency);
                     strcat(buffer, temp);
                 }
             }
             // Add reservations for this day
             for (int i = 0; i < scheduler->reservation_count; i++) {
                 if (strcmp(scheduler->reservations[i].day, DAYS[day]) == 0) {
                     int start_idx = find_slot_index(scheduler->reservations[i].start_time);
                     char end_time[8];
                     compute_end_time(start_idx, scheduler->reservations[i].duration, end_time);
                     // Build reservation row
                     snprintf(temp, sizeof(temp),
                              "<tr style='background-color:%s;'><td>%s</td><td>%s</td><td>%s</td><td>Reserved (External)</td>"
                              "<td>Reserved</td><td>%d</td><td>Weekly</td></tr>",
                              day_color, DAYS[day], scheduler->reservations[i].start_time,
                              end_time, scheduler->reservations[i].duration * 30);
                     strcat(buffer, temp);
                 }
             }
         }
         strcat(buffer, "</tbody></table>"); // End table
     }
     // Add print button and link
     strcat(buffer, "<div class='no-print mt-4'><button class='btn btn-info' onclick='window.print()'>Print to PDF</button></div>");
     strcat(buffer, "<p class='mt-2'><a href='/'>Return to Main Page</a></p>");
     strcat(buffer, "</div></body></html>");
     return buffer; // Return HTML string
 }
 
 // Generates an ICS file for calendar apps
 char *generate_ics(MeetingScheduler *scheduler) {
     // Allocate memory for ICS content
     char *ics = malloc(16384);
     if (!ics) return NULL;
     ics[0] = '\0'; // Start empty
 
     // ICS header
     strcat(ics, "BEGIN:VCALENDAR\r\n");
     strcat(ics, "PRODID:-//Meeting Scheduler//xAI//EN\r\n");
     strcat(ics, "VERSION:2.0\r\n");
 
     // Set base date (Monday, April 14, 2025)
     struct tm base_date = {0};
     base_date.tm_year = 2025 - 1900; // Years since 1900
     base_date.tm_mon = 3; // April (0=Jan)
     base_date.tm_mday = 14;
     base_date.tm_hour = 0;
     base_date.tm_min = 0;
     base_date.tm_sec = 0;
     mktime(&base_date); // Normalize date
 
     char event[512]; // Buffer for each event
     // Add meetings
     for (int i = 0; i < scheduler->schedule_count; i++) {
         ScheduleEntry *s = &scheduler->schedule[i];
         struct tm dtstart = base_date;
         dtstart.tm_mday += s->day + s->week * 7; // Add day and week offset
         int hour = s->start_time / 2 + 9; // Convert slot to hour
         if (s->start_time >= 6)
             hour++; // Skip break
         dtstart.tm_hour = hour;
         dtstart.tm_min = (s->start_time % 2) * 30; // Set minutes
         mktime(&dtstart); // Normalize
         char dtstart_str[32];
         strftime(dtstart_str, sizeof(dtstart_str), "%Y%m%dT%H%M%S", &dtstart); // Format date
         // Build event
         snprintf(event, sizeof(event),
                  "BEGIN:VEVENT\r\nSUMMARY:%s (%s)\r\nDTSTART:%s\r\nDURATION:PT%dM\r\nRRULE:FREQ=WEEKLY\r\n"
                  "DESCRIPTION:Type: %s, Duration: %d min, Frequency: %s\r\nEND:VEVENT\r\n",
                  s->name, s->type, dtstart_str, s->duration * 30, s->type, s->duration * 30, s->frequency);
         strcat(ics, event);
     }
 
     // Add reservations
     for (int i = 0; i < scheduler->reservation_count; i++) {
         Reservation *r = &scheduler->reservations[i];
         struct tm dtstart = base_date;
         int day_idx = find_day_index(r->day);
         dtstart.tm_mday += day_idx;
         int start_idx = find_slot_index(r->start_time);
         dtstart.tm_hour = start_idx / 2 + 9;
         if (start_idx >= 6)
             dtstart.tm_hour++;
         dtstart.tm_min = (start_idx % 2) * 30;
         mktime(&dtstart);
         char dtstart_str[32];
         strftime(dtstart_str, sizeof(dtstart_str), "%Y%m%dT%H%M%S", &dtstart);
         // Build reservation event
         snprintf(event, sizeof(event),
                  "BEGIN:VEVENT\r\nSUMMARY:Reserved (External)\r\nDTSTART:%s\r\nDURATION:PT%dM\r\nRRULE:FREQ=WEEKLY\r\n"
                  "DESCRIPTION:External commitment, Duration: %d min\r\nEND:VEVENT\r\n",
                  dtstart_str, r->duration * 30, r->duration * 30);
         strcat(ics, event);
     }
 
     strcat(ics, "END:VCALENDAR\r\n"); // End ICS
     return ics;
 }
 
 // -------------------------
 // WEB SERVER
 // -------------------------
 
 // Handles web requests (like clicking a link or submitting a form)
 static enum MHD_Result answer_to_connection(void *cls, struct MHD_Connection *connection,
     const char *url, const char *method, const char *version, const char *upload_data,
     size_t *upload_data_size, void **con_cls) {
     (void)version; (void)upload_data; (void)con_cls; // Unused parameters
     struct MHD_Response *response; // HTTP response object
     int ret; // Return code
     char *page = NULL; // HTML content to send
 
     MeetingScheduler *scheduler = (MeetingScheduler *)cls; // Get scheduler from cls
 
     // Main page: shows forms
     if (strcmp(url, "/") == 0) {
         // Allocate and copy HTML for main page
         page = strdup(
             "<!DOCTYPE html>"
             "<html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
             "<title>Meeting Scheduler</title>"
             "<link rel='stylesheet' href='https://stackpath.bootstrapcdn.com/bootstrap/4.5.2/css/bootstrap.min.css'>"
             "</head><body><div class='container mt-4'>"
             "<h1>Meeting Scheduler</h1><hr>"
             "<h3>Add Reservation</h3>"
             "<form action='/addReservation' method='get'>"
               "<div class='form-group'><label>Day</label>"
               "<select name='day' class='form-control'>"
                 "<option>Monday</option><option>Tuesday</option><option>Wednesday</option><option>Thursday</option>"
               "</select></div>"
               "<div class='form-group'><label>Start Time</label>"
               "<input type='time' name='start_time' class='form-control' value='09:00'></div>"
               "<div class='form-group'><label>Duration (minutes)</label>"
               "<select name='duration' class='form-control'>"
                 "<option value='30'>30</option><option value='60'>60</option><option value='90'>90</option>"
               "</select></div>"
               "<button type='submit' class='btn btn-primary'>Add Reservation</button>"
             "</form><hr>"
             "<h3>Add Meeting</h3>"
             "<form action='/addMeeting' method='get'>"
               "<div class='form-group'><label>Meeting Name</label>"
               "<input type='text' name='name' class='form-control' required></div>"
               "<div class='form-group'><label>Meeting Type</label>"
               "<select name='type' class='form-control' required>"
                 "<option value='One-to-one'>One-to-one</option>"
                 "<option value='Design'>Design</option>"
                 "<option value='Management'>Management</option>"
                 "<option value='Contractor'>Contractor</option>"
                 "<option value='Client'>Client</option>"
               "</select></div>"
               "<div class='form-group'><label>Duration (minutes)</label>"
               "<select name='duration' class='form-control'>"
                 "<option value='30'>30</option><option value='60'>60</option><option value='90'>90</option>"
               "</select></div>"
               "<div class='form-group'><label>Preferred Times (comma separated e.g., 09:30,10:00)</label>"
               "<input type='text' name='preferred_times' class='form-control'></div>"
               "<div class='form-group'><label>Fixed Day (optional)</label>"
               "<select name='fixed_day' class='form-control'>"
                 "<option value=''>None</option>"
                 "<option>Monday</option><option>Tuesday</option><option>Wednesday</option><option>Thursday</option>"
               "</select></div>"
               "<div class='form-group'><label>Fixed Time (optional)</label>"
               "<input type='time' name='fixed_time' class='form-control'></div>"
               "<div class='form-group'><label>Frequency</label>"
               "<select name='frequency' class='form-control'>"
                 "<option value='weekly'>Weekly</option><option value='fortnightly'>Fortnightly</option>"
                 "<option value='third_week'>Third Week</option><option value='monthly'>Monthly</option>"
               "</select></div>"
               "<button type='submit' class='btn btn-primary'>Add Meeting</button>"
             "</form><hr>"
             "<h3>Schedule</h3>"
             "<p><a class='btn btn-secondary' href='/displaySchedule'>View Schedule</a></p><hr>"
             "<h3>Export ICS</h3>"
             "<form action='/exportICS' method='get'>"
               "<div class='form-group'><label>Filename</label>"
               "<input type='text' name='filename' class='form-control' placeholder='schedule.ics' required></div>"
               "<button type='submit' class='btn btn-primary'>Export ICS</button>"
             "</form><hr>"
             "<h3>Clear Session</h3>"
             "<p><a class='btn btn-danger' href='/clearSession'>Clear All Meetings & Reservations</a></p>"
             "</div></body></html>"
         );
     }
     // Add reservation
     else if (strcmp(url, "/addReservation") == 0) {
         // Get form data
         const char *day = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "day");
         const char *start_time = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "start_time");
         const char *duration_str = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "duration");
         bool success = false;
         if (day && start_time && duration_str)
             success = reserve_slot(scheduler, day, start_time, atoi(duration_str)); // Try to reserve
         // Allocate response page
         page = malloc(1024);
         if (success)
             snprintf(page, 1024, "<html><body><div class='container'><h3>Reservation added successfully.</h3>"
                                  "<p><a href='/'>Return to Main Page</a></p></div></body></html>");
         else
             snprintf(page, 1024, "<html><body><div class='container'><h3>Failed to add reservation.</h3>"
                                  "<p><a href='/'>Return to Main Page</a></p></div></body></html>");
     }
     // Add meeting
     else if (strcmp(url, "/addMeeting") == 0) {
         Meeting meeting;
         memset(&meeting, 0, sizeof(meeting)); // Clear meeting struct
         // Initialize preferred_hours with -1
         for (int i = 0; i < 8; i++) meeting.preferred_hours[i] = -1;
         // Get form data
         const char *name = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "name");
         const char *type = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "type");
         const char *duration_str = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "duration");
         const char *pref_times = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "preferred_times");
         const char *fixed_day = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "fixed_day");
         const char *fixed_time = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "fixed_time");
         const char *frequency = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "frequency");
         // Fill meeting struct
         if (name) strncpy(meeting.name, name, MAX_STR);
         if (type) strncpy(meeting.type, type, MAX_STR);
         if (duration_str) {
             int dur = atoi(duration_str); // Convert string to number
             if (dur == 30) meeting.duration = 1;
             else if (dur == 60) meeting.duration = 2;
             else if (dur == 90) meeting.duration = 3;
             else meeting.duration = 1;
         }
         // Parse preferred times (e.g., "09:30,10:00")
         if (pref_times && strlen(pref_times) > 0) {
             char temp[256];
             strncpy(temp, pref_times, 256);
             char *token = strtok(temp, ","); // Split by comma
             int idx = 0;
             while (token && idx < 8) {
                 int slot = find_slot_index(token);
                 if (slot != -1)
                     meeting.preferred_hours[idx++] = slot;
                 token = strtok(NULL, ",");
             }
             if (idx < 8) meeting.preferred_hours[idx] = -1;
         }
         if (fixed_day && strlen(fixed_day) > 0)
             strncpy(meeting.fixed_day, fixed_day, MAX_STR);
         if (fixed_time && strlen(fixed_time) > 0)
             strncpy(meeting.fixed_time, fixed_time, MAX_STR);
         if (frequency) strncpy(meeting.frequency, frequency, MAX_STR);
         // Try to add meeting
         bool success = add_meeting(scheduler, &meeting);
         page = malloc(1024);
         if (success)
             snprintf(page, 1024, "<html><body><div class='container'><h3>Meeting added successfully.</h3>"
                                  "<p><a href='/'>Return to Main Page</a></p></div></body></html>");
         else
             snprintf(page, 1024, "<html><body><div class='container'><h3>Failed to add meeting.</h3>"
                                  "<p><a href='/'>Return to Main Page</a></p></div></body></html>");
     }
     // Show schedule
     else if (strcmp(url, "/displaySchedule") == 0) {
         page = generate_schedule_html(scheduler); // Generate HTML
     }
     // Export ICS
     else if (strcmp(url, "/exportICS") == 0) {
         char *ics = generate_ics(scheduler); // Generate ICS
         // Create response for download
         struct MHD_Response *ics_resp = MHD_create_response_from_buffer(strlen(ics), (void*)ics, MHD_RESPMEM_MUST_FREE);
         MHD_add_response_header(ics_resp, "Content-Type", "text/calendar");
         MHD_add_response_header(ics_resp, "Content-Disposition", "attachment; filename=\"schedule.ics\"");
         ret = MHD_queue_response(connection, MHD_HTTP_OK, ics_resp);
         MHD_destroy_response(ics_resp);
         return ret; // Send file directly
     }
     // Clear schedule
     else if (strcmp(url, "/clearSession") == 0) {
         init_scheduler(scheduler); // Reset everything
         page = strdup("<html><body><div class='container'><h3>Session Cleared.</h3>"
                       "<p><a href='/'>Return to Main Page</a></p></div></body></html>");
     }
     // Unknown URL
     else {
         page = strdup("<html><body><h3>404 Not Found</h3></body></html>");
     }
 
     // Create and send response
     response = MHD_create_response_from_buffer(strlen(page), (void *)page, MHD_RESPMEM_MUST_FREE);
     ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
     MHD_destroy_response(response);
     return ret;
 }
 
 // -------------------------
 // MAIN PROGRAM
 // -------------------------
 
 int main(void) {
     srand(time(NULL)); // Seed random numbers for week shuffling
     MeetingScheduler scheduler; // Create scheduler
     init_scheduler(&scheduler); // Initialize it
 
     // Start web server
     struct MHD_Daemon *daemon;
     daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, PORT, NULL, NULL,
                               &answer_to_connection, &scheduler, MHD_OPTION_END);
     if (NULL == daemon) {
         fprintf(stderr, "Failed to start web server\n");
         return 1; // Exit with error
     }
     printf("Server running on http://localhost:%d\n", PORT);
     getchar(); // Wait for Enter to stop
     MHD_stop_daemon(daemon); // Shut down server
     return 0; // Exit successfully
 }