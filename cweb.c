/*
 * scheduler_web.c
 *
 * A full implementation that uses your original scheduling code as its logic,
 * wrapped in a web front end (using Bootstrap for styling) served via libmicrohttpd.
 *
 * Compile with:
 *     gcc scheduler_web.c -o scheduler_web -lmicrohttpd
 *
 * Then run:
 *     ./scheduler_web
 *
 * And open http://localhost:8888 in your browser.
 */

 #include <microhttpd.h>
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <stdbool.h>
 #include <time.h>
 
 #define PORT 8888
 
 // -------------------------
 // SCHEDULER CODE (YOUR LOGIC)
 // -------------------------
 
 #define MAX_DAYS 4
 #define MAX_WEEKS 4
 #define MAX_SLOTS 14 // 09:00–16:30, excluding breaks
 #define MAX_MEETINGS 100
 #define MAX_RESERVATIONS 50
 #define MAX_STR 64
 
 // Constant definitions
 const char *DAYS[MAX_DAYS] = {"Monday", "Tuesday", "Wednesday", "Thursday"};
 const char *TIME_SLOTS[MAX_SLOTS] = {
     "09:00", "09:30", "10:00", "10:30", "11:00", "11:30",
     "13:00", "13:30", "14:00", "14:30", "15:00", "15:30",
     "16:00", "16:30"
 };
 const char *BREAK_SLOTS[2] = {"12:00", "12:30"};
 const char *FREQUENCIES[4] = {"weekly", "fortnightly", "third_week", "monthly"};
 const int DURATIONS[3] = {1, 2, 3}; // 30, 60, 90 min in 30-min slots
 
 // Data Structures
 typedef struct {
     char name[MAX_STR];
     char type[MAX_STR];
     int duration; // in 30-min slots: 1 = 30, 2 = 60, 3 = 90
     int preferred_hours[8]; // list of preferred slot indices; -1 terminates the list
     char fixed_day[MAX_STR];
     char fixed_time[MAX_STR];
     char frequency[MAX_STR];
 } Meeting;
 
 typedef struct {
     char day[MAX_STR];
     char start_time[MAX_STR];
     int duration; // in 30-min slots
 } Reservation;
 
 typedef struct {
     int week;
     int day;       // index into DAYS array (0=Monday, …, 3=Thursday)
     int start_time; // index into TIME_SLOTS
     char name[MAX_STR];
     char type[MAX_STR];
     int duration;   // in 30-min slots
     char frequency[MAX_STR];
 } ScheduleEntry;
 
 typedef struct {
     ScheduleEntry schedule[MAX_MEETINGS * MAX_WEEKS];
     int schedule_count;
     Reservation reservations[MAX_RESERVATIONS];
     int reservation_count;
     double total_hours[MAX_DAYS];   // meetings + reservations, over MAX_WEEKS
     double meeting_hours[MAX_DAYS]; // meetings only
     bool blocked_slots[MAX_WEEKS][MAX_DAYS][MAX_SLOTS];
 } MeetingScheduler;
 
 // Utility functions
 
 int find_slot_index(const char *time) {
     for (int i = 0; i < MAX_SLOTS; i++) {
         if (strcmp(TIME_SLOTS[i], time) == 0)
             return i;
     }
     return -1;
 }
 
 int find_day_index(const char *day) {
     for (int i = 0; i < MAX_DAYS; i++) {
         if (strcmp(DAYS[i], day) == 0)
             return i;
     }
     return -1;
 }
 
 bool is_break_slot(const char *time) {
     return (strcmp(time, BREAK_SLOTS[0]) == 0 || strcmp(time, BREAK_SLOTS[1]) == 0);
 }
 
 double slot_to_hour(int slot_idx) {
     int hour = slot_idx / 2 + 9;
     int minute = (slot_idx % 2) * 30;
     if (slot_idx >= 6) // skip break hour
         hour++;
     return hour + minute / 60.0;
 }
 
 void compute_end_time(int start_idx, int duration_slots, char *end_time) {
     double start_hour = slot_to_hour(start_idx);
     double end_hour = start_hour + duration_slots * 0.5;
     int end_h = (int) end_hour;
     int end_m = (int)((end_hour - end_h) * 60);
     snprintf(end_time, 8, "%02d:%02d", end_h, end_m);
 }
 
 // Initialize the scheduler
 void init_scheduler(MeetingScheduler *scheduler) {
     scheduler->schedule_count = 0;
     scheduler->reservation_count = 0;
     memset(scheduler->total_hours, 0, sizeof(scheduler->total_hours));
     memset(scheduler->meeting_hours, 0, sizeof(scheduler->meeting_hours));
     memset(scheduler->blocked_slots, 0, sizeof(scheduler->blocked_slots));
 }
 
 // Reserve a time slot (applies to all weeks)
 bool reserve_slot(MeetingScheduler *scheduler, const char *day, const char *start_time, int duration_minutes) {
     int day_idx = find_day_index(day);
     int start_idx = find_slot_index(start_time);
     if (day_idx == -1 || start_idx == -1 ||
         (duration_minutes % 30 != 0) || duration_minutes < 30 || duration_minutes > 90) {
         // Invalid parameters
         return false;
     }
     int duration_slots = duration_minutes / 30;
     double start_hour = slot_to_hour(start_idx);
     double end_hour = start_hour + duration_slots * 0.5;
     if (end_hour > 17.0 || is_break_slot(start_time))
         return false;
     for (int i = 0; i < duration_slots; i++) {
         int slot_idx = start_idx + i;
         if (slot_idx >= MAX_SLOTS || is_break_slot(TIME_SLOTS[slot_idx]))
             return false;
     }
     for (int week = 0; week < MAX_WEEKS; week++) {
         for (int i = 0; i < duration_slots; i++) {
             int slot_idx = start_idx + i;
             if (scheduler->blocked_slots[week][day_idx][slot_idx])
                 return false;
             scheduler->blocked_slots[week][day_idx][slot_idx] = true;
         }
     }
     Reservation *res = &scheduler->reservations[scheduler->reservation_count++];
     strncpy(res->day, day, MAX_STR);
     strncpy(res->start_time, start_time, MAX_STR);
     res->duration = duration_slots;
     scheduler->total_hours[day_idx] += duration_slots * 0.5 * MAX_WEEKS;
     return true;
 }
 
 bool is_valid_slot(MeetingScheduler *scheduler, int week, int day_idx, int start_idx, int duration_slots) {
     if (start_idx < 0 || start_idx >= MAX_SLOTS || is_break_slot(TIME_SLOTS[start_idx]))
         return false;
     double start_hour = slot_to_hour(start_idx);
     double end_hour = start_hour + duration_slots * 0.5;
     if (end_hour > 17.0)
         return false;
     for (int i = 0; i < duration_slots; i++) {
         int slot_idx = start_idx + i;
         if (slot_idx >= MAX_SLOTS || is_break_slot(TIME_SLOTS[slot_idx]) || scheduler->blocked_slots[week][day_idx][slot_idx])
             return false;
     }
     return true;
 }
 
 bool add_meeting(MeetingScheduler *scheduler, Meeting *meeting) {
     int duration_slots = meeting->duration;
     int occurrences = (strcmp(meeting->frequency, "weekly") == 0) ? 4 :
                       (strcmp(meeting->frequency, "fortnightly") == 0) ? 2 : 1;
     int fixed_day_idx = meeting->fixed_day[0] ? find_day_index(meeting->fixed_day) : -1;
     int fixed_time_idx = meeting->fixed_time[0] ? find_slot_index(meeting->fixed_time) : -1;
     int assigned_weeks[MAX_WEEKS] = {0};
     int chosen_day = -1, chosen_time = -1;
     int day_order[MAX_DAYS];
     for (int i = 0; i < MAX_DAYS; i++) day_order[i] = i;
     // Simple bubble sort on total_hours to try less busy days first
     for (int i = 0; i < MAX_DAYS - 1; i++) {
         for (int j = 0; j < MAX_DAYS - i - 1; j++) {
             if (scheduler->total_hours[day_order[j]] > scheduler->total_hours[day_order[j + 1]]) {
                 int temp = day_order[j];
                 day_order[j] = day_order[j + 1];
                 day_order[j + 1] = temp;
             }
         }
     }
     int weeks[MAX_WEEKS] = {0, 1, 2, 3};
     for (int i = MAX_WEEKS - 1; i > 0; i--) {
         int j = rand() % (i + 1);
         int temp = weeks[i];
         weeks[i] = weeks[j];
         weeks[j] = temp;
     }
     double min_hours = 1e9;
     for (int w = 0; w < MAX_WEEKS; w++) {
         int day_start = (fixed_day_idx >= 0) ? fixed_day_idx : 0;
         int day_end = (fixed_day_idx >= 0) ? fixed_day_idx + 1 : MAX_DAYS;
         for (int d = 0; d < day_end - day_start; d++) {
             int day_idx = (fixed_day_idx >= 0) ? fixed_day_idx : day_order[d];
             if (scheduler->meeting_hours[day_idx] / 4 > 2.5)
                 continue;
             int time_start = (fixed_time_idx >= 0) ? fixed_time_idx : 0;
             int time_end = (fixed_time_idx >= 0) ? fixed_time_idx + 1 : MAX_SLOTS;
             if (meeting->preferred_hours[0] >= 0 && fixed_time_idx < 0) {
                 time_end = 0;
                 while (meeting->preferred_hours[time_end] >= 0 && time_end < 8)
                     time_end++;
             }
             for (int t = time_start; t < time_end && t < MAX_SLOTS; t++) {
                 int time_idx = (fixed_time_idx >= 0) ? fixed_time_idx :
                                (meeting->preferred_hours[0] >= 0 ? meeting->preferred_hours[t] : t);
                 if (time_idx < 0 || time_idx >= MAX_SLOTS)
                     continue;
                 int valid_weeks = 0;
                 double avg_hours = 0;
                 for (int ww = 0; ww < MAX_WEEKS; ww++) {
                     if (assigned_weeks[ww])
                         continue;
                     if (is_valid_slot(scheduler, ww, day_idx, time_idx, duration_slots)) {
                         valid_weeks++;
                         avg_hours += scheduler->total_hours[day_idx] + duration_slots * 0.5;
                     }
                     if (valid_weeks >= occurrences)
                         break;
                 }
                 if (valid_weeks >= occurrences && avg_hours / valid_weeks < min_hours) {
                     min_hours = avg_hours / valid_weeks;
                     chosen_day = day_idx;
                     chosen_time = time_idx;
                 }
             }
         }
     }
     if (chosen_day == -1 || chosen_time == -1)
         return false;
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
             return false;
         ScheduleEntry *entry = &scheduler->schedule[scheduler->schedule_count++];
         entry->week = week;
         entry->day = chosen_day;
         entry->start_time = chosen_time;
         strncpy(entry->name, meeting->name, MAX_STR);
         strncpy(entry->type, meeting->type, MAX_STR);
         entry->duration = duration_slots;
         strncpy(entry->frequency, meeting->frequency, MAX_STR);
         assigned_weeks[week] = 1;
         scheduler->total_hours[chosen_day] += duration_slots * 0.5;
         scheduler->meeting_hours[chosen_day] += duration_slots * 0.5;
         for (int i = 0; i < duration_slots; i++) {
             scheduler->blocked_slots[week][chosen_day][chosen_time + i] = true;
         }
     }
     return true;
 }
 
 // Generate a simple HTML schedule display.
 // (This version builds an HTML string with basic formatting.)

 // Generate an HTML page that shows the schedule in tables (one per week),
