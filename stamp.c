/* Stamp is a Unix-style note-taking software.
 *
 * Copyright (C) 2014 Reinier Schoof <reinier@skoef.net>
 * Copyright (C) 2014 Niko Rosvall <niko@byteptr.com>
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
 * Stamp should be fairly portable for POSIX systems.
 */

/* enable prototyping getline() */
#define _WITH_GETLINE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "stamp.h"

/* Check if given date is in valid date format.
 * Stamp assumes the date format to be yyyy-MM-dd.
 *
 * If silent_errors is 1, no error information will be outputted.
 * When silent_errors != 1, error information is outputted to stderr.
 *
 * Functions returns 0 on success and -1 on failure.
 */
static int is_valid_date_format(const char *date, int silent_errors)
{
	int d;
	int m;
	int y;
	int ret;

	/* contains number of days in each month from jan to dec */
	int day_count[12] = { 31, 28, 31, 30, 31, 30,
			      31, 31, 30, 31, 30, 31 };

	ret = sscanf(date, "%04d-%02d-%02d", &y, &m, &d);

	if (ret != 3) {
		if (!silent_errors)
			fail(stderr,"invalid date format: %s\n", date);

		return -1;
	}

	/* Leap year check */
	if (y % 400 == 0 || y % 100 != 0 || y % 4 == 0)
		day_count[1] = 29;

	/* check month */
	if (m <= 0 || m >= 13) {
		if (!silent_errors)
			fail(stderr, "%s: invalid month %d\n", __func__, m);

		return -1;
	}

	/* check day */
	if (d <= 0 || d > day_count[m - 1]) {
		if (!silent_errors)
			fail(stderr, "%s: invalid day %d\n", __func__, d);

		return -1;
	}

	return 0;
}


/* Functions checks if file exists.
 * This should be more reliable than using access().
 *
 * Returns 1 when the file is found and 0
 * when it's not found.
 *
 * Please note that 0 could be returned even if
 * the file exists but there are other problems accessing it.
 * See http://pubs.opengroup.org/onlinepubs/009695399/functions/stat.html
 */
static int file_exists(const char *path)
{
	int retval = 0;
	struct stat buffer;

	if (stat(path, &buffer) == 0)
		retval = 1;

	return retval;
}


/* This function is used to count lines in .stamp and ~/.stamprc
 * files.
 *
 * Count the lines of the file as a note is always one liner,
 * lines == note count.
 *
 * File pointer is rewinded to the beginning of the file.
 *
 * Returns the count or -1 if the file pointer is null. -2 is
 * returned when there are no lines.
 *
 * Caller must close fp after calling the function successfully.
 */
static int count_file_lines(FILE *fp)
{
	int count = 0;
	int ch = 0;

	if (!fp) {
		fail(stderr,"%s: NULL file pointer\n", __func__);
		return -1;
	}

	/* Count lines by new line characters */
	while (!feof(fp)) {
		ch = fgetc(fp);

		if (ch == '\n')
			count++;
	}


	/* Go to beginning of the file */
	rewind(fp);

	/* return the count, ignoring the last empty line */
	if (count == 0)
		return -2;
	else
		return count - 1;
}


/* A simple error reporting function */
static void fail(FILE *out, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(out, fmt, ap);
	va_end(ap);
}


/* Get a FILE descriptor for temp file.
 * Caller is responsible for closing the
 * file pointer.
 *
 * Return NULL on failure.
 */
static FILE *get_memo_tmpfile_ptr(char *category)
{
	FILE *fp = NULL;
	char *tmp = NULL;

	tmp = get_temp_memo_path(category);

	if (tmp == NULL) {
		fail(stderr, "%s: error getting a temp file\n", __func__);
		return NULL;
	}

	fp = fopen(tmp, "w");

	if (fp == NULL) {
		fail(stderr, "%s: error opening temp file\n", __func__);
		free(tmp);
		return NULL;
	}

	free(tmp);

	return fp;
}


/* Get open FILE* for stamp file.
 * Returns NULL of failure.
 * Caller must close the file pointer after calling the function
 * succesfully.
 */
static FILE *get_memo_file_ptr(char *category, char *mode)
{
	FILE *fp = NULL;
	char *path = get_memo_file_path(category);

	if (path == NULL) {
		fail(stderr,"%s: error getting stamp path\n",
			__func__);
		return NULL;
	}

	fp = fopen(path, mode);

	if (fp == NULL) {
		fail(stderr,"%s: error opening %s\n", __func__, path);
		return NULL;
	}

	free(path);

	return fp;
}


/* Function reads multiple lines from stdin until
 * the end of transmission (^D).
 *
 * Each line is assumed to be the content part of the note.
 *
 * Notes are added to the stamp file. Returns -1 on failure, 0 on
 * success.
 */
