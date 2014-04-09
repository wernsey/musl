# My Developer Notes
 
## Caveats wrt. traditional BASICs:
 
`PRINT` is a function call, not a statement (mostly because if used as an
embedded scripting language there may not be anywhere to print to).

Comments use hash symbols rather than single quotes. This is partly
because I wanted single quotes to have the same meaning as double quotes,
as in Javacript.
 
Statements found in some old BASICs that are not available include `CLS`, 
`LOCATE`, `WHILE-WEND` and so on.
 
There aren't any arrays, only hash tables, and they are indexed using 
`[]` instead of `()`.
 
`DIM` is not needed (and not implemented) because of the way these hash 
tables are implemented.

* `&` is the string concatenation operator.
* `~` is the not-equals operator.
 
`DATA` is also a function, rather than a statement.
 
The operator `@` operator is for _references_. The expression `@identifier`
converts the identifier to a string, so `@ARRAY$` will be equivalent to
`"ARRAY$"`. 
 
It is useful, because functions like `DATA()` and `MAP()`
takes as a first parameter the name of the variable as a string. Using
the `@` operatior makes it clear that the parameter is a reference to a
variable.

Uninitialised variables are initialised as "", which is treated as 0
in arithmetic.
 
Array indexes are case sensitive: `people["John Doe"]` and
`people["john doe"]` refer to two different variables, even though all
other variables are case insensitive (`Person` and `person` will refer
to the same variable).
 
## Known Issues
 
There are some places where a call to `mu_throw()` could cause memory to 
be leaked. Moral of the story: write your Musl programs in such a way that 
`mu_throw()` need never be called.

I don't do a `<>` operator for _not equals_ (I use `~` instead), because 
the lexer doesn't support operators with multiple characters, but thinking 
about it the parser could check for a `>` after a `<` and problem solved.

`REM` is not supported for comments. I really ought to consider adding it.
 
## ToDo - Built-In Functions
 
Ideas for built-in functions that may be useful go in this section.

I need a function that does more or less the same as `strtok()`, but perhaps 
reentrant. Don't know what to name it. `TOKENIZE()` maybe?

How about an Oracle-like DECODE() function (see 
http://www.techonthenet.com/oracle/functions/decode.php for details)? I can give 
mine a different name, like `SWITCH()` or `SELECT()`

I'd also like to have a feature to dump all the variables somewhere
for debugging Musl programs. It should be an API function and a Built-in
function.
