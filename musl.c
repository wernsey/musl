/*! Musl
 *#   {/Marginally Useful Scripting Language/}
 *# or
 *#   {/My UnStructured Language/}
 *#
 *# An interpreter for a silly unstructured programming language.
 *#
 *@ License
 *# Author: Werner Stoop.\n
 *# These sources are provided under the terms of the unlicense: 
 *[
 *# This is free and unencumbered software released into the public domain.
 *# 
 *# Anyone is free to copy, modify, publish, use, compile, sell, or
 *# distribute this software, either in source code form or as a compiled
 *# binary, for any purpose, commercial or non-commercial, and by any
 *# means.
 *# 
 *# In jurisdictions that recognize copyright laws, the author or authors
 *# of this software dedicate any and all copyright interest in the
 *# software to the public domain. We make this dedication for the benefit
 *# of the public at large and to the detriment of our heirs and
 *# successors. We intend this dedication to be an overt act of
 *# relinquishment in perpetuity of all present and future rights to this
 *# software under copyright law.
 *# 
 *# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *# IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 *# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 *# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *# OTHER DEALINGS IN THE SOFTWARE.
 *# 
 *# For more information, please refer to <http://unlicense.org/>
 *]
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <stdarg.h>
#include <time.h>
#include <assert.h>

#ifdef WITH_REGEX
#include <sys/types.h>
#include <regex.h>
#endif

#include "musl.h"

/* Compiling with MS Visual C++? */
#ifdef _MSC_VER
#	define snprintf _snprintf
#endif

/* Maximum size of tokens (identifiers and quoted strings) */
#define TOK_SIZE	80

/* Maximum number of parameters that can be passed to a function */
#define MAX_PARAMS 20

/* Maximum nested gosubs */
#define MAX_GOSUB 20

/* Max number of nested FOR loops */
#define MAX_FOR 5

/* Size of hash tables; must be prime */
#define HASH_SIZE 199

#define MAX_ERROR_TEXT 80

typedef struct var* hash_table[HASH_SIZE];

/*
 * Structures
 */

enum types {nil, num, str, oper, go};

struct musl {	
	const char *s, *last, *start;
	char token[TOK_SIZE];
	
	hash_table n_vars,		/* Normal variables */
		s_vars,			/* String variables */
		labels,			/* Labels */
		funcs;				/* Functions */

	int argc;
	struct mu_par *argv;

	int active;
	
	const char *gosub_stack[MAX_GOSUB];
	int gosub_sp;
	
	const char *for_stack[MAX_FOR];
	int for_sp;
	
	jmp_buf on_error;
	char error_msg[MAX_ERROR_TEXT]; 
	char error_text[MAX_ERROR_TEXT];
	
	void *user; /* Stores arbitrary user data */
};

/*
 * Operators and Keywords
 */

#define OPERATORS	"=<>~+-*/%()[],:"

#define T_END		0
#define T_IDENT_N	1  /* Identifiers for normal variables eg "foo" */
#define T_IDENT_S	2  /* Identifiers for string variables eg "foo$" */
#define T_NUMBER	3
#define T_QUOTE		4
#define T_LF		5  /* '\n' */

#define T_LET		256
#define T_IF		257
#define T_THEN		258
#define T_GOTO		259
#define T_AND		260
#define T_OR		261
#define T_NOT		262
#define T_GOSUB		263
#define T_RETURN	264
#define T_KEND		265
#define T_ON		266

#define T_FOR		267
#define T_TO		268
#define T_DO		269
#define T_STEP		270
#define T_NEXT		271

struct {
	char * name;
	int val;
} keywords[] = {{"let",T_LET},
				{"if",T_IF},
				{"then",T_THEN},
				{"end",T_KEND},
				{"on",T_ON},
				{"goto",T_GOTO},
				{"gosub",T_GOSUB},
				{"return",T_RETURN},
				{"and",T_AND},
				{"or",T_OR},
				{"not",T_NOT},
				
				{"for",T_FOR},
				{"to",T_TO},
				{"do",T_DO},
				{"step",T_STEP},
				{"next",T_NEXT},
				{NULL, 0}};

static int iskeyword(const char *s) {
	int i;		
	for(i=0;keywords[i].name != NULL;i++)
		if(!strcmp(keywords[i].name, s))
			return keywords[i].val;
	return 0;
}

static void *mu_alloc(struct musl *m, size_t len) {
	void *p = malloc(len);
	if(!p) mu_throw_error(m, "Out of memory");
	return p;
}

/* 
 * Lookup-tables handling
 */

struct var {
	char *name;
	union {
		int i;
		char *s;
		const char *c;
		mu_func fun;
	} v;
	struct var *next;
};

static void init_table(hash_table tbl) {
	int i;
	for(i = 0; i< HASH_SIZE; i++)
		tbl[i] = NULL;
}

static void free_element(struct var* v, void (*cfun)(struct var *)) {
	if(!v) return;
	free_element(v->next, cfun);
	if(cfun) cfun(v);	
	free(v->name);
	free(v);
}

static void clear_table(hash_table tbl, void (*cfun)(struct var *)) {
	int i;
	for(i = 0; i < HASH_SIZE; i++)
		free_element(tbl[i], cfun);
}

static unsigned int hash(const char *s) {
	unsigned int i = 0x5555;
	for(;s[0];s++)
		i = i << 3 ^ (s[0] | (s[0] << 8));
	return i % HASH_SIZE;
}

