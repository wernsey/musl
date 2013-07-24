/*
 * MUSL standalone interpreter.
 * This file implements a standalone scripting language
 * built on top of the MUSL.
 * It is also intended to serve as an example of how to
 * embed MUSL in your own application, and adds some
 * file handling functions to demonstrate adding your own
 * functions to the interpreter.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "musl.h"

#define INPUT_BUFFER_SIZE 80

#define NUM_FILES 10

/* This structure shows how we can store arbitrary user data in a musl structure.
 * User data is useful if you have several interpreters in the same program that
 * needs to store their own state.
 */
struct user_data {
	FILE * files[NUM_FILES];
};

/* These functions are declared at the bottom of this file.
 * They are additional functions that this program adds to the
 * interpreter.
 * Note that they conform to the mu_func prototype.
 * Look at the comments in the definitions below to see how to
 * add your own functions to a musl interpreter.
 */
static struct mu_par my_print(struct musl *m, int argc, struct mu_par argv[]);
static struct mu_par my_input_s(struct musl *m, int argc, struct mu_par argv[]);
static struct mu_par my_fopen(struct musl *m, int argc, struct mu_par argv[]);
static struct mu_par my_fclose(struct musl *m, int argc, struct mu_par argv[]);
static struct mu_par my_feof(struct musl *m, int argc, struct mu_par argv[]);
static struct mu_par my_fread(struct musl *m, int argc, struct mu_par argv[]);
static struct mu_par my_fwrite(struct musl *m, int argc, struct mu_par argv[]);
static struct mu_par my_srand(struct musl *m, int argc, struct mu_par argv[]);
static struct mu_par my_rand(struct musl *m, int argc, struct mu_par argv[]);
static struct mu_par my_regex(struct musl *m, int argc, struct mu_par argv[]);
static struct mu_par my_call(struct musl *m, int argc, struct mu_par argv[]);
static struct mu_par my_halt(struct musl *m, int argc, struct mu_par argv[]);

int main(int argc, char *argv[]) {
	char *s;
	int r, i;
	struct musl *m;

	struct user_data data;

	srand(time(NULL)); /* For in case */

	for(r = 0; r < NUM_FILES; r++)
		data.files[r] = NULL;

	if(argc <= 1) {
		fprintf(stderr, "Usage: %s FILE1 FILE2 ...\n", argv[0]);
		return 1;
	}

	if(!(m = mu_create())) {
		fprintf(stderr, "ERROR: Couldn't create MUSL structure\n");
		exit(EXIT_FAILURE);
	}

	/* Store a pointer to our user data in the musl structure */
	mu_set_data(m, &data);

	/* Add the custom functions to the interpreter here.
	 * Function names must be in lowercase.
	 */
	mu_add_func(m, "print", my_print);
	mu_add_func(m, "input$", my_input_s);

	mu_add_func(m, "open", my_fopen);
	mu_add_func(m, "close", my_fclose);
	mu_add_func(m, "eof", my_feof);
	/* Note that functions that return strings' names must end with '$' */
	mu_add_func(m, "read$", my_fread);
	mu_add_func(m, "write", my_fwrite);

	mu_add_func(m, "randomize", my_srand);
	mu_add_func(m, "random", my_rand);
	
	mu_add_func(m, "regex", my_regex);

	mu_add_func(m, "call", my_call);
	mu_add_func(m, "halt", my_halt);

	/* You can replace built-in functions with new ones.
	 * You can also disable built-in functions by replacing them
	 * with NULL as in mu_add_func(m, "print", NULL);
	 */

	/* This is how you can set the values of variables */
	mu_set_str(m, "mystr$", "fnord");
	mu_set_num(m, "mynum", 12345);
	/* You can also access array variables like this: */
	mu_set_str(m, "myarray$[foo]", "XYZZY");

	for(i = 1; i < argc; i++) {
		/* mu_readfile() is a helper function to read an entire
		 * script file into memory
		 */
		if(!(s = mu_readfile(argv[i]))) {
			fprintf(stderr, "ERROR: Unable to read \"%s\"\n", argv[i]);
			exit(EXIT_FAILURE);
		}

#ifdef TEST
		printf("============\n%s\n============\n", s);
#endif

		/* Run the script from the string */
		if(!mu_run(m, s)) {
			/* This is how you retrieve info about interpreter errors: */
			fprintf(stderr, "ERROR:Line %d: %s:\n>> %s\n", mu_cur_line(m), mu_error_msg(m),
					mu_error_text(m));
			return 1;
		}

		/* The return value of mu_readfile() also needs to be free()'ed.
		 * Just don't free() it if you're still going to call mu_cur_line()
		 */
		free(s);
	}

#ifdef TEST
	/* This is how you retrieve variable values from the interpreter: */
	printf("============\nmystr$= \"%s\"\nmynum= %d\nmyarray$[foo]= \"%s\"\n",
			mu_get_str(m, "mystr$"), mu_get_num(m, "mynum"), mu_get_str(m, "myarray$[foo]"));
#endif

	/* Destroy the interpreter when we're done with it */
	mu_cleanup(m);

	for(i = 0; i < NUM_FILES; i++)
		if(data.files[i])
			fclose(data.files[i]);

	return 0;
}