static int add_notes_from_stdin(char *category)
{
	/* First get the whole buffer from stdin. Then parse the buffer;
	 * each note is separated by a new line character in the
	 * buffer. Call add_note for each line.
	 */

	int length = 128;
	char *buffer = NULL;
	int count = 0;
	char ch;
	char *line = NULL;

	if ((buffer = (char*)malloc(sizeof(char) * length)) == NULL) {
		fail(stderr, "%s: malloc failed\n", __func__);
		return -1;
	}

	ch = getc(stdin);

	while (ch != EOF) {
		if (count == length) {
			length += 128;
			if ((buffer = realloc(buffer, length)) == NULL) {
				fail(stderr, "%s realloc failed\n", __func__);
				return -1;
			}
		}

		buffer[count] = ch;
		count++;
		ch = getc(stdin);
	}

	buffer[count] = '\0';
	buffer = realloc(buffer, count + 1);

	line = strtok(buffer, "\n");

	while (line != NULL) {
		add_note(category, line, NULL);
		line = strtok(NULL, "\n");
	}

	free(buffer);

	return 0;
}


/* Reads a line from source pointed by FILE*.
 *
 * This function is used to read .stamp as well as ~/.stamprc
 * files line by line.
 *
 * Return NULL on failure.
 * Caller is responsible for freeing the return value
 */
static char *read_file_line(FILE *fp)
{
	if (!fp)
		return NULL;

	char *line = NULL;
    size_t len = 0;
    ssize_t read;

	if ((read = getline(&line, &len, fp)) == -1) {
		return NULL;
	}

	/* strip trailing whitespace */
	char *retval = strtok(line, "\n");

	return retval;
}

/* Wrapper to read_file_line that returns Note instead of char
 *
 * Return NULL on failure.
 */
static struct Note read_file_note(FILE *fp)
{
	struct Note note;
	char *line = read_file_line(fp);
	if (line) {
		note = line_to_Note(line);
		free(line);
	}

	return note;
}

/* Simply read all the lines from the .stamp file
 * and return the id of the last line plus one.
 * If the file is missing or is empty, return 0
 * On error, returns -1
 */
static int get_next_id(char *category)
{
	int id = 0;
	FILE *fp = NULL;
	struct Note note;
	int lines = 0;
	int current = 0;

	fp = get_memo_file_ptr(category, "r");

	lines = count_file_lines(fp);

	if (lines == -1) {
		fail(stderr,"%s: counting lines failed\n", __func__);
		return -1;
	}

	if (lines == -2) {
		fclose(fp);
		return id + 1;
	}

	for (;;) {
		note = read_file_note(fp);

		/* Check if we're at the last line */
		if (note.id && current == lines) {
			id = note.id;
			FREENOTE(note);
			break;
		}

		current++;

		if (note.id)
			FREENOTE(note);
	}

	fclose(fp);

	return id + 1;
}

/* Convert stamp line to Note struct
 */
static struct Note line_to_Note(char *line)
{
	struct Note note;
	char *token = NULL;

	token = strtok(line, "\t");
	if (token)
		note.id = atoi(token);

	token = strtok(NULL, "\t");
	if (token && strlen(token) == 10)
		strcpy(note.date, token);

	token = strtok(NULL, "\n");
	if (token) {
		note.message = (char *) malloc((strlen(token) + 1) * sizeof(char));
		strcpy(note.message, token);
	}

	return note;
}

/* Show all notes.
 *
 * Returns the number of notes. Returns -1 on failure
 */
static int show_notes(char *category)
{
	FILE *fp = NULL;
	int count = 0;
	int lines = 0;
	struct Note note;

	fp = get_memo_file_ptr(category, "r");

	lines = count_file_lines(fp);
	count = lines;

	if (lines == -1) {
		fail(stderr,"%s: counting lines failed\n", __func__);
		return -1;
	}

	/* Ignore empty note file and exit */
	if (lines == -2) {
		fclose(fp);
		return -1;
	}

	for (int i = 0; i <= lines; i++) {
		note = read_file_note(fp);

		if (!note.id)
			continue;

		output_default(note);
		FREENOTE(note);
	}

	fclose(fp);

	return count;
}

static void output_without_date(struct Note note)
{
	printf("\t%d\t%s\n",
		note.id,
		note.message
	);
}


/* Function displays notes ordered by date.
 *
 * For example:
 *
 *   2014-11-01
 *         1   Do dishes
 *         2   Pay rent
 *   2014-11-02
 *         3   Go shopping
 *
 * Returns the count of the notes. On failure returns -1.
 */