static struct var *find_var(hash_table tbl, const char *name) {
	struct var* v;
	unsigned int h = hash(name);
	if(tbl[h])
		for(v = tbl[h]; v; v = v->next)
			if(!strcmp(v->name, name))
				return v;
	return NULL;
}

static struct var *new_var(const char *name) {
	struct var *v = malloc(sizeof *v);
	if(!v) return NULL;
	if(!(v->name = strdup(name))) {
		free(v);
		return NULL;
	}
	v->next = NULL;
	return v;
}

static void put_var(hash_table tbl, struct var *val) {
	struct var* v;
	unsigned int h = hash(val->name);
	if(tbl[h]) {
		for(v = tbl[h]; v->next; v= v->next);
		v->next = val;
	} else
		tbl[h] = val;
}

/*
 * Error handling
 */

void mu_throw_error(struct musl *m, const char *msg, ...) {
	va_list arg;
	va_start (arg, msg);
  	vsnprintf (m->error_msg, MAX_ERROR_TEXT-1, msg, arg);
  	va_end (arg);
  
	longjmp(m->on_error, -1);
}

const char *mu_error_text(struct musl *m) {
	return m->error_text;
}

const char *mu_error_msg(struct musl *m) {
	return m->error_msg;
}

int mu_cur_line(struct musl *m) {
	const char *c;
	int line = 1;
	if(!m->start || !m->s || m->start > m->s)
		return 0;
	for(c = m->start; *c && c <= m->s; c++)
		if(*c == '\n')
			line++;
	return line;
}

/*
 * Lexical Analyser
 */

static struct musl *tok_reset(struct musl *m) {
	if(m->last)
		m->s = m->last;
	return m;
}

static int tokenize(struct musl *m) {
	char *t;	
	if(!m->s) return T_END;
	
	m->last = m->s;
	
whitespace:	
	while(isspace(m->s[0]))
		if((m->s++)[0] == '\n') 
			return T_LF;
	
	if(m->s[0] == '#') {
		while(m->s[0] != '\n')
			if((m->s++)[0] == '\0')
				return T_END;
		return T_LF;
	}	
	
	if(m->s[0] == '\\') {
		do {
			m->s++;
		} while(m->s[0] && m->s[0] != '\n' && isspace(m->s[0]));
		if(m->s[0] != '\n') {
			mu_throw_error(m, "Bad '\\' at end of line");
		}
		m->s++;
		goto whitespace;
	}
	
	m->last = m->s;

	if(!m->s[0])
		return T_END;
	else if(m->s[0] == '"' || m->s[0] == '\'') {
		char term = m->s[0];
		for(m->s++, t=m->token; m->s[0] != term;) {
			if(!m->s[0])
				mu_throw_error(m, "Unterminated string");
			else if(t - m->token > TOK_SIZE - 2)
				mu_throw_error(m, "Token too long");			
			else if(m->s[0] == '\\') {
				switch(m->s[1])
				{
					case '\0': mu_throw_error(m, "Unterminated string"); break;
					case 'n' : *t++ = '\n'; break;
					case 'r' : *t++ = '\r'; break;
					case 't' : *t++ = '\t'; break;
					default : *t++ = m->s[1]; break;
				}
				m->s+=2;
			} else
				*t++ = *m->s++;
		}
		m->s++;
		t[0] = '\0';
		return T_QUOTE;		
	} else if(tolower(m->s[0]) == 'r' && (m->s[1] == '"' || m->s[1] == '\'')) {
		/* Python inspired "raw" string */
		char term = m->s[1];
		for(m->s+=2, t=m->token; m->s[0] != term;) {
			if(!m->s[0])
				mu_throw_error(m, "Unterminated string");
			else if(t - m->token > TOK_SIZE - 2)
				mu_throw_error(m, "Token too long");			
			*t++ = *m->s++;
		}
		m->s++;
		t[0] = '\0';
		return T_QUOTE;		
	} else if(isalpha(m->s[0]) || m->s[0] == '_') {
		int k, v = T_IDENT_N;
		for(t = m->token; isalnum(m->s[0]) || m->s[0] == '_';) {
			if(t-m->token > TOK_SIZE - 3)
				mu_throw_error(m, "Token too long");
			*t++ = tolower(*m->s++);
		}
		if(m->s[0] == '$') {
			v = T_IDENT_S;
			*t++ = *m->s++;
		}
		t[0] = '\0';
		return (k = iskeyword(m->token))?k:v;
	} else if(isdigit(m->s[0])) {
		for(t=m->token; isdigit(m->s[0]);*t++ = *m->s++)
			if(t-m->token > TOK_SIZE - 2)
				mu_throw_error(m, "Token too long");
		t[0] = '\0';
		return T_NUMBER;
	} else if(strchr(OPERATORS,m->s[0]))
		return *m->s++;
	mu_throw_error(m, "Unknown token '%c'", m->s[0]);
	return 0;
}

char *mu_readfile(const char *fname) {
	FILE *f;
	long len,r;
	char *str;
	
	if(!(f = fopen(fname, "rb")))	
		return NULL;
	
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	rewind(f);
	
	if(!(str = malloc(len+2)))
		return NULL;	
	r = fread(str, 1, len, f);
	
	if(r != len) {
		free(str);
		return NULL;
	}
	
	fclose(f);	
	str[len] = '\0';
	return str;
}

