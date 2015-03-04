Musl
====

My Unstructured Scripting Language

Homepage: http://wstoop.co.za/?musl
Source: http://github.com/wernsey/musl

Musl is my own small BASIC interpreter.

The files in the distribution are:
* musl.c - The source code of the interpreter itself.
* musl.h - The header file, for using the interpreter in an application.
* main.c - The standalone interpreter. Also serves as a demonstration of
	how to embed the interpreter into an application.
* Makefile - The Makefile. To compile, simply type 'make'.
* doc.awk - Awk script that generates documentation from the comments in
    the source code.
* test/ - Directory full of test programs that demonstrates the syntax of
    the interpreter.

Part of the Makefile is to run the code through doc.awk which scans the
comments in the code for specific symbols from which an HTML file, manual.html,
is generated.

Mmmm, I'm going to have to rename the project. The name "Musl" is already
taken (http://www.musl-libc.org/intro.html)

-------------------------------------------------------------------------------
These sources are provided under the terms of the unlicense: 

	This is free and unencumbered software released into the public domain.

	Anyone is free to copy, modify, publish, use, compile, sell, or
	distribute this software, either in source code form or as a compiled
	binary, for any purpose, commercial or non-commercial, and by any
	means.

	In jurisdictions that recognize copyright laws, the author or authors
	of this software dedicate any and all copyright interest in the
	software to the public domain. We make this dedication for the benefit
	of the public at large and to the detriment of our heirs and
	successors. We intend this dedication to be an overt act of
	relinquishment in perpetuity of all present and future rights to this
	software under copyright law.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
	OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
	ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
	OTHER DEALINGS IN THE SOFTWARE.

	For more information, please refer to <http://unlicense.org/>
 