static int show_notes_tree(char *category)
{
	int lines = 0;
	FILE *fp = NULL;
	int date_index = 0;
	struct Note note;

	fp = get_memo_file_ptr(category, "r");
	lines = count_file_lines(fp);

	if (lines == -1) {
		fail(stderr, "%s: counting lines failed\n", __func__);
		return -1;
	}

	/* Ignore empty note file and exit */
	if (lines == -2) {
		fclose(fp);
		return -1;
	}

	char *dates[lines + 1];
	memset(dates, 0, sizeof(dates));

	/* Get the date of each note and store the pointer
	 * of it to dates array
	 */
	for (int n = 0; n <= lines; n++) {
		note = read_file_note(fp);
		if (!note.id)
			continue;

		int has_date = 0;
		if (note.date == NULL) {
			FREENOTE(note);
			fclose(fp);
			fail(stderr, "%s problem getting date\n",
				__func__);
			return -1;
		}

		/* Prevent storing duplicate dates */
		for (int i = 0; i < date_index; i++) {
			if (!dates[i])
				continue;

			if (strcmp(dates[i], note.date) == 0) {
				has_date = 1;
				break;
			}
		}

		/* If dates does not contain date, store it
		 * otherwise free it
		 */
		if (!has_date) {
			dates[date_index] = (char *)malloc((strlen(note.date) + 1) * sizeof(char));
			strcpy(dates[date_index], note.date);
			date_index++;
		}

		FREENOTE(note);
	}

	/* Loop through all dates and print all notes for
	 * the date.
	 */
	for (int i = 0; i <= lines; i++) {
		/* Rewind file pointer to beginning every time to
		 * loop all the notes in the file.
		 * It's possible that the array is not fully populated (because
		 * it's allocated to have as many elements as we have notes,
		 * however duplicate dates are not stored at all.
		 * so do a check if the value is null.
		 */
		if (dates[i]) {
			rewind(fp);
			printf("%s\n", dates[i]);
			for (int j = 0; j <= lines; j++) {
				note = read_file_note(fp);
				if (!note.id)
					continue;

				if (note.date == NULL) {
					FREENOTE(note);
					fclose(fp);
					return -1;
				}

				if (strcmp(note.date, dates[i]) == 0)
					output_without_date(note);

				FREENOTE(note);
			}

			free(dates[i]);
		}
	}

	fclose(fp);

	return lines;
}

/* Show all categories of notes
 *
 * Basically just lists files of stamp directory.
 * Returns number of categories on success or -1 if function fails.
 */
static int show_categories()
{
	char *path = get_memo_file_path("");
	if (path == NULL) {
		fail(stderr,"%s: error getting stamp path\n",
			__func__);
		return -1;
	}

	DIR *dir;
	if ((dir = opendir(path)) == NULL) {
		fail(stderr, "%s: could not open stamp path\n", __func__);
		return -1;
	}

	struct dirent *ent;
	FILE *fp;
	int categories = 0;
	while ((ent = readdir(dir)) != NULL) {
		/* only files */
		if (ent->d_type != DT_REG)
			continue;

		fp = get_memo_file_ptr(ent->d_name, "r");
		if (fp == NULL) {
			printf("%s\n", ent->d_name);
			continue;
		}

		categories++;

		int num = count_file_lines(fp);
		if (num < 0)
			printf("%s (empty)\n", ent->d_name);
		else {
			num++;
			printf("%s (%d %s)\n", ent->d_name, num, (num != 1 ? "notes" : "note"));
		}

		fclose(fp);
	}

	closedir(dir);

	return categories;
}

/* Search if a note contains the search term.
 * Returns the count of found notes or -1 if function fails.
 */
static int search_notes(char *category, const char *search)
{
	FILE *fp = NULL;
	int count = 0;
	int lines = 0;
	struct Note note;

	fp = get_memo_file_ptr(category, "r");

	lines = count_file_lines(fp);

	if (lines == -1) {
		fail(stderr,"%s: counting lines failed\n", __func__);
		return -1;
	}

	/* Ignore empty note file and exit */
	if (lines == -2) {
		fclose(fp);
		return -1;
	}

	for (int i = 0; i <= lines; i++) {
		note = read_file_note(fp);

		if (!note.id)
			continue;

		/* Check if the search term matches */
		if ((strstr(note.message, search)) != NULL){
			output_default(note);
			count++;
		}

		FREENOTE(note);
	}

	fclose(fp);

	return count;
}


/* Search using regular expressions (POSIX Basic Regular Expression syntax)
 * Returns the count of found notes or -1 if functions fails.
 */
