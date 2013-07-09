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
	
	hash_table vars,	/* variables */
		labels,			/* Labels */
		funcs;			/* Functions */

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

#define OPERATORS	"=<>~+-*/%&()[],:"

#define T_END		0
#define T_IDENT		1  /* Identifiers for normal variables eg "foo" */
#define T_NUMBER	2
#define T_STRING		3
#define T_LF		4  /* '\n' */

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
	if(!p) mu_throw(m, "Out of memory");
	return p;
}

/* 
 * Lookup-tables handling
 */

struct var {
	char *name;
	enum mu_ptype type; /* for struct musl->vars only */
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

void mu_throw(struct musl *m, const char *msg, ...) {
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
			mu_throw(m, "Bad '\\' at end of line");
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
				mu_throw(m, "Unterminated string");
			else if(t - m->token > TOK_SIZE - 2)
				mu_throw(m, "Token too long");			
			else if(m->s[0] == '\\') {
				switch(m->s[1])
				{
					case '\0': mu_throw(m, "Unterminated string"); break;
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
		return T_STRING;		
	} else if(tolower(m->s[0]) == 'r' && (m->s[1] == '"' || m->s[1] == '\'')) {
		/* Python inspired "raw" string */
		char term = m->s[1];
		for(m->s+=2, t=m->token; m->s[0] != term;) {
			if(!m->s[0])
				mu_throw(m, "Unterminated string");
			else if(t - m->token > TOK_SIZE - 2)
				mu_throw(m, "Token too long");			
			*t++ = *m->s++;
		}
		m->s++;
		t[0] = '\0';
		return T_STRING;		
	} else if(isalpha(m->s[0]) || m->s[0] == '_') {
		int k, v = T_IDENT;
		for(t = m->token; isalnum(m->s[0]) || m->s[0] == '_' || m->s[0] == '$';) {
			if(t - m->token > TOK_SIZE - 2)
				mu_throw(m, "Token too long");
			*t++ = tolower(*m->s++);
		}
		t[0] = '\0';
		return (k = iskeyword(m->token))?k:v;
	} else if(isdigit(m->s[0])) {
		for(t=m->token; isdigit(m->s[0]);*t++ = *m->s++)
			if(t-m->token > TOK_SIZE - 2)
				mu_throw(m, "Token too long");
		t[0] = '\0';
		return T_NUMBER;
	} else if(strchr(OPERATORS,m->s[0]))
		return *m->s++;
	mu_throw(m, "Unknown token '%c'", m->s[0]);
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
static struct mu_par expr(struct musl *m);
static struct mu_par and_expr(struct musl *m);
static struct mu_par not_expr(struct musl *m);
static struct mu_par comp_expr(struct musl *m);
static struct mu_par cat_expr(struct musl *m);
static struct mu_par add_expr(struct musl *m);
static struct mu_par mul_expr(struct musl *m);
static struct mu_par uexpr(struct musl *m);
static struct mu_par atom(struct musl *m);
		
static int scan_labels(struct musl *m){
	const char *store = m->s;
	int t = T_END, ft = 1, c, ln = -1;

	while(ft || (t=tokenize(m)) != T_END) {
		if(ft || t == T_LF) {
			int t2 = tokenize(m);
			if(t2 == T_NUMBER) {
				if((c = atoi(m->token)) <= ln)
					mu_throw(m, "Label %d out of sequence", c);
				ln = c;
				if(find_var(m->labels, m->token)) {
					mu_throw(m, "Duplicate label '%s'", m->token);
				} else {
					struct var * lbl = new_var(m->token);
					if(!lbl) mu_throw(m, "Out of memory");
					lbl->v.c = m->s;
					put_var(m->labels, lbl);
				}
			} else if(t2 == T_IDENT) {
				if(tokenize(m) == ':') {
					struct var * lbl = new_var(m->token);
					if(!lbl) mu_throw(m, "Out of memory");
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

/* Helpers for weak typing: */
static int par_as_int(struct mu_par *par) {
	if(par->type == mu_int) {
		return par->v.i;
	} else {
		int i = atoi(par->v.s);
		free(par->v.s);
		par->type = mu_int;
		par->v.i = i;
		return i;
	}
}

static char *par_as_str(struct mu_par *par) {
	if(par->type == mu_str) {
		return strdup(par->v.s);
	} else {
		char *buffer = malloc(20);
		if(!buffer) return NULL;
		sprintf(buffer, "%d", par->v.i);
		par->type = mu_str;
		par->v.s = buffer;
		return buffer;
	}
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
			if(t == T_IDENT) {				
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
 *# stmt ::= [LET] ident ['[' expr ']'] '=' expr
 *#        | ident '(' fparams ')'
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
	char name[TOK_SIZE], buf[TOK_SIZE];
	struct var *v;
	struct mu_par rhs;
	
	if((t = tokenize(m)) == T_IDENT || t == T_LET) {
		if(t == T_LET && (has_let = 1) && (t = tokenize(m)) != T_IDENT)
			mu_throw(m, "Identifier expected");
		
		strcpy(buf, m->token);
		if(tokenize(m) == '[') {
			has_let = 1;
			
			rhs = expr(m);
			par_as_str(&rhs);
			snprintf(name, TOK_SIZE, "%s[%s]", buf, rhs.v.s);
			free(rhs.v.s);
			
			if(tokenize(m) != ']')
				mu_throw(m, "Missing ']'");
		} else {
			strcpy(name, buf);
			tok_reset(m);
		}

		if((u = tokenize(m)) == '=') {			
			rhs = expr(m);
			if(rhs.type == mu_str) {
				if(m->active && !mu_set_str(m, name, rhs.v.s))
					mu_throw(m, "Out of memory");
				free(rhs.v.s);
			} else {
				if(m->active && !mu_set_num(m, name, rhs.v.i))
					mu_throw(m, "Out of memory");
			}			
		} else if(!has_let && u == '(') {
			rhs = fparams(name, m);
			if(rhs.type == mu_str)			
				free(rhs.v.s);
		}
	} else if(t == T_IF) {
		int save = m->active;
		const char *result;
		rhs = expr(m);
		
		if(m->active) 
			m->active = par_as_int(&rhs);
		
		if(tokenize(m) != T_THEN)
			mu_throw(m, "THEN expected");
		
		while(tokenize(m) == T_LF); /* Allow newlines after THEN */
		tok_reset(m);
				
		result = stmt(m);
		m->active = save;
		if(result) 
			return result;
		
	} else if(t == T_GOTO || t == T_GOSUB) {
		
		if((u=tokenize(m)) != T_IDENT && u != T_NUMBER)
			mu_throw(m, "Label expected");

		if(m->active && t == T_GOSUB) {
			if(m->gosub_sp >= MAX_GOSUB - 1)
				mu_throw(m, "GOSUB stack overflow");
			m->gosub_stack[m->gosub_sp++] = m->s;
		}
		
		if(!(v = find_var(m->labels, m->token))) 
			mu_throw(m, "GOTO/GOSUB to undefined label '%s'", m->token);
		if(m->active)
			return v->v.c;
	} else if(t == T_RETURN) {
		if(m->gosub_sp <= 0)
			mu_throw(m, "GOSUB stack underflow");
		if(m->active) {
			m->s = m->gosub_stack[--m->gosub_sp];
			m->last = NULL;
			
			/* special case when for when we're in a mu_gosub() */
			if(m->s == NULL)
				return NULL;
		}
	} else if(t == T_ON) {
		int j = 0;
		rhs = expr(m);
		par_as_int(&rhs);
		if((u = tokenize(m)) != T_GOTO && u != T_GOSUB)
			mu_throw(m, "GOTO or GOSUB expected");
		
		do {
			if((q=tokenize(m)) != T_IDENT && q != T_NUMBER)
				mu_throw(m, "Label expected");
			
			if(m->active && j++ == rhs.v.i) {
				if(!(v = find_var(m->labels, m->token))) 
					mu_throw(m, "ON .. GOTO/GOSUB to undefined label '%s'", m->token);		
				if(u == T_GOSUB) {
					if(m->gosub_sp >= MAX_GOSUB - 1)
						mu_throw(m, "GOSUB stack overflow");
					while(tokenize(m) == ',')
						if((q=tokenize(m)) != T_IDENT && q != T_NUMBER)
							mu_throw(m, "Label expected");
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
			mu_throw(m, "FOR stack overflow");
		m->for_stack[m->for_sp++] = m->s;
		
		if(tokenize(m) != T_IDENT)
			mu_throw(m, "Identifier expected after FOR");
		strcpy(buf, m->token);
		if(tokenize(m) != '=')
			mu_throw(m, "'=' expected");
				
		rhs = expr(m);
		start = par_as_int(&rhs);
		
		if(tokenize(m) != T_TO)
			mu_throw(m, "TO expected");
		expr(m);
		
		if(tokenize(m) == T_STEP)
			expr(m);
		else
			tok_reset(m);
		
		if(tokenize(m) != T_DO)
			mu_throw(m, "DO expected");
		
		if(!m->active) {
			m->for_sp--;
			if(tokenize(m) != T_LF)
				mu_throw(m, "<LF> expected");
			
			while((t=tokenize(m)) != T_NEXT) {
				const char *x = m->last;	
				if(t == T_NUMBER || t == T_LF) 
					continue;
				else if(t == T_IDENT) {				
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
				mu_throw(m, "FOR stack underflow");
			m->s = m->for_stack[m->for_sp - 1];
				
			if(tokenize(m) != T_IDENT)
				mu_throw(m, "Identifier expected after FOR");
			strcpy(buf, m->token);
			if(tokenize(m) != '=')
				mu_throw(m, "'=' expected");
			
			rhs = expr(m);
			start = par_as_int(&rhs);
			
			if(tokenize(m) != T_TO)
				mu_throw(m, "TO expected");
						
			rhs = expr(m);
			stop = par_as_int(&rhs);
			
			if(tokenize(m) == T_STEP) {
				rhs = expr(m);
				step = par_as_int(&rhs);
			} else {
				tok_reset(m);
				if(start < stop) 
					step = 1;
				else
					step = -1;
			}
			
			if(tokenize(m) != T_DO)
				mu_throw(m, "DO expected");
			
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
		mu_throw(m, "Statement expected (%d)", t);
	
	if((t=tokenize(m)) == ':') {
		while(tokenize(m) == T_LF);
		tok_reset(m);
		return stmt(m);
	}
	
	if(t != T_LF && t != T_KEND && t != T_END)
		mu_throw(m, "':' or <LF> expected (%d)", t);
	
	tok_reset(m);	
	return NULL;
}

/*# fparams ::= [expr ',' expr ',' ...]
 */ 
static struct mu_par fparams(const char *name, struct musl *m) {
	int t, i = 0;
	struct mu_par params[MAX_PARAMS], rv = {mu_int, {0}};
	struct var *v;
			
	while((t = tokenize(m)) != ')') {
		if(i >= MAX_PARAMS)
			mu_throw(m, "Too many parameters to function %s", name);

		params[i] = expr(tok_reset(m));			
		
		i++;
		if((t = tokenize(m)) == ')')
			break;
		else if(t != ',')
			mu_throw(m, "Expected ')'");
	}				

	v = find_var(m->funcs, name);
	if(!v || !v->v.fun) 
		mu_throw(m, "Call to undefined function %s()", name);	

	m->argc = i;
	m->argv = params;

	if(m->active)
		rv = v->v.fun(m, m->argc, m->argv);
	for(i = 0; i < m->argc; i++)
		if(params[i].type == mu_str)
			free(params[i].v.s);

	return rv;
}

/*# expr ::= and_expr [OR and_expr]*
 */
static struct mu_par expr(struct musl *m) {
	struct mu_par lhs = and_expr(m);
	
	if(tokenize(m) == T_OR) {
		int n = par_as_int(&lhs);
		do {		
			struct mu_par rhs = and_expr(m);
			n = n | par_as_int(&rhs);
		} while(tokenize(m) == T_OR);
		assert(lhs.type == mu_int);
		lhs.v.i = n;
	}
	tok_reset(m);
	return lhs;
}

/*# and_expr ::= not_expr [AND not_expr]*
 */
static struct mu_par and_expr(struct musl *m) {	
	struct mu_par lhs = not_expr(m);
	if(tokenize(m) == T_AND) {	
		int n = par_as_int(&lhs);
		do {
			struct mu_par rhs = not_expr(m);
			n = n & par_as_int(&rhs);
		} while(tokenize(m) == T_AND);
		assert(lhs.type == mu_int);
		lhs.v.i = n;
	}
	
	tok_reset(m);
	return lhs;
}

/*# not_expr ::= [NOT] comp_expr
 */
static struct mu_par not_expr(struct musl *m) {
	if(tokenize(m) == T_NOT) {
		struct mu_par lhs = comp_expr(m);
		par_as_int(&lhs);
		lhs.v.i = !lhs.v.i;
		return lhs;
	}		
	tok_reset(m);	
	return comp_expr(m);
}

/*# comp_expr ::= cat_expr [('='|'<'|'>'|'~') cat_expr]
 */
static struct mu_par comp_expr(struct musl *m) {
	int t, n, r;
	struct mu_par lhs = cat_expr(m);	
	if((t=tokenize(m)) == '=' || t == '<' || t == '>' || t == '~') {
		struct mu_par rhs = cat_expr(m);
		if(lhs.type == mu_str) {
			par_as_str(&rhs);
			r = strcmp(lhs.v.s, rhs.v.s);
			n = (t == '=' && !r) || (t == '<' && r < 0) || (t == '>' && r > 0) || (t == '~' && r);
			free(lhs.v.s);
			free(rhs.v.s);
			lhs.type = mu_int;
			lhs.v.i = n;
		} else {
			par_as_int(&rhs);
			if(t == '=')		
				n = lhs.v.i == rhs.v.i;
			else if(t == '<')
				n = lhs.v.i < rhs.v.i;
			else if(t == '>')
				n = lhs.v.i > rhs.v.i;
			else if(t == '~')
				n = lhs.v.i != rhs.v.i;
			lhs.v.i = n;
		}		
		
	} else
		tok_reset(m);
	
	return lhs;
}

/*# cat_expr ::= add_expr ['&' add_expr]*
 */
static struct mu_par cat_expr(struct musl *m) {
	int t;
	struct mu_par lhs = add_expr(m);
	if((t = tokenize(m)) == '&') {
		
		do {
			char *s = par_as_str(&lhs), *t;			
			struct mu_par rhs = add_expr(m);
			
			par_as_str(&rhs);
			
			t = malloc(strlen(s) + strlen(rhs.v.s) + 1);
			if(!t) 
				mu_throw(m, "Out of memory");
			
			strcpy(t,s);
			strcat(t,rhs.v.s);
			assert(strlen(t) == strlen(s) + strlen(rhs.v.s));
			
			free(lhs.v.s);
			lhs.v.s = t;
			
		} while((t = tokenize(m)) == '&');
		
		
		
	}
	tok_reset(m);
	return lhs;
}

/*# add_expr ::= mul_expr [('+'|'-') mul_expr]*
 */
static struct mu_par add_expr(struct musl *m) {
	int t;
	struct mu_par lhs = mul_expr(m);
	if((t = tokenize(m)) == '+' || t == '-') {
		int n = par_as_int(&lhs);
		do {
			struct mu_par rhs = mul_expr(m);
			if(t == '+')
				n += par_as_int(&rhs);
			else
				n -= par_as_int(&rhs);
		} while((t = tokenize(m)) == '+' || t == '-');
		assert(lhs.type == mu_int);
		lhs.v.i = n;
	}
	tok_reset(m);
	return lhs;
}

/*# mul_expr ::= uexpr [('*'|'/'|'%') uexpr]*
 */
static struct mu_par mul_expr(struct musl *m) {
	int t;
	struct mu_par lhs = uexpr(m);
	if((t = tokenize(m)) == '*' || t == '/' || t  == '%') {
		int n = par_as_int(&lhs);
		do {
			struct mu_par rhs = uexpr(m);
			int r = par_as_int(&rhs);
			if(t == '*')
				n *= r;
			else if(t == '/') {				
				if(!r)
					mu_throw(m, "Divide by zero");
				n /= r;
			} else {
				if(!r)
					mu_throw(m, "Divide by zero");
				n %= r;
			}
		} while((t = tokenize(m)) == '*' || t == '/' || t  == '%');		
		assert(lhs.type == mu_int);
		lhs.v.i = n;
	}
	tok_reset(m);
	return lhs;
}

/*# uexpr ::= ['-'|'+'] atom
 */
static struct mu_par uexpr(struct musl *m) {
	int t;
	if((t = tokenize(m)) == '-') {
		struct mu_par lhs = atom(m);
		par_as_int(&lhs);
		lhs.v.i = -lhs.v.i;
		return lhs;
	}
	if(t != '+') /* Throw away a unary + */
		tok_reset(m);
	return atom(m);
}

/*# atom ::= '(' expr ')'
 *#        |  ident 
 *#        |  ident '[' expr ']' 
 *#        |  ident '(' [fparams] ')'
 *#        |  number
 *#        |  string
 *]
 */
static struct mu_par atom(struct musl *m) {	
	int t, u;
	struct var *v;
	struct mu_par ret = {mu_int, {0}};
	
	if((t = tokenize(m)) == '(') {
		struct mu_par lhs = expr(m);
		if(tokenize(m) != ')')
			mu_throw(m, "Missing ')'");
		return lhs;
	} else if(t == T_IDENT) {
		
		char name[TOK_SIZE], buf[TOK_SIZE];
		strcpy(buf, m->token);
		
		if((u=tokenize(m)) == '(')
			return fparams(buf, m);
		else if(u == '[') {
			
			struct mu_par rhs = expr(m);
			par_as_str(&rhs);
			snprintf(name, TOK_SIZE, "%s[%s]", buf, rhs.v.s);
			free(rhs.v.s);
			
			if(tokenize(m) != ']')
				mu_throw(m, "Missing ']'");			
		} else {
			strcpy(name, buf);
			tok_reset(m);
		}

		v = find_var(m->vars, name);
		if(!v) {
			if(!m->active) return ret;
			mu_throw(m, "Read from undefined variable '%s'", name);
		}
		
		ret.type = v->type;
		if(v->type == mu_int) {
			ret.v.i = v->v.i;
		} else {
			ret.v.s = strdup(v->v.s);
		}
		
		return ret;
	} else if(t == T_NUMBER) {
		ret.type = mu_int;
		ret.v.i = atoi(m->token); 
		return ret;
	} else if(t == T_STRING) {
		ret.type = mu_str;
		ret.v.s = strdup(m->token);
		return ret;
	} 
	
	mu_throw(m, "Value expected");
	return ret; /* Satisfy the compiler */
}

static int add_stdfuns(struct musl *m);

struct musl *mu_create() {
	struct musl *m;
	m = malloc(sizeof *m);
	if(!m) return NULL;
	init_table(m->vars);
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
static void clear_var(struct var *v) {
	if(v->type == mu_str)
		free(v->v.s);
}

void mu_cleanup(struct musl *m) {
	clear_table(m->vars, clear_var);
	clear_table(m->funcs, NULL);
	clear_table(m->labels, NULL);
	free(m);
}

/*
 * Accessor functions
 */
int mu_set_num(struct musl *m, const char *name, int num) {
	struct var *v = find_var(m->vars, name);
		
	if(!v) {
		if(!(v = new_var(name))) return 0;
		put_var(m->vars, v);
	} else if(v->type == mu_str) {
			free(v->v.s);
	}
	v->type = mu_int;
	v->v.i = num;
	return 1;						
}

int mu_get_num(struct musl *m, const char *name) {
	struct var *v = find_var(m->vars, name);
	if(!v) 
		return 0;
	else if(v->type == mu_str) 
		return atoi(v->v.s);
	return v->v.i;						
}

int mu_set_str(struct musl *m, const char *name, const char *val) {
	struct var *v = find_var(m->vars, name);
	if(!v) {
		if(!(v = new_var(name))) return 0;
		put_var(m->vars, v);
	} else if(v->type == mu_str) {
		free(v->v.s);
	}
	v->type = mu_str;
	v->v.s = strdup(val);
	
	return v->v.s != NULL;						
}

const char *mu_get_str(struct musl *m, const char *name) {
	struct var *v = find_var(m->vars, name);	
	if(!v) 
		return NULL;
	
	if(v->type == mu_int) {
		char *buffer = malloc(20);
		if(!buffer) return NULL;
		sprintf(buffer, "%d", v->v.i);
		v->type = mu_str;
		v->v.s = buffer;
	}	
	
	return v->v.s;						
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
		mu_throw(m, "Too few parameters to function");
	if(m->argv[n].type != mu_int)
		mu_throw(m, "Parameter %d must be numeric", n);
	return m->argv[n].v.i;
}

const char *mu_par_str(struct musl *m, int n) {
	if(n >= m->argc)
		mu_throw(m, "Too few parameters to function");
	if(m->argv[n].type != mu_str)
		mu_throw(m, "Parameter %d must be a string", n);
	return m->argv[n].v.s;
}

/*
 * Standard functions
 */

/*@ Built-In Functions
 *# The Following built-in functions are available to all scripts.
 *#
 * When implementing your own functions, keep in mind that functions 
 * that return strings should allocate those strings on the heap,
 * because Musl will call free() on them at a later stage.
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
		mu_throw(m, "Invalid parameters to LEFT$()");
	
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
		mu_throw(m, "Invalid parameters to RIGHT$()");
	
	if(len > (int)strlen(s))
		len = strlen(s);
	
	s = s + strlen(s) - len;
	
	rv.type = mu_str;	
	rv.v.s = strdup(s);
	if(!rv.v.s)
		mu_throw(m, "Out of memory");
	
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
		mu_throw(m, "Invalid parameters to MID$()");
	
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
	if(!rv.v.s) mu_throw(m, "Out of memory");
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
	if(!rv.v.s) mu_throw(m, "Out of memory");
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
	if(!rv.v.s) mu_throw(m, "Out of memory");
	
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
 *# It returns the number of items inserted into the array.
 */
static struct mu_par m_data(struct musl *m, int argc, struct mu_par argv[]) {
	struct mu_par rv = {mu_int, {0}};	
	int i, sa = 0;
	char *c, name[TOK_SIZE];
	const char *aname;
	
	if(argc < 1 || argv[0].type != mu_str)
		mu_throw(m, "DATA() must take at least 1 string parameter");	
	
	if(!isalpha(argv[0].v.s[0])) 
		mu_throw(m, "DATA()'s first parameter must be a valid identifier");
	for(c = argv[0].v.s; c[0]; c++)
		if(!isalnum(c[0])) {
			if(c[0] == '$' && c[1] == '\0')
				sa = 1;
			else
				mu_throw(m, "DATA()'s first parameter must be a valid identifier");
		}		
	
	aname = mu_par_str(m, 0);
	if(sa) {		
		for(i = 1; i < argc; i++) {
			snprintf(name, TOK_SIZE, "%s[%d]", aname, i);
			if(!mu_set_str(m, name, mu_par_str(m, i)))
				mu_throw(m, "Out of memory");
		}		
	} else {		
		for(i = 1; i < argc; i++) {
			snprintf(name, TOK_SIZE, "%s[%d]", aname, i);
			if(!mu_set_num(m, name, mu_par_num(m, i)))
				mu_throw(m, "Out of memory");
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
