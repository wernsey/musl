/*
 * MUSL: A silly scripting language.
 * This file allows you to embed the MUSL interpreter into
 * your own applications.
 * See main.c for an example of how this is done.
 * See musl.c for implementation details.
 *
 * About terminology:
 * The interpreter is contained in the struct musl.
 * It is executed by the mu_run() function.
 * A script is a string of text containing MUSL code that
 * is evaluated by mu_run() on an interpreter.
 * The interpreter contains variables. Multiple scripts can
 * be executed on the same interpreter (through multiple calls
 * to mu_run()) in which case they will have access to the
 * same variables.
 * The term "external function" is used to describe a function
 * written in C that matches the mu_func prototype and is
 * registered with the interpreter through mu_add_func()
 * so that it can be called from scripts.
 */
#ifndef MUSL_H
#define MUSL_H

/* Version number: */
#define MUSL_VERSION 0.1234

#if defined(__cplusplus) || defined(c_plusplus)
extern "C"
{
#endif
	
/* MUSL interpreter structure */
struct musl;

/* Helper function to read an entire file into memory.
 * The returned pointer should be freed when no loger in use.
 */
char *mu_readfile(const char *fname);

/* Creates an interpreter structure.
 */
struct musl *mu_create();

/* Runs a script through an interpreter structure.
 * Returns 0 if the script contains errors, in which
 * case mu_error_msg() can be used to retrieve a 
 * description of the error, and mu_error_text() can 
 * be used to retrieve the text in the script where
 * the error occured.
 */
int mu_run(struct musl *m, const char *script);

/* Executes a subroutine in a script from an external 
 * function.
 * The purpose of this function is to allow some
 * sort of callback mechanism (musl calls C calls musl).
 * Note that it must only be called from some external
 * function (see mu_func), since only mu_run() knows
 * about the labels of subroutines.
 * It returns 1 on success and 0 on failure, in which
 * case the external function should clean up and then
 * call mu_throw_error() to terminate the script.
 */
int mu_gosub(struct musl *m, const char *label);

/* Stops the interpreter.
 * If you call this from an external function,
 * the interpreter will behave as if it reached 
 * an END statement.
 */
void mu_halt(struct musl *m);

/* Deallocates an interpreter */
void mu_cleanup(struct musl *m);

/* Type of function parameter/return value */
enum mu_ptype {mu_int, mu_str};

/* Function parameter/return type structure */
struct mu_par {
	enum mu_ptype type;
	union {
		int i;
		char *s;
	} v;
};

/* Prototype for external functions. */
typedef struct mu_par (*mu_func)(struct musl *m, int argc, struct mu_par argv[]);

/* Adds a function 'fun' named 'name' to the interpreter.
 * Existing functions are replaced.
 * Returns 0 on failure.
 */
int mu_add_func(struct musl *m, const char *name, mu_func fun);

/* Reports errors that happen in external functions. 
 * Don't call it from anywhere else because it uses
 * longjmp() to return to the mu_execute(). 
 */
void mu_throw_error(struct musl *m, const char *msg, ...);

/* Retrieves a text description of errors that occured 
 */
const char *mu_error_msg(struct musl *m);

/* Retrieves the text in the script where the 
 * last error occured.
 */
const char *mu_error_text(struct musl *m);

/* Returns the current line number (actual line, not the label)
 * in the script where the interpreter was last executing.
 * The memory to which the 'script' parameter of mu_run()
 * points should still be valid, because it is used internally.
 */
int mu_cur_line(struct musl *m);

/* Gets the n'th parameter of a function as a number.
 * Only call it from mu_func() external functions because 
 * it uses mu_throw_error() on errors.
 */
int mu_par_num(struct musl *m, int n);

/* Gets the n'th parameter of a function as a string.
 * Only call it from mu_func() external functions because 
 * it uses mu_throw_error() on errors.
 */
const char *mu_par_str(struct musl *m, int n);

/* Sets the value of a numeric variable.
 * Returns 0 on failure.  
 */
int mu_set_num(struct musl *m, const char *name, int num);

/* Gets the value of a numeric variable. */
int mu_get_num(struct musl *m, const char *name);

/* Sets the value  of a string variable.
 * Rememeber that string variable names must end with $
 * Returns 0  on failure.  
 */
int mu_set_str(struct musl *m, const char *name, const char *val);

/* Retrieves the value  of a string variable.
 * Rememeber that string variable names must end with $
 * Returns NULL on if the value does not exist.  
 */
const char *mu_get_str(struct musl *m, const char *name);

/* Stores arbitrary user data in the musl structure
 * that can later be retrieved with mu_get_data()
 */
void mu_set_data(struct musl *m, void *data);

/* Retrieves user data from the musl structure 
 * that was previously stored with mu_set_data()
 */
void *mu_get_data(struct musl *m);

#if defined(__cplusplus) || defined(c_plusplus)
} /* extern "C" */
#endif
#endif /* MUSL_H */