static int search_regexp(char *category, const char *regexp)
{
	int count = 0;
	regex_t regex;
	int ret;
	char lines = 0;
	FILE *fp = NULL;
	char buffer[100];
	struct Note note;

	ret = regcomp(&regex, regexp, REG_ICASE);

	if (ret != 0) {
		fail(stderr, "%s: invalid regexp\n", __func__);
		return -1;
	}

	fp = get_memo_file_ptr(category, "r");
	lines = count_file_lines(fp);

	if (lines == -1) {
		regfree(&regex);
		fail(stderr,"%s: counting lines failed\n", __func__);
		return -1;
	}

	/* Ignore empty note file and exit */
	if (lines == -2) {
		regfree(&regex);
		return -1;
	}

	for (int i = 0; i <= lines; i++) {
		note = read_file_note(fp);

		if (!note.id)
			continue;

		ret = regexec(&regex, note.message, 0, NULL, 0);

		if (ret == 0) {
			output_default(note);
			count++;
		} else if (ret != 0 && ret != REG_NOMATCH) {
			/* Something went wrong while executing
			   regexp. Clean up and exit loop. */
			regerror(ret, &regex, buffer, sizeof(buffer));
			fail(stderr, "%s: %s\n", __func__, buffer);
			FREENOTE(note);
			break;
		}

		FREENOTE(note);
	}

	regfree(&regex);
	fclose(fp);

	return count;
}


/* This functions handles the output of one line.
 * Postponed notes are ignored.
 */
static void output_default(struct Note note)
{
	printf(NOTE_FMT,
		note.id,
		note.date,
		note.message
	);
}


/* Export current .stamp file to a html file
 * Return the path of the html file, or NULL on failure.
 */
static const char *export_html(char *category, const char *path)
{
	FILE *fp = NULL;
	FILE *fpm = NULL;
	struct Note note;
	int lines = 0;

	fp = fopen(path, "w");

	if (!fp) {
		fail(stderr, "%s: failed to open %s\n", __func__, path);
		return NULL;
	}

	fpm = get_memo_file_ptr(category, "r");
	lines = count_file_lines(fpm);

	if (lines == -1) {
		fail(stderr, "%s: counting lines failed\n", __func__);
		return NULL;
	}

	if (lines == -2) {
		printf("Nothing to export.\n");
		return NULL;
	}

	fprintf(fp,"<!DOCTYPE html>\n");
	fprintf(fp, "<html>\n<head>\n");
	fprintf(fp, "<meta charset=\"UTF-8\">\n");
	fprintf(fp, "<title>Stamp notes: %s</title>\n", category);
	fprintf(fp, "<style>td{font-family: monospace; white-space: pre;}</style>\n");
	fprintf(fp, "</head>\n<body>\n");
	fprintf(fp, "<h1>Notes from Stamp, %s</h1>\n", category);
	fprintf(fp, "<table>\n");

	for (int i = 0; i <= lines; i++) {
		note = read_file_note(fpm);

		if (!note.id)
			continue;

		fprintf(fp, "<tr><td>%d</td><td>%s</td><td>%s</td></tr>\n", note.id, note.date, note.message);
		FREENOTE(note);
	}

	fprintf(fp, "</table>\n</body>\n</html>\n");
	fclose(fp);
	fclose(fpm);

	return path;
}


/* Show latest n notes */
static void show_latest(char *category, int n)
{
	FILE *fp = NULL;
	struct Note note;
	int lines = 0;
	int start;
	int current = 0;

	fp = get_memo_file_ptr(category, "r");

	lines = count_file_lines(fp);

	if (lines == -1) {
		fail(stderr,"%s: counting lines failed\n", __func__);
		return;
	}

	/* If n is bigger than the count of lines or smaller
	 * than zero we will show all the lines.
	 */
	if (n > lines || n < 0)
		start = -1;
	else
		start = lines - n;

	for (int i = 0; i <= lines; i++) {
		note = read_file_note(fp);

		if (!note.id)
			continue;

		if (current++ > start)
			output_default(note);

		FREENOTE(note);
	}

	fclose(fp);
}


/* Deletes all notes. Function actually
 * simply removes category file in .stamp directory
 * Returns 0 on success, -1 on failure.
 */
static int delete_all(char *category)
{
	char *confirm = NULL;
	int ask = 1;

	confirm = get_memo_conf_value("STAMP_CONFIRM_DELETE");

	if (confirm && strcmp(confirm, "no") == 0)
		ask = 0;

	char *path = get_memo_file_path(category);

	if (path == NULL) {
		fail(stderr,"%s error getting stamp file path\n", __func__);
		return -1;
	}

	if (ask) {
		printf("Really delete (y/N)? ");
		char ch = getc(stdin);
		if (ch == 'y' || ch == 'Y') {
			if (remove(path) != 0) {
				fail(stderr, 
					"%s error removing %s\n", __func__, 
					path);
			}
		}
	} else {
		if (remove(path) != 0)
			fail(stderr,"%s error removing %s\n", __func__, path);
	}

	free(path);

	return 0;
}


