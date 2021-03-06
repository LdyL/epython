%{
#include "byteassembler.h"
#include "memorymanager.h"
#include "stack.h"
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

extern int line_num;
extern char * parsing_filename;
void yyerror(char const*);
int yylex(void);

void yyerror (char const *msg) {
	fprintf(stderr, "%s at line %d of file %s\n", msg, line_num, parsing_filename);
	exit(0);
}
%}

%union {
	int integer;
	unsigned char uchar;
	float real;	
	struct memorycontainer * data;
	char *string;
	struct stack_t * stack;
}

%token <integer> INTEGER
%token <real>    REAL
%token <string>  STRING IDENTIFIER

%token NEWLINE INDENT OUTDENT
%token DIM SDIM EXIT ELSE ELIF COMMA WHILE
%token FOR TO FROM NEXT GOTO PRINT INPUT
%token IF NATIVE

%token ADD SUB COLON DEF RET NONE FILESTART IN ADDADD SUBSUB MULMUL DIVDIV MODMOD POWPOW FLOORDIVFLOORDIV FLOORDIV
%token MULT DIV MOD AND OR NEQ LEQ GEQ LT GT EQ IS NOT STR
%token LPAREN RPAREN SLBRACE SRBRACE TRUE FALSE

%left ADD SUB ADDADD SUBSUB
%left MULT DIV MOD MULMUL DIVDIV MODMOD
%left AND OR
%left NEQ LEQ GEQ LT GT EQ IS ASSGN
%right NOT
%right POW POWPOW FLOORDIVFLOORDIV FLOORDIV

%type <string> ident declareident fn_entry
%type <integer> unary_operator 
%type <uchar> opassgn
%type <data> constant expression logical_or_expression logical_and_expression equality_expression relational_expression additive_expression multiplicative_expression value statement statements line lines codeblock elifblock
%type <stack> fndeclarationargs fncallargs commaseparray arrayaccessor

%start program 

%%

program : lines { compileMemory($1); }

lines
        : line
        | lines line { $$=concatenateMemory($1, $2); }
;

line
        : statements NEWLINE { $$ = $1; }
        | statements { $$ = $1; }        
	    | NEWLINE { $$ = NULL; }
;

statements
	: statement statements { $$=concatenateMemory($1, $2); }
	| statement
;

statement	
	: FOR declareident IN expression COLON codeblock { $$=appendForStatement($2, $4, $6); leaveScope(); }
	| WHILE expression COLON codeblock { $$=appendWhileStatement($2, $4); }	
	| IF expression COLON codeblock { $$=appendIfStatement($2, $4); }
	| IF expression COLON codeblock ELSE COLON codeblock { $$=appendIfElseStatement($2, $4, $7); }
	| IF expression COLON codeblock elifblock { $$=appendIfElseStatement($2, $4, $5); }		
	| IF expression COLON statements { $$=appendIfStatement($2, $4); }
	| ELIF expression COLON codeblock { $$=appendIfStatement($2, $4); }		
    	| ident ASSGN expression { $$=appendLetStatement($1, $3); }
    	| ident arrayaccessor ASSGN expression { $$=appendArraySetStatement($1, $2, $4); }
    	| ident opassgn expression { $$=appendLetWithOperatorStatement($1, $3, $2); }
	| PRINT expression { $$=appendNativeCallFunctionStatement("rtl_print", NULL, $2); }	
	| EXIT LPAREN RPAREN{ $$=appendStopStatement(); }	
	| fn_entry LPAREN fndeclarationargs RPAREN COLON codeblock { appendNewFunctionStatement($1, $3, $6); leaveScope(); $$ = NULL; }
	| RET { $$ = appendReturnStatement(); }	
	| RET expression { $$ = appendReturnStatementWithExpression($2); }
	| ident LPAREN fncallargs RPAREN { $$=appendCallFunctionStatement($1, $3); }
	| NATIVE ident LPAREN fncallargs RPAREN { $$=appendNativeCallFunctionStatement($2, $4, NULL); }
;

arrayaccessor
	: SLBRACE expression SRBRACE { $$=getNewStack(); pushExpression($$, $2); }
	| arrayaccessor SLBRACE expression SRBRACE { pushExpression($1, $3); }
;

fncallargs
	: /*blank*/ { $$=getNewStack(); }	
	| expression { $$=getNewStack(); pushExpression($$, $1); }
	| fncallargs COMMA expression { pushExpression($1, $3); $$=$1; }
	;

fndeclarationargs
	: /*blank*/ { enterScope(); $$=getNewStack(); }
	| ident { $$=getNewStack(); enterScope(); pushIdentifier($$, $1); appendArgument($1); }
	| ident ASSGN expression { $$=getNewStack(); enterScope(); pushIdentifierAssgnExpression($$, $1, $3); appendArgument($1); }
	| fndeclarationargs COMMA ident { pushIdentifier($1, $3); $$=$1; appendArgument($3); }	
	| fndeclarationargs COMMA ident ASSGN expression { pushIdentifierAssgnExpression($1, $3, $5); $$=$1; appendArgument($3); }
	;
	
