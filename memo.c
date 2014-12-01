/* Memo is a Unix-style note-taking software.
 *
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
 *
 * memo.c implements a flexible, Unix-style note-taking software.
 *
 * If you're interested hacking Memo, please remember:
 * coding style is pretty much the same as for Linux kernel.
 * see https://www.kernel.org/doc/Documentation/CodingStyle
 *
 * When adding features, please consider if they can be
 * accomplished in a sane way with standard unix tools instead.
 *
 * If you port Memo for another platform, please let me know,
 * no reason for that other than it's nice to know where Memo runs.
 * Memo should be fairly portable for POSIX systems. I don't know
 * about Windows...uninstd.h is not natively available for it(?).
 */

/* To enable _POSIX_C_SOURCE feature test macro */
#define _XOPEN_SOURCE 600

/* Make only POSIX.2 regexp functions available */
#define _POSIX_C_SOURCE 200112L

#ifdef _WIN32
# undef __STRICT_ANSI__
# define S_IRGRP 0
# define S_IROTH 0
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/types.h>
#ifdef _WIN32
# include <pcreposix.h>
#else
# include <regex.h>
#endif
#include <sys/stat.h>

#define ARGCHECK(x, y, z) if (argc < y) { \
	char *err = (char *)malloc((19 + strlen(z)) * sizeof(char));\
	sprintf(err, "Error: -%s missing an argument %s\n", x, z); \
	fail(stderr, err); \
	free(err); \
	usage(); \
	return 1;\
}

typedef enum {
	NOTE_DATE = 1,
	NOTE_CONTENT = 2

} NotePart_t;


/* Function declarations */
static char *read_file_line(FILE *fp);
static int  add_notes_from_stdin();
static char *get_memo_file_path(char *category);
static char *get_memo_default_path();
static char *get_memo_conf_path();
static char *get_temp_memo_path(char *category);
static char *get_memo_conf_value(const char *prop);
static int   is_valid_date_format(const char *date, int silent_errors);
static int   file_exists(const char *path);
static void  remove_content_newlines(char *content);
static int   add_note(char *category, char *content, const char *date);
static int   replace_note(char *category, int id, const char *data);
static int   get_next_id(char *category);
static int   delete_note(char *category, int id);
static int   show_notes(char *category);
static int   show_notes_tree(char *category);
static int   count_file_lines(FILE *fp);
static char  *note_part_replace(NotePart_t part, char *note_line, const char *data);
static int   search_notes(char *category, const char *search);
static int   search_regexp(char *category, const char *regexp);
static const char *export_html(char *category, const char *path);
static void  output_default(char *line);
static void  output_without_date(char *line);
static void  show_latest(char *category, int count);
static FILE *get_memo_file_ptr();
static FILE *get_memo_tmpfile_ptr();
static void  usage();
static void  fail(FILE *out, const char *fmt, ...);
static int   delete_all(char *category);
static void  show_memo_file_path();


#define VERSION 1.4


/* Check if given date is in valid date format.
 * Memo assumes the date format to be yyyy-MM-dd.
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
		if ( !silent_errors)
			fail(stderr,"%s: invalid date format\n", __func__);

		return -1;
	}

	/* Leap year check */
	if (y % 400 == 0 || y % 100 != 0 || y % 4 == 0)
		day_count[1] = 29;

	if (m < 13 && m > 0) {
		if (d <= day_count[m - 1]) {
			return 0;
		}
		else {
			if (!silent_errors)
				fail(stderr, "%s: invalid day\n", __func__);
		}
	} else {
		if (!silent_errors)
			fail(stderr, "%s: invalid month\n", __func__);
	}

	if (!silent_errors)
		fail(stderr, "%s: parsing date failed\n", __func__);

	return -1;
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


/* This function is used to count lines in .memo and ~/.memorc
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


	//return count;
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


/* Get open FILE* for .memo file.
 * Returns NULL of failure.
 * Caller must close the file pointer after calling the function
 * succesfully.
 */