/* Delete a note by id.
 * This functions loops over all lines from original file,
 * adds them to a temporary file, except the line you want delete,
 * and moves the temporary file over the original file
 *
 * Returns 0 on success and -1 on failure.
 */
static int delete_note(char *category, int id)
{
	FILE *tmpfp = NULL;
	FILE *fp = NULL;
	char *memofile = NULL;
	char *tmpfile = NULL;

	tmpfp = get_memo_tmpfile_ptr(category);
	if (tmpfp == NULL)
		return -1;

	fp = get_memo_file_ptr(category, "r");
	if (fp == NULL) {
		fclose(tmpfp);
		return -1;
	}

	int lines = count_file_lines(fp);
	if (lines < 0) {
		if (lines == -1)
			fail(stderr, "%s: counting lines failed\n", __func__);
		fclose(fp);
		fclose(tmpfp);

		return -1;
	}

	memofile = get_memo_file_path(category);
	if (memofile == NULL) {
		fail(stderr, "%s failed to get stamp file path\n", __func__);
		fclose(fp);
		fclose(tmpfp);

		return -1;
	}

	tmpfile = get_temp_memo_path(category);
	if (tmpfile == NULL) {
		fail(stderr, "%s failed to get stamp tmp path\n", __func__);
		fclose(fp);
		fclose(tmpfp);

		free(memofile);

		return -1;
	}

	int retval = 0;
	int found = 0;
	struct Note note;
	for (int i = 0; i <= lines; i++) {
		note = read_file_note(fp);

		if (!note.id)
			continue;

		/* when ID is found, skip this line  */
		if (note.id == id)
			found = 1;
		else {
			/* write line to tmpfile */
			int written;
			if ((written = fprintf(tmpfp, NOTE_FMT,
				note.id,
				note.date,
				note.message)) < 0) {
				fail(stderr, "%s: failed writing tmpfile: %s (%d)\n",
					strerror(errno), errno);
				retval = -1;
			}

		}

		FREENOTE(note);
	}

	fclose(fp);

	/* if writing to tmpfile went OK, proceed */
	if (retval == 0) {
		/* did we find the ID we were looking for */
		if (found == 1) {
			retval = rename(tmpfile, memofile);
			/* move tmpfile over memofile */
			if (retval == 0)
				printf("note %d removed from category %s\n", id, category);
			else {
				fail(stderr, "could not rename %s to %s\n", tmpfile, memofile);
				if (remove(tmpfile) != 0)
					fail(stderr, "could not clean up %s either\n", tmpfile);
			}
		} else {
			/* ID not found, remove tmpfile */
			remove(tmpfile);
			fail(stderr, "note with ID %d not found in category %s\n", id, category);
			retval = -1;
		}
	}

	free(memofile);
	free(tmpfile);
	fclose(tmpfp);

	return retval;
}


/* Return the path to $HOME/.stamprc.  On failure NULL is returned.
 * Caller is responsible for freeing the return value.
 */
static char *get_memo_conf_path()
{
	char *env = NULL;
	char *conf_path = NULL;
	size_t len = 0;

	env = getenv("HOME");
	if (env == NULL) {
		fail(stderr,"%s: getenv(\"HOME\") failed\n", __func__);
		return NULL;
	}

	/* +1 for \0 byte */
	len = strlen(env) + 1;

	/* +8 to have space for \"/.stamprc\" */
	conf_path = (char*)malloc( (len + 9) * sizeof(char));

	if (conf_path == NULL) {
		fail(stderr, "%s: malloc failed\n", __func__);
		return NULL;
	}

	strcpy(conf_path, env);
	strcat(conf_path, "/.stamprc");

	return conf_path;
}


/* ~/.stamprc file format is following:
 *
 * PROPERTY=value
 *
 * e.g STAMP_PATH=/home/reinier/.stamprc
 *
 * This function returns the value of the property. NULL is returned on
 * failure. On success, caller must free the return value.
 */