/*@ Extended Functions
 *# These functions are available to programs run using the
 *# standalone interpreter:
 *#
 */

/*@ PRINT(exp1, exp2, ...)
 *# Prints all its parameters, followed by a newline
 */
static struct mu_par my_print(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv = {mu_int, {argc}};
	int i;
	for(i = 0; i < argc; i++)
		if(argv[i].type == mu_str)
			fputs(argv[i].v.s, stdout);
		else
			printf("%d", argv[i].v.i);
	printf("\n");
	return rv;
}

/*@ INPUT$([prompt])
 *# Reads a string from the keyboard, with an optional prompt.\n
 *# Trailing newline characters are removed.
 */
static struct mu_par my_input_s(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv;
	char *c;
	if(argc == 0)
		fputs("> ", stdout);
	else
		fputs(mu_par_str(m, 0), stdout);
	fflush(stdout);

	rv.type = mu_str;
	rv.v.s = malloc(INPUT_BUFFER_SIZE + 1);
	if(!rv.v.s) {
		mu_throw(m, "Out of memory");
	}

	rv.v.s[0] = '\0';
	if(fgets(rv.v.s, INPUT_BUFFER_SIZE, stdin))
		for(c=rv.v.s;c[0];c++)
			if(strchr("\r\n",c[0]))
				c[0] = '\0';
	return rv;
}

/*@ OPEN(path, mode)
 *# Opens a given file in a specific mode.\n
 *# {{mode}} can be "r" for reading, "w" for writing, "a" for appending.\n
 *# It returns a number that should be used with {{CLOSE()}}, {{READ()}} and {{WRITE()}}.
 */
static struct mu_par my_fopen(struct musl *m, int argc, struct mu_par argv[]) {
	const char *path, *mode;

	struct mu_par rv; /* Stores the return value of our function */

	struct user_data *data = mu_get_data(m);

	path = mu_par_str(m, 0); /* Get the path from the first parameter */
	mode = mu_par_str(m, 1); /* Get the path from the second parameter */

	/* Our function returns an index into the file table */
	rv.type = mu_int;

	/* Find the first available file handle */
	for(rv.v.i = 0; rv.v.i < NUM_FILES && data->files[rv.v.i]; rv.v.i++);

	if(rv.v.i == NUM_FILES)
		mu_throw(m, "Too many open files");

	data->files[rv.v.i] = fopen(path, mode);
	if(!data->files[rv.v.i])
		mu_throw(m, "Unable to OPEN() file");

	return rv;
}

