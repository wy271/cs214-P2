#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#define BUFSIZE 4096

typedef struct {
    char *data;
    int length;
    int capacity;
} word_buffer_t;

typedef struct {
    char *word;
    int count;
} word_entry_t;

typedef struct {
    word_entry_t *entries;
    int size;
    int capacity;
} word_table_t;

enum {
    OUT_WORD,
    IN_WORD
};

static char *suffix = ".txt";
static char **input_paths;
static int num_input_paths;

static word_table_t word_table;
static word_buffer_t current_word;

static void parse_args(int argc, char **argv);
static void process_path(const char *path, int from_command_line);
static void scan_directory(const char *path);
static char *join_path(const char *parent, const char *name);
static int ends_with(const char *filename, const char *suffix);
static void count_words_in_file(const char *path);
static int is_letter(char ch);
static void reset_word(void);
static void append_to_word(char ch);
static void commit_word(void);
static void add_word_to_table(const char *word, int length);
static int compare_entries(const void *a, const void *b);
static void print_word_counts(void);

int main(int argc, char **argv)
{
    parse_args(argc, argv);

    for (int i = 0; i < num_input_paths; i++)
        process_path(input_paths[i], 1);

    qsort(word_table.entries, word_table.size, sizeof(word_entry_t),
          compare_entries);

    print_word_counts();

    return 0;
}

static void parse_args(int argc, char **argv)
{
    input_paths = malloc(argc * sizeof(char *));
    if (!input_paths) {
        fprintf(stderr, "words: out of memory\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 1; i < argc; ) {
        if (argv[i][0] == '-' && argv[i][1] == 's') {
            if (argv[i][2] == '\0') {
                if (i + 1 >= argc) {
                    fprintf(stderr, "words: missing suffix after -s\n");
                    exit(EXIT_FAILURE);
                }
                suffix = argv[i + 1];
                i += 2;
            } else {
                suffix = argv[i] + 2;
                i += 1;
            }
        } else {
            input_paths[num_input_paths] = argv[i];
            num_input_paths += 1;
            i += 1;
        }
    }

    if (num_input_paths == 0) {
        fprintf(stderr, "words: no files or directories given\n");
        exit(EXIT_FAILURE);
    }
}

static void process_path(const char *path, int from_command_line)
{
    struct stat file_info;
    if (stat(path, &file_info) < 0) {
        perror(path);
        exit(EXIT_FAILURE);
    }

    if (S_ISDIR(file_info.st_mode)) {
        scan_directory(path);
    } else if (S_ISREG(file_info.st_mode)) {
        if (from_command_line || ends_with(path, suffix))
            count_words_in_file(path);
    }
}

static void scan_directory(const char *path)
{
    DIR *dirp = opendir(path);
    if (!dirp) {
        perror(path);
        exit(EXIT_FAILURE);
    }

    struct dirent *entry;
    while ((entry = readdir(dirp)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        char *child = join_path(path, entry->d_name);
        process_path(child, 0);
        free(child);
    }

    closedir(dirp);
}

static char *join_path(const char *parent, const char *name)
{
    char *joined = malloc(strlen(parent) + 1 + strlen(name) + 1);
    if (!joined) {
        fprintf(stderr, "words: out of memory\n");
        exit(EXIT_FAILURE);
    }

    strcpy(joined, parent);
    strcat(joined, "/");
    strcat(joined, name);
    return joined;
}

static int ends_with(const char *filename, const char *suffix)
{
    int filename_len = strlen(filename);
    int suffix_len = strlen(suffix);

    if (suffix_len > filename_len)
        return 0;

    return strcmp(filename + (filename_len - suffix_len), suffix) == 0;
}

static void count_words_in_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        exit(EXIT_FAILURE);
    }

    char buf[BUFSIZE];
    int state = OUT_WORD;
    int pending_hyphen = 0;
    int bytes_read;

    while ((bytes_read = read(fd, buf, BUFSIZE)) > 0) {
        for (int i = 0; i < bytes_read; i++) {
            char ch = buf[i];

            if (is_letter(ch)) {
                if (pending_hyphen) {
                    append_to_word('-');
                    pending_hyphen = 0;
                }
                append_to_word(ch);
                state = IN_WORD;
            } else if (ch == '\'') {
                if (pending_hyphen) {
                    pending_hyphen = 0;
                    commit_word();
                    state = OUT_WORD;
                }
                append_to_word(ch);
                state = IN_WORD;
            } else if (ch == '-') {
                if (state == IN_WORD && !pending_hyphen) {
                    pending_hyphen = 1;
                } else if (state == IN_WORD && pending_hyphen) {
                    pending_hyphen = 0;
                    commit_word();
                    state = OUT_WORD;
                }
            } else {
                if (state == IN_WORD) {
                    pending_hyphen = 0;
                    commit_word();
                    state = OUT_WORD;
                }
            }
        }
    }

    if (bytes_read < 0) {
        perror(path);
        exit(EXIT_FAILURE);
    }

    if (state == IN_WORD) {
        pending_hyphen = 0;
        commit_word();
    }

    close(fd);
}

static int is_letter(char ch)
{
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
        return 1;
    } else {
        return 0;
    }
}

static void reset_word(void)
{
    current_word.length = 0;
}

static void append_to_word(char ch)
{
    if (current_word.length + 2 > current_word.capacity) {
        if (current_word.capacity == 0)
            current_word.capacity = 16;
        else
            current_word.capacity *= 2;
        current_word.data =
            realloc(current_word.data, current_word.capacity);
        if (!current_word.data) {
            fprintf(stderr, "words: out of memory\n");
            exit(EXIT_FAILURE);
        }
    }

    current_word.data[current_word.length] = ch;
    current_word.length += 1;
    current_word.data[current_word.length] = '\0';
}

static void commit_word(void)
{
    if (current_word.length == 0)
        return;

    add_word_to_table(current_word.data, current_word.length);
    reset_word();
}

static void add_word_to_table(const char *word, int length)
{
    for (int i = 0; i < word_table.size; i++) {
        if (strcmp(word_table.entries[i].word, word) == 0) {
            word_table.entries[i].count += 1;
            return;
        }
    }

    if (word_table.size == word_table.capacity) {
        if (word_table.capacity == 0)
            word_table.capacity = 16;
        else
            word_table.capacity *= 2;
        word_table.entries =
            realloc(word_table.entries,
                    word_table.capacity * sizeof(word_entry_t));
        if (!word_table.entries) {
            fprintf(stderr, "words: out of memory\n");
            exit(EXIT_FAILURE);
        }
    }

    word_entry_t *entry = &word_table.entries[word_table.size];
    entry->word = malloc(length + 1);
    if (!entry->word) {
        fprintf(stderr, "words: out of memory\n");
        exit(EXIT_FAILURE);
    }
    memcpy(entry->word, word, length);
    entry->word[length] = '\0';
    entry->count = 1;
    word_table.size += 1;
}

static int compare_entries(const void *a, const void *b)
{
    const word_entry_t *entry_a = a;
    const word_entry_t *entry_b = b;

    if (entry_a->count != entry_b->count)
        return entry_b->count - entry_a->count;

    return strcmp(entry_a->word, entry_b->word);
}

static void print_word_counts(void)
{
    for (int i = 0; i < word_table.size; i++) {
        word_entry_t *entry = &word_table.entries[i];

        char number[24];
        int number_len = sprintf(number, "%d", entry->count);

        write(1, entry->word, strlen(entry->word));
        write(1, " ", 1);
        write(1, number, number_len);
        write(1, "\n", 1);
    }
}