// with rows for a given day sharing the same background color (alternating white and gray).
// Generate an HTML page that shows the schedule in tables (one per week),
// with rows for a given day sharing the same background color (alternating white and gray),
// and adds a "Print to PDF" button at the bottom.
char *generate_schedule_html(MeetingScheduler *scheduler) {
    char *buffer = malloc(16384);
    if (!buffer)
        return NULL;
    buffer[0] = '\0';
    
    strcat(buffer, "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Schedule</title>");
    strcat(buffer, "<link rel='stylesheet' href='https://stackpath.bootstrapcdn.com/bootstrap/4.5.2/css/bootstrap.min.css'>");
    strcat(buffer, "<style>@media print { .no-print { display: none; } }</style>");
    strcat(buffer, "</head><body><div class='container'><h1>Weekly Meeting Schedule</h1>");
    
    char temp[512];
    
    // Loop through each week
    for (int week = 0; week < MAX_WEEKS; week++) {
        snprintf(temp, sizeof(temp), "<h3>Week %d</h3>", week + 1);
        strcat(buffer, temp);
        // Start table with Bootstrap styling
        strcat(buffer, "<table class='table table-bordered'><thead><tr>");
        strcat(buffer, "<th>Day</th><th>Start Time</th><th>End Time</th><th>Name</th><th>Type</th><th>Duration (min)</th><th>Frequency</th>");
        strcat(buffer, "</tr></thead><tbody>");
        
        // Loop through each day (Monday to Thursday)
        for (int day = 0; day < MAX_DAYS; day++) {
            // Determine background color for the entire day:
            const char *day_color = (day % 2 == 0) ? "#ffffff" : "#f2f2f2";
            
            // Append meeting entries for this week and day
            for (int i = 0; i < scheduler->schedule_count; i++) {
                if (scheduler->schedule[i].week == week && scheduler->schedule[i].day == day) {
                    char end_time[8];
                    compute_end_time(scheduler->schedule[i].start_time, scheduler->schedule[i].duration, end_time);
                    snprintf(temp, sizeof(temp),
                             "<tr style='background-color:%s;'><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td>"
                             "<td>%d</td><td>%s</td></tr>",
                             day_color,
                             DAYS[day],
                             TIME_SLOTS[scheduler->schedule[i].start_time],
                             end_time,
                             scheduler->schedule[i].name,
                             scheduler->schedule[i].type,
                             scheduler->schedule[i].duration * 30,
                             scheduler->schedule[i].frequency);
                    strcat(buffer, temp);
                }
            }
            // Append external reservations for this day
            for (int i = 0; i < scheduler->reservation_count; i++) {
                if (strcmp(scheduler->reservations[i].day, DAYS[day]) == 0) {
                    int start_idx = find_slot_index(scheduler->reservations[i].start_time);
                    char end_time[8];
                    compute_end_time(start_idx, scheduler->reservations[i].duration, end_time);
                    snprintf(temp, sizeof(temp),
                             "<tr style='background-color:%s;'><td>%s</td><td>%s</td><td>%s</td><td>Reserved (External)</td>"
                             "<td>Reserved</td><td>%d</td><td>Weekly</td></tr>",
                             day_color,
                             DAYS[day],
                             scheduler->reservations[i].start_time,
                             end_time,
                             scheduler->reservations[i].duration * 30);
                    strcat(buffer, temp);
                }
            }
        }
        strcat(buffer, "</tbody></table>");
    }
    // Add a Print button that will invoke the browser's print dialog.
    strcat(buffer, "<div class='no-print mt-4'><button class='btn btn-info' onclick='window.print()'>Print to PDF</button></div>");
    strcat(buffer, "<p class='mt-2'><a href='/'>Return to Main Page</a></p>");
    strcat(buffer, "</div></body></html>");
    return buffer;
}
 
 // Generate an ICS file in memory and return it in a buffer.
 // For simplicity, this function writes to a fixed-size buffer.
 char *generate_ics(MeetingScheduler *scheduler) {
     char *ics = malloc(16384);
     if (!ics) return NULL;
     ics[0] = '\0';
     strcat(ics, "BEGIN:VCALENDAR\r\n");
     strcat(ics, "PRODID:-//Meeting Scheduler//xAI//EN\r\n");
     strcat(ics, "VERSION:2.0\r\n");
 
     // Use a fixed base date, e.g. Monday, April 14, 2025.
     struct tm base_date = {0};
     base_date.tm_year = 2025 - 1900;
     base_date.tm_mon = 3; // April (0-indexed)
     base_date.tm_mday = 14;
     base_date.tm_hour = 0;
     base_date.tm_min = 0;
     base_date.tm_sec = 0;
     mktime(&base_date);
     
     // For each scheduled entry, create an event.
     char event[512];
     for (int i = 0; i < scheduler->schedule_count; i++) {
         ScheduleEntry *s = &scheduler->schedule[i];
         struct tm dtstart = base_date;
         dtstart.tm_mday += s->day + s->week * 7;
         int hour = s->start_time / 2 + 9;
         if (s->start_time >= 6)
             hour++;
         int minute = (s->start_time % 2) * 30;
         dtstart.tm_hour = hour;
         dtstart.tm_min = minute;
         mktime(&dtstart);
         char dtstart_str[32];
         strftime(dtstart_str, sizeof(dtstart_str), "%Y%m%dT%H%M%S", &dtstart);
         snprintf(event, sizeof(event),
                  "BEGIN:VEVENT\r\nSUMMARY:%s (%s)\r\nDTSTART:%s\r\nDURATION:PT%dM\r\nRRULE:FREQ=WEEKLY\r\nDESCRIPTION:Type: %s, Duration: %d min, Frequency: %s\r\nEND:VEVENT\r\n",
                  s->name, s->type, dtstart_str, s->duration * 30, s->type, s->duration * 30, s->frequency);
         strcat(ics, event);
     }
     
     // Export reservations as events
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
         snprintf(event, sizeof(event),
                  "BEGIN:VEVENT\r\nSUMMARY:Reserved (External)\r\nDTSTART:%s\r\nDURATION:PT%dM\r\nRRULE:FREQ=WEEKLY\r\nDESCRIPTION:External commitment, Duration: %d min\r\nEND:VEVENT\r\n",
                  dtstart_str, r->duration * 30, r->duration * 30);
         strcat(ics, event);
     }
     
     strcat(ics, "END:VCALENDAR\r\n");
     return ics;
 }
 
 // -------------------------
 // WEB SERVER CODE USING LIBMICROHTTPD
 // -------------------------
 
 // The answer callback for each connection.
 // We use GET submissions for simplicity.
 static enum MHD_Result answer_to_connection(void *cls,
    struct MHD_Connection *connection,
    const char *url, const char *method,
    const char *version, const char *upload_data,
    size_t *upload_data_size, void **con_cls)
 {
     (void)cls; (void)version; (void)upload_data; (void)con_cls;
     
     struct MHD_Response *response;
     int ret;
     char *page = NULL;
 
     // Main page: shows forms with Bootstrap styling.
     if(strcmp(url, "/") == 0) {
        page = strdup(
            "<!DOCTYPE html>"
            "<html lang='en'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
            "<title>Meeting Scheduler</title>"
            "<link rel='stylesheet' href='https://stackpath.bootstrapcdn.com/bootstrap/4.5.2/css/bootstrap.min.css'>"
            "</head><body><div class='container mt-4'>"
            "<h1>Meeting Scheduler</h1>"
            "<hr>"
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
            "</form>"
            "<hr>"
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
            "</form>"
            "<hr>"
            "<h3>Schedule</h3>"
            "<p><a class='btn btn-secondary' href='/displaySchedule'>View Schedule</a></p>"
            "<hr>"
            "<h3>Export ICS</h3>"
            "<form action='/exportICS' method='get'>"
              "<div class='form-group'><label>Filename</label>"
              "<input type='text' name='filename' class='form-control' placeholder='schedule.ics' required></div>"
              "<button type='submit' class='btn btn-primary'>Export ICS</button>"
            "</form>"
            "<hr>"
            "<h3>Clear Session</h3>"
            "<p><a class='btn btn-danger' href='/clearSession'>Clear All Meetings &amp; Reservations</a></p>"
            "</div></body></html>"
        );
    }
    
     // /addReservation endpoint: add a reservation using query parameters.
     else if(strcmp(url, "/addReservation") == 0) {
         const char *day = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "day");
         const char *start_time = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "start_time");
         const char *duration_str = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "duration");
         bool success = false;
         if(day && start_time && duration_str)
             success = reserve_slot((MeetingScheduler *)cls, day, start_time, atoi(duration_str));
         page = malloc(1024);
         if(success)
             snprintf(page, 1024, "<html><body><div class='container'><h3>Reservation added successfully.</h3>"
                                   "<p><a href='/'>Return to Main Page</a></p></div></body></html>");
         else
             snprintf(page, 1024, "<html><body><div class='container'><h3>Failed to add reservation.</h3>"
                                   "<p><a href='/'>Return to Main Page</a></p></div></body></html>");
     }
     // /addMeeting endpoint: add a meeting.
     else if(strcmp(url, "/addMeeting") == 0) {
         Meeting meeting;
         memset(&meeting, 0, sizeof(meeting));
         // Initialize preferred_hours with -1
         for (int i = 0; i < 8; i++) meeting.preferred_hours[i] = -1;
         const char *name = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "name");
         const char *type = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "type");
         const char *duration_str = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "duration");
         const char *pref_times = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "preferred_times");
         const char *fixed_day = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "fixed_day");
         const char *fixed_time = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "fixed_time");
         const char *frequency = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "frequency");
         if(name) strncpy(meeting.name, name, MAX_STR);
         if(type) strncpy(meeting.type, type, MAX_STR);
         if(duration_str) {
             int dur = atoi(duration_str);
             if(dur == 30) meeting.duration = 1;
             else if(dur == 60) meeting.duration = 2;
             else if(dur == 90) meeting.duration = 3;
             else meeting.duration = 1;
         }
         if(pref_times && strlen(pref_times) > 0) {
             // Parse comma-separated times.
             char temp[256];
             strncpy(temp, pref_times, 256);
             char *token = strtok(temp, ",");
             int idx = 0;
             while(token && idx < 8) {
                 int slot = find_slot_index(token);
                 if(slot != -1)
                     meeting.preferred_hours[idx++] = slot;
                 token = strtok(NULL, ",");
             }
             if(idx < 8) meeting.preferred_hours[idx] = -1;
         }
         if(fixed_day && strlen(fixed_day) > 0)
             strncpy(meeting.fixed_day, fixed_day, MAX_STR);
         if(fixed_time && strlen(fixed_time) > 0)
             strncpy(meeting.fixed_time, fixed_time, MAX_STR);
         if(frequency) strncpy(meeting.frequency, frequency, MAX_STR);
         bool success = add_meeting((MeetingScheduler *)cls, &meeting);
         page = malloc(1024);
         if(success)
             snprintf(page, 1024, "<html><body><div class='container'><h3>Meeting added successfully.</h3>"
                                   "<p><a href='/'>Return to Main Page</a></p></div></body></html>");
         else
             snprintf(page, 1024, "<html><body><div class='container'><h3>Failed to add meeting.</h3>"
                                   "<p><a href='/'>Return to Main Page</a></p></div></body></html>");
     }
     // /displaySchedule: show the schedule in an HTML page.
     else if(strcmp(url, "/displaySchedule") == 0) {
         char *sched_html = generate_schedule_html((MeetingScheduler *)cls);
         page = sched_html; // already allocated
     }
     // /exportICS: return the calendar as an ICS file.
     else if(strcmp(url, "/exportICS") == 0) {
         char *ics = generate_ics((MeetingScheduler *)cls);
         // Set appropriate headers for a file download
         struct MHD_Response *ics_resp = MHD_create_response_from_buffer(strlen(ics), (void*)ics, MHD_RESPMEM_MUST_FREE);
         MHD_add_response_header(ics_resp, "Content-Type", "text/calendar");
         MHD_add_response_header(ics_resp, "Content-Disposition", "attachment; filename=\"schedule.ics\"");
         ret = MHD_queue_response(connection, MHD_HTTP_OK, ics_resp);
         MHD_destroy_response(ics_resp);
         return ret;
     }
     // /clearSession endpoint: clear the scheduler session
    else if(strcmp(url, "/clearSession") == 0) {
        MeetingScheduler *sched = (MeetingScheduler *)cls;
        init_scheduler(sched);  // resets schedule_count, reservation_count, etc.
        page = strdup("<html><body><div class='container'><h3>Session Cleared.</h3>"
                        "<p><a href='/'>Return to Main Page</a></p></div></body></html>");
}

     // Unknown endpoint: 404 response.
     else {
         page = strdup("<html><body><h3>404 Not Found</h3></body></html>");
     }
 
     response = MHD_create_response_from_buffer(strlen(page), (void *)page, MHD_RESPMEM_MUST_FREE);
     ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
     MHD_destroy_response(response);
     return ret;
 }
 
 // -------------------------
 // MAIN FUNCTION: initialize scheduler and start the server.
 // -------------------------
 int main(void) {
     srand(time(NULL));
     MeetingScheduler scheduler;
     init_scheduler(&scheduler);
 
     // Optionally, preload some sample data.
    //  reserve_slot(&scheduler, "Monday", "14:00", 60);
    //  reserve_slot(&scheduler, "Wednesday", "15:00", 30);
    //  Meeting sampleMeetings[] = {
    //      {"One-to-one with Ian", "one-to-one", 1, {2, 3, 4, 5, 6, 7, -1}, "", "", "weekly"},
    //      {"One-to-one with Fari", "one-to-one", 1, {2, 3, 4, 5, 6, 7, -1}, "", "", "weekly"},
    //      {"One-to-one with Perith", "one-to-one", 1, {2, 3, 4, 5, 6, 7, -1}, "", "", "weekly"}
    //  };
    //  int sampleCount = sizeof(sampleMeetings)/sizeof(sampleMeetings[0]);
    //  for (int i = 0; i < sampleCount; i++) {
    //      add_meeting(&scheduler, &sampleMeetings[i]);
    //  }
 
     struct MHD_Daemon *daemon;
     daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, PORT, NULL, NULL,
         &answer_to_connection, &scheduler, MHD_OPTION_END);
     if (NULL == daemon) {
         fprintf(stderr, "Failed to start web server\n");
         return 1;
     }
     printf("Server running on http://localhost:%d\n", PORT);
     getchar();  // Press Enter to stop the server.
     MHD_stop_daemon(daemon);
     return 0;
 }
 