static char *get_memo_conf_value(const char *prop)
{
	char *retval = NULL;
	char *conf_path = NULL;
	FILE *fp = NULL;

	/* first, check the environment for config */
	retval = getenv(prop);
	if (retval != NULL)
		return retval;

	/* config not found, check stamprc */
	conf_path = get_memo_conf_path();
	if (conf_path == NULL)
		return NULL;

	fp = fopen(conf_path, "r");

	if (fp == NULL) {
		free(conf_path);
		return NULL;
	}

	int lines = count_file_lines(fp);

	if (lines == -1) {
		fail(stderr, "%s: counting lines failed\n", __func__);
		fclose(fp);
		free(conf_path);

		return NULL;
	}

	for (int i = 0; i <= lines; i++) {
		char *line = read_file_line(fp);

		if (!line)
			continue;

		if (strncmp(line, prop, strlen(prop)) == 0) {

			/* Property found, get the value */
			char *token = strtok(line, "=");
			token = strtok(NULL, "=");

			if (token == NULL) {
				/* property does not have
				 * a value. fail.
				 */
				fail(stderr, "%s: no value\n", prop);
				free(line);

				break;
			}

			size_t len = strlen(token) + 1;
			retval = (char*)malloc(len * sizeof(char));

			if (retval == NULL) {
				fail(stderr,"%s malloc\n", __func__);
				free(line);

				break;
			}

			strcpy(retval, token);
			free(line);

			break;

		}

		free(line);
	}

	fclose(fp);
	free(conf_path);

	return retval;
}


/* Returns the default path. Default path is ~/.stamp
 * 
 * Caller must free the return value. On failure NULL is returned.
 */
static char *get_memo_default_path()
{
	char *path = NULL;
	size_t len = 0;
	char *env = getenv("HOME");

	if (env == NULL) {
		fail(stderr,"%s: getenv(\"HOME\") failed\n", __func__);
		return NULL;
	}

	/* +1 for \0 byte */
	len = strlen(env) + 1;

	/* +6 to have space for \"/.stamp\" */
	path = (char*)malloc( (len + 7) * sizeof(char));

	if (path == NULL) {
		fail(stderr,"%s: malloc failed\n", __func__);
		return NULL;
	}

	strcpy(path, env);
	strcat(path, "/.stamp");

	return path;
}


/* Function reads STAMP_PATH environment variable to see if it's set and
 * uses value from it as a path.  When STAMP_PATH is not set, function
 * reads $HOME/.stamprc file. If the file is not found $HOME/.stamprc is
 * used as a fallback path.
 *
 * Returns the path to category file in .stamp directory or NULL on failure.
 * Caller is responsible for freeing the return value.
 */
static char *get_memo_file_path(char *category)
{
	char *path = get_memo_conf_value("STAMP_PATH");
	if (!path)
		path = get_memo_default_path();

	/* prepare stamp path */
	mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR);
	chmod(path, 0700);

	if (strlen(category) == 0)
		return path;

	/* append category to stamp path
	 * + 2 for leading slash and nul byte
	 */
	char *cat_path = (char *)malloc((strlen(path) + strlen(category) + 2) * sizeof(char));
	strcpy(cat_path, path);
	strcat(cat_path, "/");
	strcat(cat_path, category);

	return cat_path;
}


/* Returns temporary file.  It will be in the stamp directory.
 *
 * Returns NULL on failure.
 */
static char *get_temp_memo_path(char *category)
{
	char *orig = get_memo_file_path(category);

	if (orig == NULL)
		return NULL;

	char *tmp = (char*)malloc(sizeof(char) * (strlen(orig) + 5));

	if (tmp == NULL) {
		free(orig);
		fail(stderr,"%s: malloc failed\n", __func__);
		return NULL;
	}

	strcpy(tmp, orig);
	strcat(tmp, ".tmp");

	free(orig);

	return tmp;
}


/* Remove new lines from the content.  Sometimes user might want to type
 * multiline note. This functions makes it one liner.
 */
static void remove_content_newlines(char *content)
{
	char *i = content;
	char *j = content;

	while (*j != '\0') {
		*i = *j++;
		if (*i != '\n')
			i++;
	}

	*i = '\0';
}


/* Replaces part of the note.
 * data is the new part defined by NotePart_t.
 *
 * Original note line is modified.
 *
 * Caller is responsible for freeing the return value.
 * Returns new note line on success, NULL on failure.
 */
