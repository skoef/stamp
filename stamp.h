/* Stamp is a Unix-style note-taking software.
 *
 * Copyright (C) 2014 Reinier Schoof <reinier@skoef.net>
 * Copyright (C) 2014 Niko Rosvall <niko@ideabyte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * stamp.c implements a flexible, Unix-style note-taking software.
 * It is forked from memo by Niko Rosvall from version 1.4.
 *
 * If you're interested hacking Stamp, please remember:
 * coding style is pretty much the same as for Linux kernel.
 * see https://www.kernel.org/doc/Documentation/CodingStyle
 *
 * When adding features, please consider if they can be
 * accomplished in a sane way with standard unix tools instead.
 *
 * If you port Stamp for another platform, please let me know,
 * no reason for that other than it's nice to know where Stamp runs.
 * Stamp should be fairly portable for POSIX systems. I don't know
 * about Windows...uninstd.h is not natively available for it(?).
 */

#ifndef _STAMP_H
#define _STAMP_H

typedef enum {
    NOTE_DATE = 1,
    NOTE_CONTENT = 2

} NotePart_t;

struct Note {
    int  id;
    char date[10];
    char *message;
};

static char       *read_file_line(FILE *fp);
static struct      Note read_file_note(FILE *fp);
static int         add_notes_from_stdin(char *category);
static char       *get_memo_file_path(char *category);
static char       *get_memo_default_path();
static char       *get_memo_conf_path();
static char       *get_temp_memo_path(char *category);
static char       *get_memo_conf_value(const char *prop);
static int         is_valid_date_format(const char *date, int silent_errors);
static int         file_exists(const char *path);
static void        remove_content_newlines(char *content);
static int         add_note(char *category, char *content, const char *date);
static int         replace_note(char *category, int id, const char *data);
static int         get_next_id(char *category);
static int         delete_note(char *category, int id);
static int         show_notes(char *category);
static int         show_notes_tree(char *category);
static int         show_categories();
static int         count_file_lines(FILE *fp);
static char       *note_part_replace(NotePart_t part, char *note_line, const char *data);
static int         search_notes(char *category, const char *search);
static int         search_regexp(char *category, const char *regexp);
static const char *export_html(char *category, const char *path);
static struct Note line_to_Note(char *line);
static void        output_default(struct Note note);
static void        output_without_date(struct Note note);
static void        show_latest(char *category, int count);
static FILE       *get_memo_file_ptr();
static FILE       *get_memo_tmpfile_ptr();
static void        usage();
static void        fail(FILE *out, const char *fmt, ...);
static int         delete_all(char *category);
static void        show_memo_file_path();

#define NOTE_FMT "%d\t%s\t%s\n"

#define ARGCHECK(x, y, z) if (argc < y) { \
    char *err = (char *)malloc((19 + strlen(z)) * sizeof(char));\
    sprintf(err, "Error: -%s missing an argument %s\n", x, z); \
    fail(stderr, err); \
    free(err); \
    usage(); \
    return 1;\
}

#define FREENOTE(x) if (x.message) free(x.message);

#define VERSION 1.4

#endif /* _STAMP_H */