/*
 *@ Syntax
 *# The following describes the syntax of the interpreter.
 *# Consult the examples if it is unclear. 
 *[
 */

static const char *stmt(struct musl *m);
static struct mu_par fparams(const char *name, struct musl *m);
static int expr(struct musl *m);
static int and_expr(struct musl *m);
static int not_expr(struct musl *m);
static int comp_expr(struct musl *m);
static int add_expr(struct musl *m);
static int mul_expr(struct musl *m);
static int uexpr(struct musl *m);
static int atom(struct musl *m);
static char *str_expr(struct musl *m);
		
static int scan_labels(struct musl *m){
	const char *store = m->s;
	int t = T_END, ft = 1, c, ln = -1;

	while(ft || (t=tokenize(m)) != T_END) {
		if(ft || t == T_LF) {
			int t2 = tokenize(m);
			if(t2 == T_NUMBER) {
				if((c = atoi(m->token)) <= ln)
					mu_throw_error(m, "Label %d out of sequence", c);
				ln = c;
				if(find_var(m->labels, m->token)) {
					mu_throw_error(m, "Duplicate label '%s'", m->token);
				} else {
					struct var * lbl = new_var(m->token);
					if(!lbl) mu_throw_error(m, "Out of memory");
					lbl->v.c = m->s;
					put_var(m->labels, lbl);
				}
			} else if(t2 == T_IDENT_N) {
				if(tokenize(m) == ':') {
					struct var * lbl = new_var(m->token);
					if(!lbl) mu_throw_error(m, "Out of memory");
					lbl->v.c = m->s;
					put_var(m->labels, lbl);
				}
			} else if(t == T_LF)
				tok_reset(m);
		}
		ft = 0;
	}
	
	m->s = store;
	return 1;
}

/*# program ::= [line]*
 *# line ::= [label ':'] stmts <LF>
 *#            | NUMBER stmts <LF>
 */
static int program(struct musl *m) {
	int t, n = 0, ft = 1;
	const char *s;
	while((t=tokenize(m)) != T_END && t != T_KEND) {
		if(ft || t == T_LF) {		
			if(!ft) t = tokenize(m);						
			if(t == T_IDENT_N) {				
				const char *x = m->last;
				if(tokenize(m) != ':') {
					m->s = x;
				}
			} else if(t != T_NUMBER) {
				tok_reset(m);
			}
		} else if((s = stmt(tok_reset(m))) != NULL) {			
			m->s = s;
			m->last = NULL;
			
			/* To return to the C domain when we're in
			 * a call to mu_gosub(): */
			if(m->s == NULL) return n;
		}
		ft = 0;
	}
	return n;
}

/*# stmts ::= stmt [':' [<LF>+] stmts]
 *# stmt ::= [LET] ident ['[' (str_expr | expr) ']'] '=' expr
 *#        | [LET] ident$ ['[' (str_expr | expr) ']'] '=' str_expr
 *#        | ident '(' fparams ')'
 *#        | ident$ '(' fparams ')'
 *#        | GOTO label
 *#        | GOSUB label
 *#        | ON expr GOTO label [',' label]*
 *#        | ON expr GOSUB label [',' label]*
 *#        | RETURN
 *#        | IF expr THEN [<LF>+] stmts
 *#        | FOR ident = expr TO expr [STEP expr] DO [<LF>+] stmts [<LF>+] NEXT
 *#        | END
 */