static char *note_part_replace(NotePart_t part, char *note_line, const char *data)
{
	char *new_line = NULL;
	int size = ((strlen(note_line) + strlen(data)) + 1) * sizeof(char);

	new_line = (char*)malloc(size);

	if (new_line == NULL) {
		fail(stderr, "%s: malloc failed\n", __func__);
		return NULL;
	}

	char *token = NULL;

	/* Get the id and copy it */
	if ((token = strtok(note_line, "\t")) != NULL) {
		if (sprintf(new_line, "%s\t", token) < 0)
			goto error_clean_up;
	}
	else {
		goto error_clean_up;
	}

	/* Get the original date */
	if ((token = strtok(NULL, "\t")) == NULL)
		goto error_clean_up;

	if (part == NOTE_DATE) {
		/* Copy data as the new date */
		if (sprintf(new_line + strlen(new_line), "%s\t", data) < 0)
			goto error_clean_up;
	} else {
		/* Copy the original date */
		if (sprintf(new_line + strlen(new_line), "%s\t", token) < 0)
			goto error_clean_up;
	}

	/* Get the original note content */
	if ((token = strtok(NULL, "\t")) == NULL)
		goto error_clean_up;

	if (part == NOTE_CONTENT) {
		/* Copy the data as new content */
		if (sprintf(new_line + strlen(new_line), "%s", data) < 0)
			goto error_clean_up;
	} else {
		/* Copy the original note content, do not write \n because
		 * the token has it already.
		 */
		if (sprintf(new_line + strlen(new_line), "%s", token) < 0)
			goto error_clean_up;
	}

	return new_line;

error_clean_up:
	fail(stderr, "%s: replacing note data failed\n", __func__);
	free(new_line);

	return NULL;
}


/* Function replaces a note content with data.
 *
 * Data can be either a valid date or content.  Replace operation is
 * simply done by creating a temporary file, existing notes are written
 * line by line to it. Line that has matching id will be written with
 * new data. Then the original stamp file is replaced with the temporary
 * one.
 *
 * Returns 0 on success, -1 on failure.
 */
static int replace_note(char *category, int id, const char *data)
{
	FILE *tmpfp = NULL;
	FILE *fp = NULL;
	char *memofile = NULL;
	char *tmpfile = NULL;
	int lines = 0;

	tmpfp = get_memo_tmpfile_ptr(category);

	if (tmpfp == NULL)
		return -1;

	fp = get_memo_file_ptr(category, "r");

	lines = count_file_lines(fp);

	if (lines == -1) {
		fail(stderr, "%s: counting lines failed\n", __func__);
		fclose(tmpfp);

		return -1;
	}

	/* Empty file, ignore. */
	if (lines == -2) {
		fclose(fp);
		fclose(tmpfp);

		return -1;
	}

	memofile = get_memo_file_path(category);

	if (memofile == NULL) {
		fail(stderr, "%s failed to get stamp file path\n", __func__);
		fclose(fp);
		fclose(tmpfp);

		return -1;
	}

	tmpfile = get_temp_memo_path(category);

	if (tmpfile == NULL) {
		fail(stderr, "%s failed to get stamp tmp path\n", __func__);
		fclose(fp);
		fclose(tmpfp);

		free(memofile);

		return -1;
	}

	while (lines-- >= 0) {
		char *line = read_file_line(fp);

		if (!line)
			continue;
		char *endptr;
		int curr_id = strtol(line, &endptr, 10);
		if (curr_id != id) {
			fprintf(tmpfp, "%s\n", line);
			free(line);
			continue;
		}

		/* Found the note to be replaced
		 * Check if user wants to replace the date
		 * by validating the data as date. Otherwise
		 * assume content is being replaced.
		 */
		char *new_line = NULL;
		if (is_valid_date_format(data, 1) == 0)
			new_line = note_part_replace(NOTE_DATE,
						line, data);
		else
			new_line = note_part_replace(NOTE_CONTENT,
						line, data);

		if (new_line == NULL) {
			printf("Unable to replace note %d\n", id);

			free(line);
			free(memofile);
			free(tmpfile);
			fclose(fp);
			fclose(tmpfp);

			return -1;
		}

		fprintf(tmpfp, "%s\n", new_line);

		free(new_line);
		free(line);
	}

	if (file_exists(memofile))
		remove(memofile);

	rename(tmpfile, memofile);
	remove(tmpfile);

	free(memofile);
	free(tmpfile);
	fclose(fp);
	fclose(tmpfp);

	return 0;
}


/* .stamp file format is following:
 *
 * id     date           content
 * |      |              |
 * |- id  |- yyy-MM-dd   |- actual note
 *
 * sections are separated by a tab character
 *
 * Parameter date can be NULL. If date is given in valid
 * format(yyyy-MM-dd) it will be used for creating the note. If date is
 * NULL, current date will be used instead.
 */
static int add_note(char *category, char *content, const char *date)
{
	FILE *fp = NULL;
	time_t t;
	struct tm *ti;
	int id = -1;
	char note_date[11];

	/* Do not add an empty note */
	if (strlen(content) == 0)
		return -1;

	remove_content_newlines(content);

	fp = get_memo_file_ptr(category, "a");

	if (fp == NULL) {
		fail(stderr,"%s: Error opening stamp path\n", __func__);
		return -1;
	}

	id = get_next_id(category);

	if (id == -1)
		id = 1;

	if (date != NULL) {
		/* Date is already validated, so just copy it
		 * for later use.
		 */
		strcpy(note_date, date);
	} else {

		time(&t);
		ti = localtime(&t);

		strftime(note_date, 11, "%Y-%m-%d", ti);
	}


	fprintf(fp, "%d\t%s\t%s\n", id, note_date,
		content);

	fclose(fp);

	return id;
}


