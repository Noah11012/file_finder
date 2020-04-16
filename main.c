#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <ncurses.h>
#include <string.h>

#define true 1
#define false 0

typedef enum
{
    DIR_ENTRY_NONE,
    DIR_ENTRY_FILE,
    DIR_ENTRY_DIRECTORY
} Dir_Entry_Kind;

typedef struct
{
    Dir_Entry_Kind kind;
    char name[256];
    int name_length;
} Dir_Entry;

typedef struct
{
    int count;
    int capacity;
    int position;
    Dir_Entry stack_buffer[16];
    Dir_Entry *heap_buffer;
    Dir_Entry *buffer;
} Array;

#define for_each_array(a) for (Dir_Entry *it = &a.buffer[a.position]; \
a.position < a.count; \
it = &a.buffer[++a.position]) \

Array array_new(void)
{
    Array array =
    {
        .count = 0,
        .capacity = 16,
        .position = 0,
        .heap_buffer = NULL,
    };
    
    array.buffer = array.stack_buffer;
    
    return array;
}

void array_add(Array *array, Dir_Entry item)
{
    if (array->count + 1 >= array->capacity)
    {
        if (!array->heap_buffer)
        {
            array->heap_buffer = malloc(sizeof item * array->capacity * 2);
            memcpy(array->heap_buffer, array->stack_buffer, sizeof item * array->count);
            array->buffer = array->heap_buffer;
        } else
        {
            array->heap_buffer = realloc(array->heap_buffer, sizeof item * array->capacity * 2);
            array->buffer = array->heap_buffer;
        }
        
        array->capacity *= 2;
    }
    
    array->buffer[array->count] = item;
    array->count++;
}

int main(void)
{
    initscr();
    cbreak();
    raw();
    noecho();
    curs_set(0);
    
    char cwd_buffer[512];
    getcwd(cwd_buffer, 512);
    DIR *current_directory = opendir(cwd_buffer);
    
    int initial_window_height = getmaxy(stdscr);
    int initial_window_width = getmaxx(stdscr);
    
    WINDOW *left = newwin(initial_window_height - 1, initial_window_width, 1, 0);
    // WINDOW *right;
    
    int entry_x = 2;
    int entry_y = 2;
    
    // Only fill the entries array on the
    // first iteration or when the directory changes
    int update_directory_entries = true;
    int show_hidden_files = false;
    int selection = 0;
    
    Array entries = array_new();
    
    int keep_running = true;
    while (keep_running)
    {
        int height;
        int width;
        // For every iteration of the loop
        // make sure to grab the dimensions of the window
        // This can change because the user could have resized the terminal
        height = getmaxy(stdscr);
        width = getmaxx(stdscr);
        wresize(left, height - 1, width);
        
        wclear(left);
        clear();
        
        box(left, 0, 0);
        mvprintw(0, 1, "%s", cwd_buffer);
        
        if (update_directory_entries)
        {
            entries.count = 0;
        }
        
        // Only update the entries array when we need to
        // like if we change the current directory
        entries.position = 0;
        for (struct dirent *ent = readdir(current_directory);
             update_directory_entries && ent;
             ent = readdir(current_directory))
        {
            int add_entry = true;
            if (ent->d_name[0] == '.')
            {
                add_entry = show_hidden_files;
            }
            
            if (add_entry)
            {
                Dir_Entry entry;
                entry.name_length = strlen(ent->d_name);
                memcpy(entry.name, ent->d_name, entry.name_length);
                entry.name[entry.name_length] = 0;
                
                switch (ent->d_type)
                {
                    case DT_REG:
                    entry.kind = DIR_ENTRY_FILE;
                    break;
                    
                    case DT_DIR:
                    entry.kind = DIR_ENTRY_DIRECTORY;
                    break;
                }
                
                array_add(&entries, entry);
            }
        }
        
        update_directory_entries = false;
        
        // Display the contents of the directory
        int i = 0;
        for_each_array (entries)
        {
            // If the next item is too big to fit onto the current
            // row then start rendering the entries two rows down
            if (entry_x + it->name_length > width)
            {
                entry_y += 2;
                entry_x = 1;
            }
            
            if (it->kind == DIR_ENTRY_DIRECTORY)
            {
                wattron(left, A_UNDERLINE);
            }
            
            mvwprintw(left, entry_y, entry_x, it->name);
            
            wattroff(left, A_UNDERLINE);
            
            if (selection == i)
            {
                mvwprintw(left, entry_y + 1, entry_x, "^");
            }
            
            entry_x += it->name_length;
            entry_x++; // For the space between entries
            
            i++;
        }
        
        entry_y = 2;
        entry_x = 2;
        
        refresh();
        wrefresh(left);
        
        int c = getch();
        switch (c)
        {
            case 'q':
            keep_running = false;
            break;
            
            case 'b':
            {
                chdir("..");
                closedir(current_directory);
                getcwd(cwd_buffer, 512);
                current_directory = opendir(cwd_buffer);
                update_directory_entries = true;
                selection = 0;
            }
            break;
            
            case 'e':
            {
                Dir_Entry *entry = &entries.buffer[selection];
                if (entry->kind == DIR_ENTRY_DIRECTORY)
                {
                    chdir(entry->name);
                    closedir(current_directory);
                    getcwd(cwd_buffer, 512);
                    current_directory = opendir(cwd_buffer);
                    update_directory_entries = true;
                    selection = 0;
                }
            }
            break;
            
            case 's':
            {
                show_hidden_files = !show_hidden_files;
                rewinddir(current_directory);
                update_directory_entries = true;
            }
            break;
            
            case 'h':
            {
                if (selection > 0)
                {
                    selection--;
                }
            }
            break;
            
            case 'l':
            {
                if (selection < entries.count - 1)
                {
                    selection++;
                }
            }
            break;
        }
    }
    
    closedir(current_directory);
    
    endwin();
}
