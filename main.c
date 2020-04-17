#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <ncurses.h>
#include <string.h>
#include <sys/stat.h>

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

// A small non dynamic buffer used for things like input
typedef struct
{
    char memory[128];
    int count;
} Const_Buffer;

Const_Buffer const_buffer_new(void)
{
    Const_Buffer buffer;
    memset(buffer.memory, 0, 128);
    buffer.count = 0;

    return buffer;
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
    // Later on we will allow two windows at a time to be rendered
    // WINDOW *right;
    
    int entry_x = 2;
    int entry_y = 2;
    
    int update_directory_entries = true;
    int show_hidden_files = false;
    int selection = 0;

    int draw_new_file_dialog = false;
    int create_new_file = false;
    WINDOW *new_file_dialog = NULL;
    Const_Buffer new_file_buffer = const_buffer_new();
    
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
        if (draw_new_file_dialog)
        {
            wclear(new_file_dialog);
        }
        clear();
        
        box(left, 0, 0);
        mvprintw(0, 1, "%s", cwd_buffer);
        
        // When the user presses 'enter' on the new file dialog
        // box then actually create the file
        if (create_new_file)
        {
            struct stat s;
            int exist = stat(new_file_buffer.memory, &s);
            if (exist == 0)
            {
                // Just exit for now if a the file we want to create
                // already exists.
                keep_running = false;
                continue;
            }

            FILE *file = fopen(new_file_buffer.memory, "w");
            fclose(file);

            memset(new_file_buffer.memory, 0, new_file_buffer.count);
            new_file_buffer.count = 0;
            create_new_file = false;
            selection = 0;
            rewinddir(current_directory);
            update_directory_entries = true;
        }

        if (update_directory_entries)
        {
            entries.count = 0;
        }
        
        // Only update the entries array when we need to
        // like if the current directory changes or
        // if we need to reiterate over the directory
        // again if hidden files are toggled or a file is created or deleted
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

        // @Bug
        // The new file dialog does not resize itself
        // when the dimensions of the terminal changes
        if (draw_new_file_dialog)
        {
            box(new_file_dialog, 0, 0);
            char *message = "Name new file";
            int dialog_width = getmaxx(new_file_dialog);
            mvwprintw(new_file_dialog, 0, (dialog_width / 2) - strlen(message) / 2, "%s", message);
            mvwprintw(new_file_dialog, 1, 1, "%s", new_file_buffer.memory);
        }
        
        entry_y = 2;
        entry_x = 2;
        
        refresh();
        wrefresh(left);
        if (draw_new_file_dialog)
        {
            wrefresh(new_file_dialog);
        }
        
        // Get input from the standard screen
        // if the new file dialog is not up
        // and get input from the dialog if it is being rendered
        int c;
        if (!draw_new_file_dialog)
        {
            c = getch();
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

                case 'n':
                {
                    new_file_dialog = newwin(3, width / 2, 10, (width / 2) - (width / 4));
                    draw_new_file_dialog = true;
                }
                break;

                case 'd':
                {
                    // @Bug
                    // This might fail becase we may not have privileges
                    // to delete this file
                    remove(entries.buffer[selection].name);
                    rewinddir(current_directory);
                    selection = 0;
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
        } else
        {
            c = wgetch(new_file_dialog);
            switch(c)
            {
                case KEY_ENTER:
                case '\n':
                {
                    draw_new_file_dialog = false;
                    delwin(new_file_dialog);
                    create_new_file = true;
                }
                break;

                case KEY_BACKSPACE:
                case 127:
                {

                    if (new_file_buffer.count > 0)
                    {
                        new_file_buffer.count--;
                        new_file_buffer.memory[new_file_buffer.count] = 0;
                        wdelch(new_file_dialog);
                    }
                }
                break;

                default:
                {
                    if (new_file_buffer.count < 128)
                    {
                        new_file_buffer.memory[new_file_buffer.count] = c;
                        new_file_buffer.count++;
                    }
                }
            }
        }
    }
    
    closedir(current_directory);
    
    endwin();
}