/*@ CLOSE(f)
 *# Closes a previously opened file.\n
 *# {{f}} is the number of the file previously opened with {{OPEN()}}.
 */
static struct mu_par my_fclose(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv = {mu_int, {0}};
	int index;
	struct user_data *data = mu_get_data(m);

	index = mu_par_num(m, 0);

	if(index < 0 || index > NUM_FILES || !data->files[index])
		mu_throw(m, "Invalid file handle in CLOSE()");
	fclose(data->files[index]);
	data->files[index] = NULL;

	return rv;
}

/*@ EOF(f)
 *# Returns 1 if the end of a file has been reached.\n
 *# {{f}} is the number of the file previously opened with {{OPEN()}}.
 */
static struct mu_par my_feof(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv = {mu_int, {0}};
	int index;
	struct user_data *data = mu_get_data(m);

	index = mu_par_num(m, 0);

	if(index < 0 || index > NUM_FILES || !data->files[index])
		mu_throw(m, "Invalid file handle in EOF()");

	rv.v.i = feof(data->files[index]);

	return rv;
}

/*@ READ$(f)
 *# Reads a line of text from a file.\n
 *# {{f}} is the number of the file previously opened with {{OPEN()}}.\n
 *# It trims the trailing newline.
 */
static struct mu_par my_fread(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv;
	int index, i;
	char buffer[INPUT_BUFFER_SIZE];
	struct user_data *data = mu_get_data(m);

	index = mu_par_num(m, 0);

	if(index < 0 || index > NUM_FILES || !data->files[index])
		mu_throw(m, "Invalid file handle in READ$()");

	if(!fgets(buffer, INPUT_BUFFER_SIZE, data->files[index]))
	{
		if(feof(data->files[index]))
			buffer[0] = '\0';
		else
			mu_throw(m, "Couldn't READ$() from file");
	}

	for(i = 0; buffer[i]; i++)
		if(buffer[i] == '\r' || buffer[i] == '\n')
			buffer[i] = '\0';

	/* NOTE that functions returning strings must allocate those
	 * strings on the heap because musl will try to free() them
	 * when they're no longer in use. Hence the strdup() below.
	 */
	rv.type = mu_str;
	rv.v.s = strdup(buffer);
	if(!rv.v.s)
		mu_throw(m, "Out of memory");
	return rv;
}

/*@ WRITE(f, par1, par2, par, ...)
 *# Writes all its parameters to a file, followed by a newline.\n
 *# {{f}} is the number of the file previously opened with {{OPEN()}}.\n
 *# The remaining parameters can be numbers or strings, and will be
 *# separated by spaces.
 */
static struct mu_par my_fwrite(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv = {mu_int, {0}};
	int index, i;
	struct user_data *data = mu_get_data(m);

	index = mu_par_num(m, 0);

	if(index < 0 || index > NUM_FILES || !data->files[index])
		mu_throw(m, "Invalid file handle in EOF()");

	for(i = 1; i < argc; i++)
	{
		if(argv[i].type == mu_str)
			fputs(argv[i].v.s, data->files[index]);
		else
			fprintf(data->files[index], "%d", argv[i].v.i);
		fputs(i == argc-1?"\n":" ", data->files[index]);
	}

	return rv;
}

/*@ RANDOMIZE([seed])
 *# Initializes the system random number generator with an optional 'seed'
 */
static struct mu_par my_srand(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv = {mu_int, {0}};
		srand(argc?mu_par_num(m, 0):time(NULL));
	return rv;
}

/*@ RANDOM() RANDOM(N) RANDOM(N,M)
 *{
 ** {{RANDOM()}} - Chooses a random number
 ** {{RANDOM(N)}} - Chooses a random number in the range [1, N]
 ** {{RANDOM(N,M)}} - Chooses a random number in the range [N, M]
 *}
 */
