/*
 * Musl: A silly scripting language.
 * This file allows you to embed the Musl interpreter into
 * your own applications.
 *
 *2 API
 *# {*Musl*} provides an API through which it can be embedded into
 *# other applications in order to provide a scripting interface.\n
 *# About terminology:\n
 *{
 ** The {/interpreter/} is contained in the {{struct ~~musl}}.
 *# It is created with the {{~~mu_create()}} function.
 ** A {/script/} is a string of text containing Musl code that
 *# is evaluated by {{~~mu_run()}} on an interpreter.
 ** The interpreter contains {/variables/}. Multiple scripts can
 *# be executed on the same interpreter (through multiple calls
 *# to {{~~mu_run()}}) in which case they will have access to the
 *# same variables.\n
 ** The term {/"external function"/} is used to describe a function
 *# written in C that matches the {{~~mu_func}} prototype and is
 *# registered with the interpreter through {{~~mu_add_func()}}
 *# so that it can be called from scripts.
 *}
 *# For more information:
 *{ 
 ** See {{main.c}} for an example of how to add Musl to your applications.
 ** See {{musl.c}} for details about the implementation of the API.
 *}
 */
#ifndef MUSL_H
#define MUSL_H

/* Version number: */
#define MUSL_VERSION 0.1234

#if defined(__cplusplus) || defined(c_plusplus)
extern "C"
{
#endif
	
/*@ struct ##musl
 *# The {*Musl*} structure that contains the state of the interpreter.\n
 *# It is created with {{~~mu_create()}}.\n
 *# A script is run on the interpreter with {{~~mu_run()}}.\n
 *# The structure is destroyed with {{~~mu_cleanup()}}.
 */
struct musl;

/*@ char *##mu_readfile(const char *fname)
 *# Helper function to read an entire file into memory.
 *# The returned pointer should be {{free()}}'d when no loger in use.
 */
char *mu_readfile(const char *fname);

/*@ struct musl *##mu_create()
 *# Creates a {{~~musl}} interpreter structure.\n
 *# Use {{~~mu_run()}} to execute a script against the new interpreter.\n
 *# Use {{~~mu_cleanup()}} to deallocate it's memory when it is no longer in use.\n
 *# It will return {{NULL}} if a {{malloc()}} failed.
 */
struct musl *mu_create();

/*@ void ##mu_cleanup(struct musl *m)
 *# Deallocates an interpreter.
 */
void mu_cleanup(struct musl *m);

/*@ int ##mu_run(struct musl *m, const char *script)
 *# Runs a script through an interpreter structure.\n
 *# Returns 0 if the script contains errors, in which
 *# case {{~~mu_error_msg()}} can be used to retrieve a 
 *# description of the error, and {{~~mu_error_text()}} can 
 *# be used to retrieve the text in the script where
 *# the error occured.
 */
int mu_run(struct musl *m, const char *script);
 
/*@ int ##mu_gosub(struct musl *m, const char *label)
 *# Executes a subroutine in a script from an external 
 *# function.\n
 *# The purpose of this function is to allow some
 *# sort of callback mechanism (musl calls C calls musl).\n
 *# Note that it must only be called from some external
 *# function (see {{~~mu_func}}), since only {{~~mu_run()}} knows
 *# about the labels of subroutines.\n
 *# It returns 1 on success and 0 on failure, in which
 *# case the external function should clean up and then
 *# call {{~~mu_throw()}} to terminate the script.
 */
int mu_gosub(struct musl *m, const char *label);

/*@ void ##mu_halt(struct musl *m)
 *# Stops the interpreter.\n
 *# If you call this from an external function,
 *# the interpreter will behave as if it reached 
 *# an END statement.
 */
void mu_halt(struct musl *m);

/*@ enum ##mu_ptype {mu_int, mu_str}
 *# Type of function parameter/return value.
 *# See {{~~mu_par}}'s {{type}} field. 
 */
enum mu_ptype {mu_int, mu_str};

/*@ struct ##mu_par
 *# Function parameter/return type structure.
 *[
 *# struct mu_par {
 *#   enum mu_ptype type;
 *#   union {
 *#     int i;
 *#     char *s;
 *#   } v;
 *# }
 *]
 */
struct mu_par {
	enum mu_ptype type;
	union {
		int i;
		char *s;
	} v;
};

/*@ typedef struct mu_par (*##mu_func)(struct musl *m, int argc, struct mu_par argv[])
 *# Prototype for external functions that are added through {{~~mu_add_func()}}. 
 *{
 ** {{m}} is the interpreter. You can use it as a parameter to other API funtions.
 ** {{argc}} is the number of parameters - the number of elements in the {{argv}} array.
 ** {{argv}} is the parameters to the function.
 *}
 */
typedef struct mu_par (*mu_func)(struct musl *m, int argc, struct mu_par argv[]);

/*@ int ##mu_add_func(struct musl *m, const char *name, mu_func fun)
 *# Adds a {{~~mu_func}} function {{fun}} named {{name}} to the interpreter.\n
 *# Existing functions are replaced.\n
 *# Returns 0 on failure.
 */
int mu_add_func(struct musl *m, const char *name, mu_func fun);

/*@ void ##mu_throw(struct musl *m, const char *msg, ...)
 *# Reports errors that happen in external functions.
 *N Only call it from within external functions.
 *# Don't call it from anywhere else because it uses
 *# {{longjmp()}} to return to the {{~~mu_run()}}. 
 */
void mu_throw(struct musl *m, const char *msg, ...);

/*@ const char *##mu_error_msg(struct musl *m)
 *# Retrieves a text description of errors that occured.
 */
const char *mu_error_msg(struct musl *m);

/*@ const char *##mu_error_text(struct musl *m)
 *# Retrieves the text in the script where the 
 *# last error occured.
 */
const char *mu_error_text(struct musl *m);

/*@ int ##mu_cur_line(struct musl *m)
 *# Returns the current line number (actual line, not the label)
 *# in the script where the interpreter was last executing.\n
 *# The memory to which the {{script}} parameter of {{~~mu_run()}}
 *# points should still be valid, because it is used internally.
 */
int mu_cur_line(struct musl *m);

/*@ int ##mu_par_int(struct musl *m, int n)
 *# Gets the {{n}}'th parameter of a function as a number.\n
 *# Only call it from {{~~mu_func()}} external functions because 
 *# it uses {{~~mu_throw()}} on errors.
 */
int mu_par_int(struct musl *m, int n, int argc, struct mu_par argv[]);

/*@ const char *##mu_par_str(struct musl *m, int n)
 *# Gets the {{n}}'th parameter of a function as a string.\n
 *# Only call it from {{~~mu_func()}} external functions because 
 *# it uses {{~~mu_throw()}} on errors.
 */
const char *mu_par_str(struct musl *m, int n, int argc, struct mu_par argv[]);

/*@ int ##mu_set_int(struct musl *m, const char *name, int num)
 *# Sets the value of a numeric variable.\n
 *# Returns 0 on failure.  
 */
int mu_set_int(struct musl *m, const char *name, int num);

/*@ int ##mu_get_int(struct musl *m, const char *name)
 *# Gets the value of a numeric variable. 
 */
int mu_get_int(struct musl *m, const char *name);

/*@ int ##mu_set_str(struct musl *m, const char *name, const char *val)
 *# Sets the value of a variable to a string.\n
 *# Returns 0 on failure.  
 */
int mu_set_str(struct musl *m, const char *name, const char *val);

/*@ const char *##mu_get_str(struct musl *m, const char *name)
 *# Retrieves the value of a variable as a string.\n
 *# Returns {{NULL}} on if the value does not exist.  
 */
const char *mu_get_str(struct musl *m, const char *name);

/*@ int ##mu_has_var(struct musl *m, const char *name)
 *# Returns 1 if the variable {{name}} is defined in the 
 *# interpreter, 0 otherwise.
 */
int mu_has_var(struct musl *m, const char *name);

/*@ void ##mu_set_data(struct musl *m, void *data)
 *# Stores arbitrary user data in the musl structure
 *# that can later be retrieved with {{~~mu_get_data()}}
 */
void mu_set_data(struct musl *m, void *data);

/*@ void *##mu_get_data(struct musl *m)
 *# Retrieves user data from the musl structure 
 *# that was previously stored with {{~~mu_set_data()}}
 */
void *mu_get_data(struct musl *m);

/*@ void ##mu_dump(struct musl *m, FILE *f)
 *# Dumps the contents of the interpreter's variables to the file {{f}}.\n
 *# This is useful for debugging purposes.
 */
void mu_dump(struct musl *m, FILE *f);

/*@ int ##mu_valid_id(const char *id)
 *# Returns 1 if {{id}} is a valid Musl identifier,
 *# otherwise it returns 0.
 */
int mu_valid_id(const char *id);

#if defined(__cplusplus) || defined(c_plusplus)
} /* extern "C" */
#endif
#endif /* MUSL_H */