static const char *stmt(struct musl *m) {  
	int t, u, has_let=0, q;
	char name[TOK_SIZE], buf[TOK_SIZE], *s;
	struct var *v;
	
	if((t = tokenize(m)) == T_IDENT_N || t == T_IDENT_S || t == T_LET) {
		if(t == T_LET && (has_let = 1) && (t = tokenize(m)) != T_IDENT_N && t != T_IDENT_S)
			mu_throw_error(m, "Identifier expected");
		
		strcpy(buf, m->token);
		if(tokenize(m) == '[') {
			has_let = 1;
			u = tokenize(m);
			tok_reset(m);
			if(u == T_QUOTE || u == T_IDENT_S) {
				s = str_expr(m);
				snprintf(name, TOK_SIZE, "%s[%s]", buf, s);
				free(s);
			} else
				snprintf(name, TOK_SIZE, "%s[%d]", buf, expr(m));
			if(tokenize(m) != ']')
				mu_throw_error(m, "Missing ']'");
		} else {
			strcpy(name, buf);
			tok_reset(m);
		}

		if((u = tokenize(m)) == '=') {
			if(t == T_IDENT_N) {
				int n = add_expr(m);
				if(m->active)
					if(!mu_set_num(m, name, n))
						mu_throw_error(m, "Out of memory");
			} else {
				s = str_expr(m);
				if(m->active)
					if(!mu_set_str(m, name, s))
						mu_throw_error(m, "Out of memory");
				free(s);
			}
		} else if(!has_let && u == '(') {
			struct mu_par rv = fparams(name, m);
			if(rv.type == mu_str)			
				free(rv.v.s);
		}
	} else if(t == T_IF) {
		int save = m->active;
		const char *result;
		u = expr(m);
		if(m->active) m->active = u;
		if(tokenize(m) != T_THEN)
			mu_throw_error(m, "THEN expected");
		
		while(tokenize(m) == T_LF); /* Allow newlines after THEN */
		tok_reset(m);
		
		result = stmt(m);
		m->active = save;
		if(result) 
			return result;
	} else if(t == T_GOTO || t == T_GOSUB) {
		
		if((u=tokenize(m)) != T_IDENT_N && u != T_NUMBER)
			mu_throw_error(m, "Label expected");

		if(m->active && t == T_GOSUB) {
			if(m->gosub_sp >= MAX_GOSUB - 1)
				mu_throw_error(m, "GOSUB stack overflow");
			m->gosub_stack[m->gosub_sp++] = m->s;
		}
		
		if(!(v = find_var(m->labels, m->token))) 
			mu_throw_error(m, "GOTO/GOSUB to undefined label '%s'", m->token);
		if(m->active)
			return v->v.c;
	} else if(t == T_RETURN) {
		if(m->gosub_sp <= 0)
			mu_throw_error(m, "GOSUB stack underflow");
		if(m->active) {
			m->s = m->gosub_stack[--m->gosub_sp];
			m->last = NULL;
			
			/* special case when for when we're in a mu_gosub() */
			if(m->s == NULL)
				return NULL;
		}
	} else if(t == T_ON) {
		int j = 0, x = expr(m);
		if((u = tokenize(m)) != T_GOTO && u != T_GOSUB)
			mu_throw_error(m, "GOTO or GOSUB expected");
		
		do {
			if((q=tokenize(m)) != T_IDENT_N && q != T_NUMBER)
				mu_throw_error(m, "Label expected");
			if(m->active && j++ == x) {
				if(!(v = find_var(m->labels, m->token))) 
					mu_throw_error(m, "ON .. GOTO/GOSUB to undefined label '%s'", m->token);		
				if(u == T_GOSUB) {
					if(m->gosub_sp >= MAX_GOSUB - 1)
						mu_throw_error(m, "GOSUB stack overflow");
					while(tokenize(m) == ',')
						if((q=tokenize(m)) != T_IDENT_N && q != T_NUMBER)
							mu_throw_error(m, "Label expected");
					tok_reset(m);
					m->gosub_stack[m->gosub_sp++] = m->s;
				}
				return v->v.c;
			}
		} while(tokenize(m) == ',');
		tok_reset(m);
	} else if(t == T_FOR) {
		int start;
		
		if(m->for_sp >= MAX_FOR)
			mu_throw_error(m, "FOR stack overflow");
		m->for_stack[m->for_sp++] = m->s;
		
		if(tokenize(m) != T_IDENT_N)
			mu_throw_error(m, "Identifier expected after FOR");
		strcpy(buf, m->token);
		if(tokenize(m) != '=')
			mu_throw_error(m, "'=' expected");
		start = expr(m);
		if(tokenize(m) != T_TO)
			mu_throw_error(m, "TO expected");
		expr(m);
		
		if(tokenize(m) == T_STEP)
			expr(m);
		else
			tok_reset(m);
		
		if(tokenize(m) != T_DO)
			mu_throw_error(m, "DO expected");
		
		if(!m->active) {
			m->for_sp--;
			if(tokenize(m) != T_LF)
				mu_throw_error(m, "<LF> expected");
			
			while((t=tokenize(m)) != T_NEXT) {
				const char *x = m->last;	
				if(t == T_NUMBER || t == T_LF) 
					continue;
				else if(t == T_IDENT_N) {				
					if(tokenize(m) == ':') {
						continue;
					} else {
						m->s = x;
					}
				} else
					tok_reset(m);
				stmt(m);
			}
		} else
			mu_set_num(m, buf, start);
	} else if(t == T_NEXT) {
		if(m->active) {
			int start, stop, step, idx;
			const char *save = m->s;					
			if(m->for_sp < 1)
				mu_throw_error(m, "FOR stack underflow");
			m->s = m->for_stack[m->for_sp - 1];
				
			if(tokenize(m) != T_IDENT_N)
				mu_throw_error(m, "Identifier expected after FOR");
			strcpy(buf, m->token);
			if(tokenize(m) != '=')
				mu_throw_error(m, "'=' expected");
			start = expr(m);
			if(tokenize(m) != T_TO)
				mu_throw_error(m, "TO expected");
			stop = expr(m);
			
			if(tokenize(m) == T_STEP) {
				step = expr(m);
			} else {
				tok_reset(m);
				if(start < stop) 
					step = 1;
				else
					step = -1;
			}
			
			if(tokenize(m) != T_DO)
				mu_throw_error(m, "DO expected");
			
			idx = mu_get_num(m, buf);
			if(idx == stop) {
				m->s = save;
				m->for_sp--;
			} else {
				idx += step;
				mu_set_num(m, buf, idx);
			}
		}
		return NULL;
	} else if(t == T_KEND || t == T_END) {
		if(m->active)
			tok_reset(m);
		return NULL;
	} else
		mu_throw_error(m, "Statement expected: %d", t);
	
	if((t=tokenize(m)) == ':') {
		while(tokenize(m) == T_LF);
		tok_reset(m);
		return stmt(m);
	}
	
	if(t != T_LF && t != T_KEND && t != T_END)
		mu_throw_error(m, "':' or <LF> expected (%d)", t);
	
	tok_reset(m);	
	return NULL;
}

/*# expr ::= and_expr [OR and_expr]*
 */