fn_entry
	: DEF ident { enterFunction($2); $$=$2; }

codeblock
	: NEWLINE indent_rule lines outdent_rule { $$=$3; }
	
indent_rule
	: INDENT { enterScope(); }
	
outdent_rule
	: OUTDENT { leaveScope(); }
	
opassgn
	: ADDADD { $$=0; }
	| SUBSUB { $$=1; }
	| MULMUL { $$=2; }
	| DIVDIV { $$=3; }
	| MODMOD { $$=4; }
	| POWPOW { $$=5; }
	| FLOORDIVFLOORDIV { $$=6; }

declareident
	 : ident { $$=$1; enterScope(); addVariableIfNeeded($1); }
;

elifblock
	: ELIF expression COLON codeblock { $$=appendIfStatement($2, $4); }
	| ELIF expression COLON codeblock ELSE COLON codeblock { $$=appendIfElseStatement($2, $4, $7); }
	| ELIF expression COLON codeblock elifblock { $$=appendIfElseStatement($2, $4, $5); }
;

expression
	: logical_or_expression { $$=$1; }
	| NOT logical_or_expression { $$=createNotExpression($2); }
;

logical_or_expression
	: logical_and_expression { $$=$1; }
	| logical_or_expression OR logical_and_expression { $$=createOrExpression($1, $3); }

logical_and_expression
	: equality_expression { $$=$1; }
	| logical_and_expression AND equality_expression { $$=createAndExpression($1, $3); }
;

equality_expression
	: relational_expression { $$=$1; }
	| equality_expression EQ relational_expression { $$=createEqExpression($1, $3); }
	| equality_expression NEQ relational_expression { $$=createNeqExpression($1, $3); }
	| equality_expression IS relational_expression { $$=createIsExpression($1, $3); }
;

relational_expression
	: additive_expression { $$=$1; }
	| relational_expression GT additive_expression { $$=createGtExpression($1, $3); }
	| relational_expression LT additive_expression { $$=createLtExpression($1, $3); }
	| relational_expression LEQ additive_expression { $$=createLeqExpression($1, $3); }
	| relational_expression GEQ additive_expression { $$=createGeqExpression($1, $3); }
;

additive_expression
	: multiplicative_expression { $$=$1; }
	| additive_expression ADD multiplicative_expression { $$=createAddExpression($1, $3); }
	| additive_expression SUB multiplicative_expression { $$=createSubExpression($1, $3); }
;

multiplicative_expression
	: value { $$=$1; }
	| multiplicative_expression MULT value { $$=createMulExpression($1, $3); }
	| multiplicative_expression DIV value { $$=createDivExpression($1, $3); }
	| multiplicative_expression FLOORDIV value { $$=createFloorDivExpression($1, $3); }
	| multiplicative_expression MOD value { $$=createModExpression($1, $3); }
	| multiplicative_expression POW value { $$=createPowExpression($1, $3); }
	| STR LPAREN expression RPAREN { $$=$3; } 	
	| SLBRACE commaseparray SRBRACE { $$=createArrayExpression($2, NULL); }
	| SLBRACE commaseparray SRBRACE MULT value { $$=createArrayExpression($2, $5); }
	| INPUT LPAREN RPAREN { $$=appendNativeCallFunctionStatement("rtl_input", NULL, NULL); }
	| INPUT LPAREN expression RPAREN { $$=appendNativeCallFunctionStatement("rtl_inputprint", NULL, $3); }	
;

commaseparray
	: expression { $$=getNewStack(); pushExpression($$, $1); }
	| commaseparray COMMA expression { pushExpression($1, $3); }
;

value
	: constant { $$=$1; }
	| LPAREN expression RPAREN { $$=$2; }
	| ident { $$=createIdentifierExpression($1); }
	| ident arrayaccessor { $$=createIdentifierArrayAccessExpression($1, $2); }
	| ident LPAREN fncallargs RPAREN { $$=appendCallFunctionStatement($1, $3); }
	| NATIVE ident LPAREN fncallargs RPAREN { $$=appendNativeCallFunctionStatement($2, $4, NULL); }
;

ident
	: IDENTIFIER { $$ = malloc(strlen($1)+1); strcpy($$, $1); }	
;

constant
        : INTEGER { $$=createIntegerExpression($1); }
        | REAL { $$=createRealExpression($1); }
	| unary_operator INTEGER { $$=createIntegerExpression($1 * $2); }	
	| unary_operator REAL { $$=createRealExpression($1 * $2); }		
	| STRING { $$=createStringExpression($1); }	
	| TRUE { $$=createBooleanExpression(1); }
	| FALSE { $$=createBooleanExpression(0); }
	| NONE { $$=createNoneExpression(); }
;

unary_operator
	: ADD { $$ = 1; }
	| SUB { $$ = -1; }
;

%%