static void usage()
{
	printf("SYNOPSIS\n\
\n\
    stamp [options]\n\
\n\
OPTIONS\n\
\n\
    -a <category> <content> [yyyy-MM-dd]       Add a new note with optional date\n\
    -d <category> <id>                         Delete note by id\n\
    -D <category>                              Delete all notes\n\
    -e <category> <path>                       Export notes as html to a file\n\
    -f <category> <search>                     Find notes by search term\n\
    -F <category> <regex>                      Find notes by regular expression\n\
    -i <category>                              Read from stdin until ^D\n\
    -l <category> <n>                          Show latest n notes\n\
    -L                                         List all categories\n\
    -o <category>                              Show all notes organized by date\n\
    -p                                         Show current stamp file path\n\
    -r <category> <id> [content]/[yyyy-MM-dd]  Replace note content or date\n\
    -s <category>                              Show all notes\n\
\n\
    -h                                         Show short help and exit. This page\n\
    -V                                         Show version number of program\n\
\n\
For more information and examples see man stamp(1).\n\
\n\
AUTHORS\n\
\n\
    Copyright (C) 2014 Reinier Schoof <reinier@skoef.net>\n\
    Copyright (C) 2014 Niko Rosvall <niko@byteptr.com>\n\
\n\
    Released under license GPL-3+. For more information, see\n\
    http://www.gnu.org/licenses\n\
");
}


static void show_memo_file_path()
{
	char *path = NULL;

	path = get_memo_file_path("");

	if (path == NULL)
		fail(stderr,"%s: can't retrieve path\n", __func__);
	else
		printf("%s\n", path);
}


/* Program entry point */
int main(int argc, char *argv[])
{
	int c;
	int has_valid_options = 0;
	opterr = 0;

	if (argc == 1) {
		usage();
		return -1;
	}

	int ret = 0;
	int result;
	while ((c = getopt(argc, argv, "a:d:D:e:f:F:hi:l:Lo:pr:s:V")) != -1){
		has_valid_options = 1;

		switch(c) {

			case 'a':
				ARGCHECK("a", 4, "content");
				/* if last arg is valid date, use it */
				if (argc > 4) {
					if (is_valid_date_format(argv[4], 0) == 0)
						add_note(argv[2], argv[3], argv[4]);
					else
						ret = 1;
				} else
					add_note(argv[2], argv[3], NULL);
				break;
			case 'd':
				ARGCHECK("d", 4, "ID");
				if ((result = delete_note(argv[2], atoi(argv[3]))) != 0)
					ret = 2;
				break;
			case 'D':
				if ((result = delete_all(optarg)) != 0)
					ret = 2;
				break;
			case 'e':
				ARGCHECK("e", 4, "path");
				export_html(argv[2], argv[3]);
				break;
			case 'f':
				ARGCHECK("f", 4, "search string");
				if ((result = search_notes(argv[2], argv[3])) == 0)
					ret = 2;
				break;
			case 'F':
				ARGCHECK("F", 4, "regex");
				if ((result = search_regexp(argv[2], argv[3])) == 0)
					ret = 2;
				break;
			case 'h':
				usage();
				break;
			case 'i':
				add_notes_from_stdin(optarg);
				break;
			case 'o':
				show_notes_tree(optarg);
				break;
			case 'l':
				ARGCHECK("l", 4, "number");
				show_latest(argv[2], atoi(argv[3]));
				break;
			case 'L':
				if ((result = show_categories()) <= 0)
					ret = 2;
				break;
			case 'p':
				show_memo_file_path();
				break;
			case 'r':
				ARGCHECK("r", 5, "id, content or date");
				replace_note(argv[2], atoi(argv[3]), argv[4]);
				break;
			case 's':
				show_notes(optarg);
				break;
			case 'V':
				printf("Stamp version %.1f\n", VERSION);
				break;
			case '?': {
				char *copts = "adDefFilors";
				int coptfound = 0;
				for (int i = 0; i < strlen(copts); i++) {
					if (copts[i] == optopt) {
						coptfound = 1;
						printf("Error: -%c missing an argument category\n", optopt);
						usage();
						ret = 1;
						break;
					}
				}

				if (coptfound == 0) {
					printf("invalid option '%c', see stamp -h for help\n", optopt);
					ret = 1;
				}

				break;
			}
		}
	}

	if (argc > 1 && !has_valid_options)
		printf("invalid input, see stamp -h for help\n");

	return ret;
}