static int expr(struct musl *m) {
	int n = and_expr(m);
	while(tokenize(m) == T_OR)
		n = n | and_expr(m); /* Dont try || */
	tok_reset(m);
	return n;
}

/*# and_expr ::= not_expr [AND not_expr]*
 */
static int and_expr(struct musl *m) {	
	int n = not_expr(m);
	while(tokenize(m) == T_AND)
		n = n & not_expr(m); /* Dont try && */
	tok_reset(m);
	return n;
}

/*# not_expr ::= [NOT] comp_expr
 */
static int not_expr(struct musl *m) {
	if(tokenize(m) == T_NOT)
		return !comp_expr(m);	
	tok_reset(m);	
	return comp_expr(m);
}

/*# comp_expr ::= add_expr [('='|'<'|'>'|'~') add_expr]
 *#             | str_expr ('='|'<'|'>'|'~') str_expr
 */
static int comp_expr(struct musl *m) {
	int t, n, r;
	char *s1, *s2;
		
	t = tokenize(m);
	tok_reset(m);
	if(t == T_QUOTE || t == T_IDENT_S) {
		s1 = str_expr(m);
		if((t=tokenize(m)) != '=' && t != '<' && t != '>' && t != '~')
			mu_throw_error(m, "Comparison operator expected");	
		
		s2 = str_expr(m);
		r = strcmp(s1,s2);
		n = (t == '=' && !r) || (t == '<' && r < 0) || (t == '>' && r > 0) || (t == '~' && r);
		
		free(s1);
		free(s2);
		
		return n;
	}				
				
	n = add_expr(m);
	
	if((t=tokenize(m)) == '=')		
		n = n == add_expr(m);
	else if(t == '<')
		n = n < add_expr(m);
	else if(t == '>')
		n = n > add_expr(m);
	else if(t == '~')
		n = n != add_expr(m);
	else
		tok_reset(m);
	return n;
}

/*# fparams ::= [param ',' param ',' ...]
 *# param ::= str_expr | expr
 */ 
static struct mu_par fparams(const char *name, struct musl *m) {
	int t, i = 0;
	struct mu_par params[MAX_PARAMS], rv = {mu_int, {0}};
	struct var *v;
			
	while((t = tokenize(m)) != ')') {
		if(i >= MAX_PARAMS)
			mu_throw_error(m, "Too many parameters to function %s", name);

		if(t == T_QUOTE || t == T_IDENT_S) {
			params[i].type = mu_str;
			params[i].v.s = str_expr(tok_reset(m));
		} else {	
			params[i].type = mu_int;
			params[i].v.i = expr(tok_reset(m));			
		}
		i++;
		if((t = tokenize(m)) == ')')
			break;
		else if(t != ',')
			mu_throw_error(m, "Expected ')'");
	}				

	v = find_var(m->funcs, name);
	if(!v || !v->v.fun) 
		mu_throw_error(m, "Call to undefined function %s()", name);	

	m->argc = i;
	m->argv = params;

	if(m->active)
		rv = v->v.fun(m, m->argc, m->argv);
	for(i = 0; i < m->argc; i++)
		if(params[i].type == mu_str)
			free(params[i].v.s);

	return rv;
}

/*# str_expr ::= ("string"|ident$ (['[' (str_expr | expr) ']'] | [fparams])) 
 *#            ['+' ("string"|ident$ (['[' (str_expr | expr) ']'] | [fparams]))]*
 */
static char *str_expr(struct musl *m) {
	int t, u;
	char *s1 = NULL, *s2 = NULL, *s3;
	struct var *v;
	do {
		if((t = tokenize(m)) == T_QUOTE)
			s2 = strdup(m->token);
		else if(t == T_IDENT_S) {
			char name[TOK_SIZE], buf[TOK_SIZE];
			strcpy(buf, m->token);

			if((u = tokenize(m)) == '(')
				s2 = fparams(buf, m).v.s;
			else { 
				if(u == '[') {
					if((u=tokenize(m)) == T_QUOTE || u == T_IDENT_S) {
						tok_reset(m);
						s3 = str_expr(m);
						snprintf(name, TOK_SIZE, "%s[%s]", buf, s3);
						free(s3);
					} else {
						tok_reset(m);
						snprintf(name, TOK_SIZE, "%s[%d]", buf, expr(m));
					}
					if(tokenize(m) != ']')
						mu_throw_error(m, "Missing ']'");			
				} else {
					tok_reset(m);
					strcpy(name, buf);
				}
				
				v = find_var(m->s_vars, name);
				if(!v)
					mu_throw_error(m, "Read from undefined variable '%s'", name);
				s2 = strdup(v->v.s);
			}
		} else
			mu_throw_error(m, "String expression expected");

		if(!s2)
			mu_throw_error(m, "Out of memory");

		if(!s1)
			s1 = s2;
		else {
			s3 = malloc(strlen(s1) + strlen(s2) + 1);
			if(!s3)
				mu_throw_error(m, "Out of memory");
			sprintf(s3, "%s%s", s1, s2);
			free(s1);
			free(s2);
			s1 = s3;
		}
	} while(tokenize(m) == '+');
	tok_reset(m);
	return s1;	
}

/*# add_expr ::= mul_expr [('+'|'-') mul_expr]*
 */
