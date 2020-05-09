#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <ncurses.h>
#include <string.h>
#include <sys/stat.h>
#include <linux/limits.h>

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
    char name[NAME_MAX];
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

#define BIT(n) (1 << n)

typedef enum
{
    STATE_NONE,
    STATE_LISTING = BIT(2),
    STATE_NEW_FILE = BIT(3),
} State;

typedef enum
{
    ERROR_MESSAGE_DISPLAY_NONE,
    ERROR_MESSAGE_DISPLAY_NEW_FILE,
    ERROR_MESSAGE_DISPLAY_DELETE_FILE
} Error_Message_Display;

typedef struct
{
    WINDOW *left;
    int width;
    int height;
    WINDOW *new_file_box;
    char *error_message;
    Error_Message_Display error_message_display;
    int change_selection;
    char *message_change_directory_new_path;
    Array entries;
    State state;
    int keep_running;
    char current_directory_path[PATH_MAX];
    DIR *current_directory;
    int selection;
    int show_hidden_files;
    Const_Buffer input_buffer;
} Context;

void refresh_windows_that_need_it(Context *context)
{
    refresh();
    wrefresh(context->left);
    if (context->state & STATE_NEW_FILE)
    {
        wrefresh(context->new_file_box);
    }
}

void clear_windows_that_need_it(Context *context)
{
    clear();
    wclear(context->left);
    if (context->state & STATE_NEW_FILE)
    {
        wclear(context->new_file_box);
    }
}

void update_entries(Context *context)
{
    context->entries.count = 0;
    for (struct dirent *ent = readdir(context->current_directory); ent;
         ent = readdir(context->current_directory))
    {
        int add_entry = true;
        if (ent->d_name[0] == '.')
        {
            add_entry = context->show_hidden_files;
        }

        if (add_entry)
        {
            Dir_Entry entry;
            switch (ent->d_type)
            {
                case DT_REG:
                entry.kind = DIR_ENTRY_FILE;
                break;

                case DT_DIR:
                entry.kind = DIR_ENTRY_DIRECTORY;
                break;
            }
            entry.name_length = strlen(ent->d_name);
            memcpy(entry.name, ent->d_name, entry.name_length);
            entry.name[entry.name_length] = 0;
            array_add(&context->entries, entry);
        }
    }
    rewinddir(context->current_directory);

    if (context->change_selection)
    {
        context->selection = 0;
    }

    context->change_selection = true;
}

void create_file(Context *context)
{
    struct stat s;
    int exist = stat(context->input_buffer.memory, &s);
    if (exist == 0)
    {
        context->error_message = "That file already exists!";
        context->error_message_display = ERROR_MESSAGE_DISPLAY_NEW_FILE;
        return;
    }

    FILE *file = fopen(context->input_buffer.memory, "w");
    fclose(file);

    memset(context->input_buffer.memory, 0, context->input_buffer.count);
    context->input_buffer.count = 0;
    context->state &= ~STATE_NEW_FILE;
    delwin(context->new_file_box);
    curs_set(0);
}

void delete_file(Context *context)
{
    if (remove(context->entries.buffer[context->selection].name) == -1)
    {
        context->error_message = "Cannot delete that file!";
        context->error_message_display = ERROR_MESSAGE_DISPLAY_DELETE_FILE;
        return;
    }

    context->change_selection = false;
    if (context->selection == context->entries.count - 1)
    {
        context->selection--;
    }
}

void change_directory(Context *context, char *new_path)
{
    chdir(new_path);
    getcwd(context->current_directory_path, PATH_MAX);
    closedir(context->current_directory);
    context->current_directory = opendir(context->current_directory_path);
}

void draw_and_handle_input_for_new_file_box(Context *context)
{
    box(context->new_file_box, 0, 0);
    char *message = "Name New File";
    mvwprintw(context->new_file_box,
              0,
              (getmaxx(context->new_file_box) / 2) - (strlen(message) / 2),
              "%s", message);
    mvwprintw(context->new_file_box, 1, 1, "%s", context->input_buffer.memory);
    int c = wgetch(context->new_file_box);
    Const_Buffer *buffer = &context->input_buffer;
    switch (c)
    {
        case 127:
        if (buffer->count > 0)
        {
            buffer->count--;
            buffer->memory[buffer->count] = 0;
            wdelch(context->new_file_box);
        }
        break;

        case '\n':
        create_file(context);
        break;

        case 27:
        context->state &= ~STATE_NEW_FILE;
        memset(context->input_buffer.memory, 0, context->input_buffer.count);
        context->input_buffer.count = 0;
        curs_set(0);
        break;

        default:
        if (buffer->count < getmaxx(context->new_file_box) - 3)
        {
            buffer->memory[buffer->count] = c;
            buffer->count++;
        }
        break;
    }
}