static FILE *get_memo_file_ptr(char *category, char *mode)
{
	FILE *fp = NULL;
	char *path = get_memo_file_path(category);

	if (path == NULL) {
		fail(stderr,"%s: error getting memo path\n",
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
 * Notes are added to the memo file. Returns -1 on failure, 0 on
 * success.
 */
static int add_notes_from_stdin()
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
		add_note("stdin", line, NULL);
		line = strtok(NULL, "\n");
	}

	free(buffer);

	return 0;
}


/* Reads a line from source pointed by FILE*.
 *
 * This function is used to read .memo as well as ~/.memorc
 * files line by line.
 *
 * Return NULL on failure.
 * Caller is responsible for freeing the return value
 */
static char *read_file_line(FILE *fp)
{
	if (!fp)
		return NULL;

	int length = 128;
	char *buffer = NULL;

	buffer = (char*)malloc(sizeof(char) * length);

	if (buffer == NULL) {
		fail(stderr,"%s: malloc failed\n", __func__);
		return NULL;
	}

	int count = 0;

	char ch = getc(fp);

       /* Read char by char until the end of the line.
        * and allocate memory as needed.
        */
	while ((ch != '\n') && (ch != EOF)) {
		if (count == length) {
			length += 128;
			buffer = realloc(buffer, length);

			if (buffer == NULL) {
				fail(stderr,
					"%s: realloc failed\n",
					__func__);
				return NULL;
			}
		}

		buffer[count] = ch;
		count++;
		ch = getc(fp);
	}

	buffer[count] = '\0';
	buffer = realloc(buffer, count + 1);

	return buffer;
}


/* Simply read all the lines from the .memo file
 * and return the id of the last line plus one.
 * If the file is missing or is empty, return 0
 * On error, returns -1
 */
static int get_next_id(char *category)
{
	int id = 0;
	FILE *fp = NULL;
	char *line = NULL;
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

	while (1) {
		line = read_file_line(fp);

		/* Check if we're at the last line */
		if (line && current == lines) {
			char *endptr;
			id = strtol(line, &endptr, 10);
			free(line);
			break;
	        }

		current++;

		if (line)
			free(line);
	}

	fclose(fp);

	return id + 1;
}


/* Show all notes.
 *
 * Returns the number of notes. Returns -1 on failure
 */
static int show_notes(char *category)
{
	FILE *fp = NULL;
	char *line;
	int count = 0;
	int lines = 0;

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

	while (lines >= 0) {
		line = read_file_line(fp);

		if (line) {
			output_default(line);
			free(line);
		}

		lines--;
	}

	fclose(fp);

	return count;
}


/* Function returns the date string of the note.
 *
 * On success caller must free the return value. 
 * NULL is returned on failure.
 */
static char *get_note_date(char *line)
{
	char *date = NULL;
	char *tmpline = strdup(line);
	char *datetoken = NULL;

	if (tmpline == NULL)
		return NULL;

	datetoken = strtok(tmpline, "\t");
	datetoken = strtok(NULL, "\t");
	datetoken = strtok(NULL, "\t");

	if (datetoken == NULL) {
		free(tmpline);
		return NULL;
	}

	date = (char*)malloc((strlen(datetoken) + 1) * sizeof(char));
	
	if (date == NULL) {
		free(tmpline);
		return NULL;
	}

	strcpy(date, datetoken);

	free(tmpline);

	return date;
}


static void output_without_date(char *line)
{
	char *token = strtok(line, "\t");

	if (token != NULL)
		printf("\t%s\t", token);
	else
		goto error;

	token = strtok(NULL, "\t");

	if (token != NULL)
		printf("%s\t", token);
	else
		goto error;

	token = strtok(NULL, "\t");
	token = strtok(NULL, "\t");

	if (token != NULL)
		printf("%s\n", token);
	else
		goto error;

	return;

error:
	fail(stderr, "%s parsing line failed\n", __func__);

}


/* Function displays notes ordered by date.
 *
 * For example:
 *
 *   2014-11-01
 *         1   Release Memo 1.3
 *         2   Pay rent
 *   2014-11-02
 *         3   Go shopping
 *
 * Returns the count of the notes. On failure returns -1.
 */
static int show_notes_tree(char *category)
{
	int count = 0;
	int lines = 0;
	FILE *fp = NULL;
	int date_index = 0;

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

	int n = lines;
	char *dates[lines + 1];

	memset(dates, 0, sizeof(dates));

	/* Get the date of each note and store the pointer
	 * of it to dates array
	 */
	while (n >= 0) {
		char *line = read_file_line(fp);
		if (line) {
			char *date = get_note_date(line);
			int has_date = 0;

			if (date == NULL) {
				free(line);
				fclose(fp);
				fail(stderr, "%s problem getting date\n",
					__func__);
				return -1;
			}
			/* Prevent storing duplicate dates*/
			for (int i = 0; i < date_index; i++) {
				if (dates[i]) {
					if (strcmp(dates[i], date) == 0) {
						has_date = 1;
						break;
					}
				}
			}

			/* If dates does not contain date, store it
			 * otherwise free it
			 */
			if (!has_date)
				dates[date_index] = date;
			else
				free(date);

			date_index++;
			count++;
			free(line);
		}
		n--;
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
			n = lines;
			rewind(fp);
			printf("%s\n", dates[i]);
			while (n >= 0) {
				char *line = read_file_line(fp);
				if (line) {
					char *date = get_note_date(line);
					if (date == NULL) {
						free(line);
						fclose(fp);
						return -1;
					}
				
					if (strcmp(date, dates[i]) == 0)
						output_without_date(line);

					free(line);
					free(date);
				}
				n--;
			}
			free(dates[i]);
		}
	}

	fclose(fp);

	return count;
}