static int add_expr(struct musl *m) {
	int t;
	int n = mul_expr(m);
	while((t = tokenize(m)) == '+' || t == '-')
		if(t == '+')
			n += mul_expr(m);
		else
			n -= mul_expr(m);
	tok_reset(m);
	return n;
}

/*# mul_expr ::= uexpr [('*'|'/'|'%') uexpr]*
 */
static int mul_expr(struct musl *m) {
	int t, n = uexpr(m), r;
	while((t = tokenize(m)) == '*' || t == '/' || t  == '%')
		if(t == '*')
			n *= uexpr(m);
		else if(t == '/') {
			if(!(r = uexpr(m)))
				mu_throw_error(m, "Divide by zero");
			n /= r;
		} else {
			if(!(r = uexpr(m)))
				mu_throw_error(m, "Divide by zero");
			n %= r;
		}
	tok_reset(m);
	return n;
}

/*# uexpr ::= ['-'|'+'] atom
 */
static int uexpr(struct musl *m) {
	int t;
	if((t = tokenize(m)) == '-')
		return -atom(m);
	if(t != '+') /* Throw away a unary + */
		tok_reset(m);
	return atom(m);
}

/*# atom ::= '(' expr ')'
 *#        |  ident 
 *#        |  ident '[' (str_expr | expr) ']' 
 *#        |  ident '(' [fparams] ')'
 *#        |  number
 *]
 */
static int atom(struct musl *m) {	
	int t, u;
	struct var *v;
	if((t = tokenize(m)) == '(') {
		int n = expr(m);
		if(tokenize(m) != ')')
			mu_throw_error(m, "Missing ')'");
		return n;
	} else if(t == T_IDENT_N) {
		char name[TOK_SIZE], buf[TOK_SIZE];
		strcpy(buf, m->token);
		
		if((u=tokenize(m)) == '(')
			return fparams(buf, m).v.i;
		else if(u == '[') {
			if((u=tokenize(m)) == T_QUOTE || u == T_IDENT_S) {
				char *s;
				tok_reset(m);
				s = str_expr(m);
				snprintf(name, TOK_SIZE, "%s[%s]", buf, s);
				free(s);
			} else {
				tok_reset(m);
				snprintf(name, TOK_SIZE, "%s[%d]", buf, expr(m));
			}
			if(tokenize(m) != ']')
				mu_throw_error(m, "Missing ']'");			
		} else {
			strcpy(name, buf);
			tok_reset(m);
		}

		v = find_var(m->n_vars, name);
		if(!v) {
			if(!m->active) return 0;
			mu_throw_error(m, "Read from undefined variable '%s'", name);
		}
		return v->v.i;
	} else if(t == T_NUMBER)
		return atoi(m->token);
	else if(t == T_IDENT_S)
		mu_throw_error(m, "Invalid string expression");
	
	mu_throw_error(m, "Value expected");
	return 0; /* Satisfy the compiler */
}

static int add_stdfuns(struct musl *m);

struct musl *mu_create() {
	struct musl *m;
	m = malloc(sizeof *m);
	if(!m) return NULL;
	init_table(m->n_vars);
	init_table(m->s_vars);
	init_table(m->labels);
	init_table(m->funcs);
	m->gosub_sp = 0;
	m->for_sp = 0;
	m->user = NULL;
	m->active = 1;
	m->start = NULL;
	m->s = NULL;
	strcpy(m->error_msg, "");
	strcpy(m->error_text, "");
	if(!add_stdfuns(m)) {
		free(m);
		return NULL;
	}
	return m;
}

int mu_run(struct musl *m, const char *s) {
	m->s = s;
	m->start = s;
	m->last = NULL;
	
	if(setjmp(m->on_error) != 0) {
		int i;
		tok_reset(m);
		for(i = 0; i < MAX_ERROR_TEXT - 1 && m->s[i] != '\0' && m->s[i] != '\n'; i++)
			m->error_text[i] = m->s[i];
		m->error_text[i] = '\0';
		return 0;
	}

	scan_labels(m);	
	program(m);
	
	/* Delete the labels, case another script is run */
	clear_table(m->labels, NULL);
	init_table(m->labels);
	return 1; 
}

int mu_gosub(struct musl *m, const char *label) {
	const char *save;
	volatile struct var *v;
	volatile jmp_buf save_jmp;
	volatile int rv = 0, save_sp;
		
	/* Find the label we're supposed to go to */
	if(!(v = find_var(m->labels, label))) {
		snprintf (m->error_msg, MAX_ERROR_TEXT-1, "GOSUB to undefined label");
		return 0;
	}
		
	/* Setup the GOSUB stack */	
	if(m->gosub_sp >= MAX_GOSUB - 1) {
		snprintf (m->error_msg, MAX_ERROR_TEXT-1, "GOSUB stack overflow");
		return 0;
	}
		
	/* Set the location in the program */
	save = m->s; /* Save current location */
	m->s = v->v.c; /* Set the new location */
	m->last = NULL;
	save_sp = m->gosub_sp;
	
	/* Push null onto the stack; Special case to show that
	 * the script needs to return to the C domain
	 */
	m->gosub_stack[m->gosub_sp++] = NULL;
	
	/* Save the old error handler and set the new one */
	memcpy(&save_jmp, &m->on_error, sizeof save_jmp);
	
	if(setjmp(m->on_error) != 0) {
		int i;
		tok_reset(m);
		for(i = 0; i < MAX_ERROR_TEXT - 1 && m->s[i] != '\0' && m->s[i] != '\n'; i++)
			m->error_text[i] = m->s[i];
		m->error_text[i] = '\0';
		
		/* Need to restore the old error handler */
		goto restore;
	}
	
	/* Run the subroutine */
	program(m);
	rv  = 1;
	
restore:
	/* Restore everything */
	memcpy(&m->on_error, &save_jmp, sizeof save_jmp);
	m->s = save;
	m->last = NULL;
	m->gosub_sp = save_sp;
	
	/* Return success */
	return rv;
}