static struct mu_par my_rand(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv = {mu_int, {0}};
	rv.v.i = rand();
	if(argc == 1)
		rv.v.i = (rv.v.i % mu_par_num(m,0)) + 1;
	else if(argc == 2)
		rv.v.i = (rv.v.i % (mu_par_num(m,1) - mu_par_num(m,0) + 1)) + mu_par_num(m,0);
	return rv;
}

/*
 * The regex function requires the POSIX regular expression functions
 * regcomp, regexec, regerror, regfree, which may not be available on
 * all platforms.
 */

/*@ REGEX(pattern$, string$)
 *# Matches the regex {{pattern$}} to {{string$}}.\n
 *#      It uses the POSIX Extended regular expression syntax
 *#      described in {{'man 7 regex'}}.\n
 *#      It returns the number of matches/submatches or 0 if {{string$}}
 *#      does not match the {{pattern$}}.\n
 *#      On a successful match, the array {{_M$[]}} will be filled with
 *#      the submatches. Note that {{_M$[]}} is zero indexed: {{_M$[0]}} will
 *#      contain the entire string that matched the regex.\n
 *#      This implies that on a successful match, the largest index in
 *#      {{_M$[]}} will be one less than the return value of {{REGEX()}}
 */
static struct mu_par my_regex(struct musl *m, int argc, struct mu_par argv[])
{
	struct mu_par rv = {mu_int, {0}};
#ifdef WITH_REGEX
	const char *pat = mu_par_str(m, 0);
	const char *str = mu_par_str(m, 1);
	regex_t preg;
	regmatch_t sm[10];
	int r;
	char errbuf[MAX_ERROR_TEXT];

	if((r = regcomp(&preg, pat, REG_EXTENDED)) != 0) {
		regerror(r, &preg, errbuf, MAX_ERROR_TEXT);
		regfree(&preg);
		mu_throw(m, "In REGEX(): %s", errbuf);
	}

	if((r = regexec(&preg, str, 10, sm, 0)) != 0) {
		if(r != REG_NOMATCH) {
			regerror(r, &preg, errbuf, MAX_ERROR_TEXT);
			regfree(&preg);
			mu_throw(m, "In REGEX(): %s", errbuf);
		}
		rv.v.i = 0;
	} else {
		for(r = 0; r < 10; r++) {
			const char *c, *start;
			char *match, name[TOK_SIZE];
			if(sm[r].rm_so < 0)
				break;

			match = mu_alloc(m, sm[r].rm_eo + 1);
			start = str + sm[r].rm_so;
			for(c = start; c - str < sm[r].rm_eo; c++)
				match[c - start] = *c;
			match[c - start] = '\0';

			snprintf(name, TOK_SIZE, "_m$[%d]", r);
			if(!mu_set_str(m, name, match))
				mu_throw(m, "Out of memory");
			free(match);
		}
		rv.v.i = r;
	}
	regfree(&preg);
#else
	mu_throw(m, "This version of Musl was compiled without Regex support");
#endif
	return rv;
}

/*@ CALL(label$)
 *# {{CALL("label")}} is the same as {{GOSUB label}}.\n
 *# {/It is mainly here to test and demo the {{mu_gosub()}} API function./}
 */
static struct mu_par my_call(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv = {mu_int, {0}};
	const char *label = mu_par_str(m, 0);

	rv.v.i = mu_gosub(m, label);
	if(!rv.v.i) {
		/* You really should call mu_throw(),
		 * but you get an opportunity first to
 		 * clean up after yourself */
		mu_throw(m, mu_error_msg(m));
	}
	return rv;
}

/*@ HALT()
 *# Halts the interpreter immediately.\n
 *# Calling this is equivalent to executing an {{END}} statement.\n
 *# {/It is mainly here to test and demo the {{mu_halt()}} API function./}
 */
static struct mu_par my_halt(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv = {mu_int, {0}};
	mu_halt(m);
	return rv;
}