void handle_input_for_listing(Context *context)
{
    int c = wgetch(context->left);
    switch (c)
    {
        case 'q':
        context->keep_running = false;
        break;

        case 'n':
        context->new_file_box = newwin(3,
               context->width / 2,
               context->height / 4,
               (context->width / 2) - (context->width / 4));
        context->state |= STATE_NEW_FILE;
        curs_set(1);
        update_entries(context);
        break;

        case 'd':
        delete_file(context);
        update_entries(context);
        break;

        case 's':
        context->show_hidden_files = !context->show_hidden_files;
        update_entries(context);
        break;

        case 'b':
        change_directory(context, "..");
        update_entries(context);
        break;

        case 'e':
        if (context->entries.buffer[context->selection].kind == DIR_ENTRY_DIRECTORY)
        {
            change_directory(context, context->entries.buffer[context->selection].name);
            update_entries(context);
        }
        break;

        case 'h':
        if (context->selection > 0)
        {
            context->selection--;
        }
        break;

        case 'l':
        if (context->selection < context->entries.count - 1)
        {
            context->selection++;
        }
        break;
    }
}

void draw_listing(Context *context)
{
    int new_width = getmaxx(stdscr);
    int new_height = getmaxy(stdscr);
    if (context->width != new_width || context->height != new_height)
    {
        context->width = new_width;
        context->height = new_height;
        wresize(context->left, context->height - 1, context->width);
    }

    clear_windows_that_need_it(context);

    mvprintw(0, 1, "%s", context->current_directory_path);

    int entry_x = 2;
    int entry_y = 2;

    int i = 0;

    context->entries.position = 0;
    for_each_array (context->entries)
    {
        if (entry_x + it->name_length > context->width)
        {
            entry_y += 2;
            entry_x = 2;
        }

        if (it->kind == DIR_ENTRY_DIRECTORY)
        {
            wattron(context->left, A_UNDERLINE);
        }

        mvwprintw(context->left,
                  entry_y, entry_x,
                  "%s", it->name);

        wattroff(context->left, A_UNDERLINE);

        if (i == context->selection)
        {
            mvwprintw(context->left,
                      entry_y + 1, entry_x, "^");
        }

        entry_x += it->name_length;
        entry_x++;

        i++;
    }

    box(context->left, 0, 0);

    if (context->error_message_display)
    {
        if (context->error_message_display == ERROR_MESSAGE_DISPLAY_NEW_FILE)
        {
            mvwprintw(context->left,
                      getbegy(context->new_file_box) - 2,
                      (getmaxx(context->left) / 2) - (strlen(context->error_message) / 2),
                      "%s",
                      context->error_message);

            memset(context->input_buffer.memory, 0, context->input_buffer.count);
            context->input_buffer.count = 0;
        }

        if (context->error_message_display == ERROR_MESSAGE_DISPLAY_DELETE_FILE)
        {
            mvwprintw(context->left,
                      (context->height / 2 - 4),
                      (context->width / 2) - (strlen(context->error_message) / 2),
                      "%s", context->error_message);
        }
    }

    refresh_windows_that_need_it(context);

    if (context->keep_running)
    {
        int someone_else_is_getting_input = context->state & STATE_NEW_FILE;
        if (someone_else_is_getting_input)
        {
            draw_and_handle_input_for_new_file_box(context);
        }

        if (!someone_else_is_getting_input)
        {
            handle_input_for_listing(context);
        }
    }
}

int main(void)
{
    initscr();
    cbreak();
    raw();
    noecho();
    curs_set(0);

    Context context;
    context.width = getmaxx(stdscr);
    context.height = getmaxy(stdscr);
    context.left = newwin(context.height - 1, context.width, 1, 0);

    getcwd(context.current_directory_path, PATH_MAX);
    context.current_directory = opendir(context.current_directory_path);

    context.selection = 0;
    context.show_hidden_files = false;
    context.input_buffer = const_buffer_new();
    context.new_file_box = NULL;
    context.state = STATE_LISTING;
    context.error_message_display = ERROR_MESSAGE_DISPLAY_NONE;
    context.change_selection = true;
    context.entries = array_new();
    context.new_file_box = NULL;
    context.keep_running = true;
    update_entries(&context);
    while(context.keep_running)
    {
        draw_listing(&context);
    }

    closedir(context.current_directory);

    endwin();
}