void mu_halt(struct musl *m) {
	m->s = NULL;	
	m->last = NULL;
}

/*
 * Cleanup
 */
static void clear_svar(struct var *v) {
	free(v->v.s);
}

void mu_cleanup(struct musl *m) {
	clear_table(m->s_vars, clear_svar);
	clear_table(m->n_vars, NULL);
	clear_table(m->funcs, NULL);
	free(m);
}

/*
 * Accessor functions
 */
int mu_set_num(struct musl *m, const char *name, int num) {
	struct var *v = find_var(m->n_vars, name);
	if(!v) {
		if(!(v = new_var(name))) return 0;
		put_var(m->n_vars, v);
	}
		
	v->v.i = num;
	return 1;						
}

int mu_get_num(struct musl *m, const char *name) {
	struct var *v = find_var(m->n_vars, name);
	return v ? v->v.i : 0;						
}

int mu_set_str(struct musl *m, const char *name, const char *val) {
	struct var *v = find_var(m->s_vars, name);
	if(!v) {
		if(!(v = new_var(name))) return 0;
		put_var(m->s_vars, v);
	} else
		free(v->v.s);
		
	v->v.s = strdup(val);
	return v->v.s != NULL;						
}

const char *mu_get_str(struct musl *m, const char *name) {
	struct var *v = find_var(m->s_vars, name);		
	return v ? v->v.s : NULL;						
}

void mu_set_data(struct musl *m, void *data) {
	m->user = data;
}

void *mu_get_data(struct musl *m) {
	return m->user;
}

/*
 * External functions
 */
int mu_add_func(struct musl *m, const char *name, mu_func fun) {
	struct var *v = find_var(m->funcs, name);
	if(!v) {					
		if(!(v = new_var(name))) return 0;	
		put_var(m->funcs, v);
	}
	v->v.fun = fun;
	return 1;
}

int mu_par_num(struct musl *m, int n) {
	if(n >= m->argc)
		mu_throw_error(m, "Too few parameters to function");
	if(m->argv[n].type != mu_int)
		mu_throw_error(m, "Parameter %d must be numeric", n);
	return m->argv[n].v.i;
}

const char *mu_par_str(struct musl *m, int n) {
	if(n >= m->argc)
		mu_throw_error(m, "Too few parameters to function");
	if(m->argv[n].type != mu_str)
		mu_throw_error(m, "Parameter %d must be a string", n);
	return m->argv[n].v.s;
}

/*
 * Standard functions
 */

/*@ Built-In Functions
 *# The Following built-in functions are available to all scripts.
 *#
 * When adding your own functions, keep the following in mind:
 * - Functions that return strings should allocate those strings on the heap,
 *   because musl will call free() on them at a later stage.
 * - Functions that return strings' names must end with $. The parser won't
 *   allow them otherwise.
 */

/*@ VAL(x$) 
 *# Converts the string {{x$}} to a number. */
static struct mu_par m_val(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv = {mu_int, {atoi(mu_par_str(m, 0))}};
	return rv;
}

/*@ STR$(x) 
 *# Converts the number {{x}} to a string. */
static struct mu_par m_str(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv;
	rv.type = mu_str;
	rv.v.s = mu_alloc(m, TOK_SIZE);
	snprintf(rv.v.s, TOK_SIZE, "%d", mu_par_num(m, 0));
	return rv;
}

/*@ LEN(x$) 
 *# Returns the length of string {{x$}} */
static struct mu_par m_len(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv = {mu_int, {strlen(mu_par_str(m, 0))}};
	return rv;
}

/*@ LEFT$(s$, n) 
 *# Returns the {{n}} leftmost characters in {{s$}} 
 */
static struct mu_par m_left(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv;
	const char *s = mu_par_str(m, 0);
	int len = mu_par_num(m, 1);
	
	if(len < 0)
		mu_throw_error(m, "Invalid parameters to LEFT$()");
	
	if(len > (int)strlen(s))
		len = strlen(s);
	
	rv.type = mu_str;	
	rv.v.s = mu_alloc(m, len + 1);
	strncpy(rv.v.s, s, len);
	rv.v.s[len] = '\0';
	return rv;
}

/*@ RIGHT$(s$, n)
 *# Returns the {{n}} rightmost characters in {{s$}} */
static struct mu_par m_right(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv;
	const char *s = mu_par_str(m, 0);
	int len = mu_par_num(m, 1);
	
	if(len < 0)
		mu_throw_error(m, "Invalid parameters to RIGHT$()");
	
	if(len > (int)strlen(s))
		len = strlen(s);
	
	s = s + strlen(s) - len;
	
	rv.type = mu_str;	
	rv.v.s = strdup(s);
	if(!rv.v.s)
		mu_throw_error(m, "Out of memory");
	
	return rv;
}