/* Search if a note contains the search term.
 * Returns the count of found notes or -1 if function fails.
 */
static int search_notes(char *category, const char *search)
{
	FILE *fp = NULL;
	int count = 0;
	char *line;
	int lines = 0;

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

	while (lines >= 0) {
		line = read_file_line(fp);

		if (line) {
			/* Check if the search term matches */
			const char *tmp = line;

			if ((strstr(tmp, search)) != NULL){
				output_default(line);
				count++;
			}

			free(line);
		}

		lines--;
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
	char *line = NULL;
	char lines = 0;
	FILE *fp = NULL;
	char buffer[100];

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

	while (lines >= 0) {
		line = read_file_line(fp);

		if (line) {
			ret = regexec(&regex, line, 0, NULL, 0);

			if (ret == 0) {
				output_default(line);
				count++;
			} else if (ret != 0 && ret != REG_NOMATCH) {
				/* Something went wrong while executing
				   regexp. Clean up and exit loop. */
				regerror(ret, &regex, buffer, sizeof(buffer));
				fail(stderr, "%s: %s\n", __func__, buffer);
				free(line);

				break;
			}

			free(line);
		}

		lines--;
	}

	regfree(&regex);
	fclose(fp);

	return count;
}


/* This functions handles the output of one line.
 * Postponed notes are ignored.
 */
static void output_default(char *line)
{
	printf("%s\n", line);
}


/* Export current .memo file to a html file
 * Return the path of the html file, or NULL on failure.
 */
static const char *export_html(char *category, const char *path)
{
	FILE *fp = NULL;
	FILE *fpm = NULL;
	char *line = NULL;
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
	fprintf(fp, "<title>Memo notes</title>\n");
	fprintf(fp, "<style>pre{font-family: sans-serif;}</style>\n");
	fprintf(fp, "</head>\n<body>\n");
	fprintf(fp, "<h1>Notes from Memo</h1>\n");
	fprintf(fp, "<table>\n");

	while (lines >= 0) {
		line = read_file_line(fpm);

		if (line) {
			fprintf(fp, "<tr><td><pre>%s</pre></td></tr>\n",
				line);
			free(line);
		}

		lines--;
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
	char *line;
	int lines = 0;
	int start;
	int current = 0;

	fp = get_memo_file_ptr(category, "r");

	lines = count_file_lines(fp);

	if (lines != -1) {
		/* If n is bigger than the count of lines or smaller
		 * than zero we will show all the lines.
		 */
		if (n > lines || n < 0)
			start = -1;
		else
			start = lines - n;

		while (lines >= 0) {
			line = read_file_line(fp);

			if (line) {
				if (current > start)
					printf("%s\n", line);
				free(line);
			}

			lines--;
			current++;
		}

		fclose(fp);
	} else
		fail(stderr,"%s: counting lines failed\n", __func__);
}


/* Deletes all notes. Function actually
 * simply removes .memo file.
 * Returns 0 on success, -1 on failure.
 */
static int delete_all(char *category)
{
	char *confirm = NULL;
	int ask = 1;

	confirm = get_memo_conf_value("MEMO_CONFIRM_DELETE");

	if (confirm) {

		if (strcmp(confirm, "no") == 0)
			ask = 0;

		free(confirm);
	}

	char *path = get_memo_file_path(category);

	if (path == NULL) {
		fail(stderr,"%s error getting memo file path\n", __func__);
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
 * Returns 0 on success and -1 on failure.
 */
static int delete_note(char *category, int id)
{
	fail(stderr, "%s: deleting notes not implemented right now\n", __func__);
	return 0;
}


/* Return the path to $HOME/.memorc.  On failure NULL is returned.
 * Caller is responsible for freeing the return value.
 */
static char *get_memo_conf_path()
{
	char *env = NULL;
	char *conf_path = NULL;
	size_t len = 0;

	env = getenv("HOME");
#ifdef _WIN32
	if (env == NULL)
		env = getenv("USERPROFILE");
#endif

	if (env == NULL){
		fail(stderr,"%s: getenv(\"HOME\") failed\n", __func__);
		return NULL;
	}

	/* +1 for \0 byte */
	len = strlen(env) + 1;

	/* +8 to have space for \"/.memorc\" */
	conf_path = (char*)malloc( (len + 8) * sizeof(char));

	if (conf_path == NULL) {
		fail(stderr, "%s: malloc failed\n", __func__);
		return NULL;
	}

	strcpy(conf_path, env);
	strcat(conf_path, "/.memorc");

	return conf_path;
}


/* ~/.memorc file format is following:
 *
 * PROPERTY=value
 *
 * e.g MEMO_PATH=/home/niko/.memo
 *
 * This function returns the value of the property. NULL is returned on
 * failure. On success, caller must free the return value.
 */
static char *get_memo_conf_value(const char *prop)
{
	char *retval = NULL;
	char *conf_path = NULL;
	FILE *fp = NULL;

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

	while (lines >= 0) {

		char *line = read_file_line(fp);

		if (line) {
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

		lines--;
	}

	fclose(fp);
	free(conf_path);

	return retval;
}


/* Returns the default path. Default path is ~/.memo
 * 
 * Caller must free the return value. On failure NULL is returned.
 */
static char *get_memo_default_path()
{
	char *path = NULL;
	size_t len = 0;
	char *env = getenv("HOME");
#ifdef _WIN32
	if (env == NULL)
		env = getenv("USERPROFILE");
#endif

	if (env == NULL){
		fail(stderr,"%s: getenv(\"HOME\") failed\n", __func__);
		return NULL;
	}

	/* +1 for \0 byte */
	len = strlen(env) + 1;

	/* +6 to have space for \"/.memo\" */
	path = (char*)malloc( (len + 6) * sizeof(char));

	if (path == NULL) {
		fail(stderr,"%s: malloc failed\n", __func__);
		return NULL;
	}

	strcpy(path, env);
	strcat(path, "/.memo");

	return path;
}


/* Function reads MEMO_PATH environment variable to see if it's set and
 * uses value from it as a path.  When MEMO_PATH is not set, function
 * reads $HOME/.memorc file. If the file is not found $HOME/.memo is
 * used as a fallback path.
 *
 * Returns the path to .memo file or NULL on failure.  Caller is
 * responsible for freeing the return value.
 */
static char *get_memo_file_path(char *category)
{
	char *path = NULL;
	char *env_path = NULL;

	env_path = getenv("MEMO_PATH");
	/* Try and see if environment variable MEMO_PATH is set
	 * and use value from it as a path */
	if (env_path != NULL) {
		/* +1 for \0 byte */
		path = (char*)malloc((strlen(env_path) + 1) * sizeof(char));

		if (path == NULL) {
			fail(stderr, "%s malloc failed\n", __func__);
			return NULL;
		}
		strcpy(path, env_path);

		return path;
	}

	char *conf_path = NULL;

	conf_path = get_memo_conf_path();

	if (conf_path == NULL)
		return NULL;


	if (!file_exists(conf_path)) {
		/* Config file not found, so fallback to ~/.memo */
		path = get_memo_default_path();

	} else {
		/* Configuration file found, read .memo location
		   from it */
		path = get_memo_conf_value("MEMO_PATH");
		
		if (path == NULL) {
			/* Failed to get the path. Most likely user did not
			 * specify MEMO_PATH in the configuration file at all
			 * and configuration file is used for setting other
			 * properties like MEMO_CONFIRM_DELETE.
			 *
			 * Let's default to ~/.memo
			 */
			path = get_memo_default_path();
		}

	}

	free(conf_path);

	return path;
}


/* Returns temporary .memo.tmp file.  It will be in the same directory
 * as the original .memo file.
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
 * new data. Then the original memo file is replaced with the temporary
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
		fail(stderr, "%s failed to get memo file path\n", __func__);
		fclose(fp);
		fclose(tmpfp);

		return -1;
	}

	tmpfile = get_temp_memo_path(category);
	
	if (tmpfile == NULL) {
		fail(stderr, "%s failed to get memo tmp path\n", __func__);
		fclose(fp);
		fclose(tmpfp);

		free(memofile);

		return -1;
	}

	while (lines >= 0) {
		char *line = read_file_line(fp);

		if (line) {
			char *endptr;
			int curr_id = strtol(line, &endptr, 10);
			if (curr_id == id) {
				/* Found the note to be replaced 
				 * Check if user wants to replace the date
				 * by validating the data as date. Otherwise
				 * assume content is being replaced.
				 */
				if (is_valid_date_format(data, 1) == 0) {
					char *new_line = NULL;
					new_line = note_part_replace(NOTE_DATE,
								line, data);
					if (new_line == NULL) {

						printf("Unable to replace note %d\n", id);

						free(memofile);
						free(tmpfile);
						fclose(fp);
						fclose(tmpfp);

						return -1;
					}

					fprintf(tmpfp, "%s\n", new_line);
					free(new_line);
					
				} else {
					char *new_line = NULL;
					new_line = note_part_replace(NOTE_CONTENT,
								line, data);

					if (new_line == NULL) {

						printf("Unable to replace note %d\n", id);

						free(memofile);
						free(tmpfile);
						fclose(fp);
						fclose(tmpfp);

						return -1;
					}

					fprintf(tmpfp, "%s\n", new_line);
					free(new_line);
				}
			} else {
				fprintf(tmpfp, "%s\n", line);
			}

			free(line);
		}

		lines--;
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


/* .memo file format is following:
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
		fail(stderr,"%s: Error opening memo path\n", __func__);
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
#define HELP "\
SYNOPSIS\n\
\n\
    memo [options]\n\
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
    -o <category>                              Show all notes organized by date\n\
    -p                                         Show current memo file path\n\
    -r <category> <id> [content]/[yyyy-MM-dd]  Replace note content or date\n\
    -s <category>                              Show all notes\n\
\n\
    -                                          Read from stdin\n\
    -h                                         Show short help and exit. This page\n\
    -V                                         Show version number of program\n\
\n\
For more information and examples see man memo(1).\n\
\n\
AUTHORS\n\
    Copyright (C) 2014 Niko Rosvall <niko@ideabyte.net>\n\
\n\
    Released under license GPL-3+. For more information, see\n\
    http://www.gnu.org/licenses\n\
"

	printf(HELP);
}


static void show_memo_file_path()
{
	char *path = NULL;

	path = get_memo_file_path("");

	if (path == NULL) {
		fail(stderr,"%s: can't retrieve path\n", __func__);
	}
	else {
		printf("%s\n", path);
		free(path);
	}
}


/* Program entry point */
int main(int argc, char *argv[])
{
	char *path = NULL;
	int c;
	int has_valid_options = 0;

	path = get_memo_file_path("");

	if (path == NULL)
		return -1;

	if (!file_exists(path)) {
		int fd = open(path, O_RDWR | O_CREAT,
			S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

		if (fd == -1) {
			fail(stderr,"%s: failed to create empty memo\n",
				__func__);
			free(path);
			return -1;
		}

		close(fd);
	}

	opterr = 0;

	if (argc == 1) {
		usage();
		return -1;
	}

	while ((c = getopt(argc, argv, "a:d:D:e:f:F:hi:l:o:pr:s:V")) != -1){
		has_valid_options = 1;

		switch(c) {

		case 'a':
			ARGCHECK("a", 4, "content");
			/* if last arg is valid date, use it */
			if (is_valid_date_format(argv[(argc - 1)], 1) == 0)
				add_note(argv[2], argv[3], argv[(argc - 1)]);
			else
				add_note(argv[2], argv[3], NULL);
			break;
		case 'd':
			ARGCHECK("d", 4, "ID");
			delete_note(argv[2], atoi(argv[3]));
			break;
		case 'D':
			delete_all(optarg);
			break;
		case 'e':
			ARGCHECK("e", 4, "path");
			export_html(argv[2], argv[3]);
			break;
		case 'f':
			ARGCHECK("f", 4, "search string");
			search_notes(argv[2], argv[3]);
			break;
		case 'F':
			ARGCHECK("F", 4, "regex");
			search_regexp(argv[2], argv[3]);
			break;
		case 'h':
			usage();
			break;
		case 'i':
			add_notes_from_stdin();
			break;
		case 'o':
			show_notes_tree(optarg);
			break;
		case 'l':
			ARGCHECK("l", 4, "number");
			show_latest(argv[2], atoi(argv[3]));
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
			printf("Memo version %.1f\n", VERSION);
			break;
		case '?': {
			char copts[11] = "adDefFilors";
			int coptfound = 0;
			for (int i = 0; i < strlen(copts); i++) {
				if (copts[i] == optopt) {
					coptfound = 1;
					printf("Error: -%c missing an argument category\n", optopt);
					usage();
				}
			}
			free(copts);
			if (coptfound == 0)
				printf("invalid option '%c', see memo -h for help\n", optopt);
			break;
		}
		}
	}

	if (argc > 1 && !has_valid_options)
		printf("invalid input, see memo -h for help\n");

	free(path);

	return 0;
}