/*@ MID$(s$, n, m)
 *# Returns the all the characters in {{s$}} between {{n}} and {{m}} \n
 *# The string is indexed from 1, that is
 *#     {{MID$("Hello World From Musl", 7, 11)}}
 *# will return {{"World"}}
 */
static struct mu_par m_mid(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv;
	const char *s = mu_par_str(m, 0);
	int p = mu_par_num(m, 1) - 1;
	int q = mu_par_num(m, 2);
	int len = q - p;
	
	if(q < p || p < 0)
		mu_throw_error(m, "Invalid parameters to MID$()");
	
	if(len > (int)strlen(s + p))
		len = strlen(s + p);
	
	rv.type = mu_str;	
	rv.v.s = mu_alloc(m, len + 1);
	strncpy(rv.v.s, s + p, len);
	rv.v.s[len] = '\0';
	return rv;
}

/*@ UCASE$(x$) 
 *# Converts the string {{x$}} to uppercase. */
static struct mu_par m_ucase(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv;
	char *c;
	rv.type = mu_str;
	rv.v.s = strdup(mu_par_str(m, 0));
	if(!rv.v.s) mu_throw_error(m, "Out of memory");
	for(c=rv.v.s;*c;c++)
		*c = toupper(*c);
	return rv;
}

/*@ LCASE$(x$) 
 *# Converts the string {{x$}} to lowercase. */
static struct mu_par m_lcase(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv;
	char *c;
	rv.type = mu_str;
	rv.v.s = strdup(mu_par_str(m, 0));
	if(!rv.v.s) mu_throw_error(m, "Out of memory");
	for(c=rv.v.s;*c;c++)
		*c = tolower(*c);
	return rv;
}

/*@ TRIM$(x$) 
 *# Removes leading and trailing whitespace from string {{x$}}. */
static struct mu_par m_trim(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv;
	char *c;
	const char *s = mu_par_str(m, 0);
	
	while(isspace(s[0])) s++;
		
	rv.type = mu_str;
	rv.v.s = strdup(s);
	if(!rv.v.s) mu_throw_error(m, "Out of memory");
	
	for(c=rv.v.s + strlen(rv.v.s) - 1;c > rv.v.s; c--)
		if(!isspace(*c)) {
			c[1] = '\0';
			break;
		}
	return rv;
}

/*@ INSTR(str$, find$) 
 *# Searches for {{find$}} in {{str$}} and returns the index.\n
 *# It returns 0 if find$ was not found.
 */
static struct mu_par m_instr(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv = {mu_int, {0}};	
	const char *str = mu_par_str(m, 0);
	char *x = strstr(str, mu_par_str(m, 1));	
	if(x) 
		rv.v.i = x - str + 1;	
	return rv;
}

/*@ DATA(list$, item1, item2, item3, ...) 
 *# Populates an array named 'list'
 *#      A call 
 *[
 *# DATA("array$", "Alice", "Bob", "Carol") 
 *]
 *# is equivalent to the statements:
 *[
 *# LET array$[1] = "Alice"
 *# LET array$[2] = "Bob"
 *# LET array$[3] = "Carol"
 *]
 *# Note that the normal rules for identifiers count: If the array is
 *# to contain strings, the identifier should end with {{'$'}}.\n
 *# It returns the number of items inserted into the array.
 */
static struct mu_par m_data(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv = {mu_int, {0}};	
	int i, sa = 0;
	char *c, name[TOK_SIZE];
	const char *aname;
	
	if(argc < 1 || argv[0].type != mu_str)
		mu_throw_error(m, "DATA() must take at least 1 string parameter");	
	
	if(!isalpha(argv[0].v.s[0])) 
		mu_throw_error(m, "DATA()'s first parameter must be a valid identifier");
	for(c = argv[0].v.s; c[0]; c++)
		if(!isalnum(c[0])) {
			if(c[0] == '$' && c[1] == '\0')
				sa = 1;
			else
				mu_throw_error(m, "DATA()'s first parameter must be a valid identifier");
		}		
	
	aname = mu_par_str(m, 0);
	if(sa) {		
		for(i = 1; i < argc; i++) {
			snprintf(name, TOK_SIZE, "%s[%d]", aname, i);
			if(!mu_set_str(m, name, mu_par_str(m, i)))
				mu_throw_error(m, "Out of memory");
		}		
	} else {		
		for(i = 1; i < argc; i++) {
			snprintf(name, TOK_SIZE, "%s[%d]", aname, i);
			if(!mu_set_num(m, name, mu_par_num(m, i)))
				mu_throw_error(m, "Out of memory");
		}
	}
	rv.v.i = argc - 1;
	return rv;
}

/* Adds the standard functions to the interpreter */
static int add_stdfuns(struct musl *m) {
	return !(!mu_add_func(m, "val", m_val) ||
		!mu_add_func(m, "str$", m_str) ||
		!mu_add_func(m, "len", m_len) ||
		!mu_add_func(m, "left$", m_left) ||
		!mu_add_func(m, "right$", m_right)||
		!mu_add_func(m, "mid$", m_mid)||
		!mu_add_func(m, "ucase$", m_ucase)||
		!mu_add_func(m, "lcase$", m_lcase)||
		!mu_add_func(m, "trim$", m_trim)||
		!mu_add_func(m, "instr", m_instr)||
		!mu_add_func(m, "data", m_data)
		);
}
