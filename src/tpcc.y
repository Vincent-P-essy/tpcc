%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "tree.h"
#include "symtable.h"
#include "semantic.h"
#include "codegen.h"

extern int lineno;
extern int colno;
extern FILE *yyin;
int yylex(void);
void yyerror(const char *s);

static int print_tree   = 0;
static int print_symtab = 0;
static Node *syntax_tree = NULL;
%}

%union {
    char  *str;
    int    num;
    struct Node *node;
}

%token IF ELSE WHILE RETURN
%token VOID STRUCT
%token <str> TYPE IDENT
%token <num> NUM
%token <str> CHARACTER
%token <str> EQ ORDER ADDSUB DIVSTAR
%token OR AND

%type <node> Prog DeclVars DeclFoncts DeclFonct
%type <node> StructDecl ChampStructs
%type <node> Declarateurs EnTeteFonct Parametres ListTypVar
%type <node> Corps SuiteInstr Instr
%type <node> Exp TB FB M E T F
%type <node> Arguments ListExp StructLValue

%left OR
%left AND
%left EQ
%left ORDER
%left ADDSUB
%left DIVSTAR
%right '!' UMINUS

%expect 3

%%

Prog:
      DeclVars DeclFoncts
    {
        $$ = makeNode(Prog);
        if ($1) addChild($$, $1);
        if ($2) addChild($$, $2);
        syntax_tree = $$;
    }
    ;

DeclVars:
      DeclVars TYPE Declarateurs ';'
    {
        $$ = $1 ? $1 : makeNode(DeclVars);
        Node *tn = makeNodeType(Type, $2); free($2);
        addChild($$, tn); addChild($$, $3);
    }
    | DeclVars STRUCT IDENT Declarateurs ';'
    {
        $$ = $1 ? $1 : makeNode(DeclVars);
        Node *st = makeNode(StructType);
        addChild(st, makeNodeIdent(Ident, $3)); free($3);
        addChild($$, st); addChild($$, $4);
    }
    | DeclVars StructDecl
    {
        $$ = $1 ? $1 : makeNode(DeclVars);
        addChild($$, $2);
    }
    | /* epsilon */ { $$ = NULL; }
    ;

StructDecl:
      STRUCT IDENT '{' ChampStructs '}' ';'
    {
        $$ = makeNode(StructDecl);
        addChild($$, makeNodeIdent(Ident, $2)); free($2);
        addChild($$, $4);
    }
    ;

ChampStructs:
      ChampStructs TYPE Declarateurs ';'
    {
        $$ = $1 ? $1 : makeNode(ChampStructs);
        Node *tn = makeNodeType(Type, $2); free($2);
        addChild($$, tn); addChild($$, $3);
    }
    | ChampStructs STRUCT IDENT Declarateurs ';'
    {
        $$ = $1 ? $1 : makeNode(ChampStructs);
        Node *st = makeNode(StructType);
        addChild(st, makeNodeIdent(Ident, $3)); free($3);
        addChild($$, st); addChild($$, $4);
    }
    | TYPE Declarateurs ';'
    {
        $$ = makeNode(ChampStructs);
        Node *tn = makeNodeType(Type, $1); free($1);
        addChild($$, tn); addChild($$, $2);
    }
    | STRUCT IDENT Declarateurs ';'
    {
        $$ = makeNode(ChampStructs);
        Node *st = makeNode(StructType);
        addChild(st, makeNodeIdent(Ident, $2)); free($2);
        addChild($$, st); addChild($$, $3);
    }
    ;

Declarateurs:
      Declarateurs ',' IDENT
    {
        $$ = $1;
        addChild($$, makeNodeIdent(Ident, $3)); free($3);
    }
    | IDENT
    {
        $$ = makeNode(Declarateurs);
        addChild($$, makeNodeIdent(Ident, $1)); free($1);
    }
    ;

DeclFoncts:
      DeclFoncts DeclFonct
    {
        $$ = $1 ? $1 : makeNode(DeclFoncts);
        addChild($$, $2);
    }
    | DeclFonct
    {
        $$ = makeNode(DeclFoncts);
        addChild($$, $1);
    }
    ;

DeclFonct:
      EnTeteFonct Corps
    {
        $$ = makeNode(DeclFonct);
        addChild($$, $1); addChild($$, $2);
    }
    ;

EnTeteFonct:
      TYPE IDENT '(' Parametres ')'
    {
        $$ = makeNode(EnTeteFonct);
        addChild($$, makeNodeType(Type, $1)); free($1);
        addChild($$, makeNodeIdent(Ident, $2)); free($2);
        if ($4) addChild($$, $4);
    }
    | VOID IDENT '(' Parametres ')'
    {
        $$ = makeNode(EnTeteFonct);
        addChild($$, makeNodeType(Type, "void"));
        addChild($$, makeNodeIdent(Ident, $2)); free($2);
        if ($4) addChild($$, $4);
    }
    | STRUCT IDENT IDENT '(' Parametres ')'
    {
        $$ = makeNode(EnTeteFonct);
        Node *st = makeNode(StructType);
        addChild(st, makeNodeIdent(Ident, $2)); free($2);
        addChild($$, st);
        addChild($$, makeNodeIdent(Ident, $3)); free($3);
        if ($5) addChild($$, $5);
    }
    ;

Parametres:
      VOID    { $$ = NULL; }
    | ListTypVar
    {
        $$ = makeNode(Parametres);
        addChild($$, $1);
    }
    ;

ListTypVar:
      ListTypVar ',' TYPE IDENT
    {
        $$ = $1;
        addChild($$, makeNodeType(Type, $3)); free($3);
        addChild($$, makeNodeIdent(Ident, $4)); free($4);
    }
    | ListTypVar ',' STRUCT IDENT IDENT
    {
        $$ = $1;
        Node *st = makeNode(StructType);
        addChild(st, makeNodeIdent(Ident, $4)); free($4);
        addChild($$, st);
        addChild($$, makeNodeIdent(Ident, $5)); free($5);
    }
    | TYPE IDENT
    {
        $$ = makeNode(ListTypVar);
        addChild($$, makeNodeType(Type, $1)); free($1);
        addChild($$, makeNodeIdent(Ident, $2)); free($2);
    }
    | STRUCT IDENT IDENT
    {
        $$ = makeNode(ListTypVar);
        Node *st = makeNode(StructType);
        addChild(st, makeNodeIdent(Ident, $2)); free($2);
        addChild($$, st);
        addChild($$, makeNodeIdent(Ident, $3)); free($3);
    }
    ;

Corps:
      '{' DeclVars SuiteInstr '}'
    {
        $$ = makeNode(Corps);
        if ($2) addChild($$, $2);
        if ($3) addChild($$, $3);
    }
    ;

SuiteInstr:
      SuiteInstr Instr
    {
        $$ = $1 ? $1 : makeNode(SuiteInstr);
        if ($2) addChild($$, $2);
    }
    | /* epsilon */ { $$ = NULL; }
    ;

Instr:
      IDENT '=' Exp ';'
    {
        $$ = makeNode(InstrAssign);
        addChild($$, makeNodeIdent(Ident, $1)); free($1);
        addChild($$, $3);
    }
    | StructLValue '=' Exp ';'
    {
        $$ = makeNode(InstrAssign);
        addChild($$, $1);
        addChild($$, $3);
    }
    | IF '(' Exp ')' Instr
    {
        $$ = makeNode(InstrIf);
        addChild($$, $3); addChild($$, $5);
    }
    | IF '(' Exp ')' Instr ELSE Instr
    {
        $$ = makeNode(InstrIfElse);
        addChild($$, $3); addChild($$, $5); addChild($$, $7);
    }
    | WHILE '(' Exp ')' Instr
    {
        $$ = makeNode(InstrWhile);
        addChild($$, $3); addChild($$, $5);
    }
    | IDENT '(' Arguments ')' ';'
    {
        $$ = makeNode(InstrCall);
        addChild($$, makeNodeIdent(Ident, $1)); free($1);
        if ($3) addChild($$, $3);
    }
    | RETURN Exp ';'
    {
        $$ = makeNode(InstrReturn);
        addChild($$, $2);
    }
    | RETURN ';'
    {
        $$ = makeNode(InstrReturnVoid);
    }
    | '{' SuiteInstr '}'
    {
        $$ = makeNode(InstrBlock);
        if ($2) addChild($$, $2);
    }
    | ';' { $$ = makeNode(InstrEmpty); }
    ;

Exp:
      Exp OR TB  { $$ = makeNode(OpOr);  addChild($$,$1); addChild($$,$3); }
    | TB          { $$ = $1; }
    ;
TB:
      TB AND FB  { $$ = makeNode(OpAnd); addChild($$,$1); addChild($$,$3); }
    | FB          { $$ = $1; }
    ;
FB:
      FB EQ M    { $$ = makeNodeType(OpEq,$2); free($2); addChild($$,$1); addChild($$,$3); }
    | M           { $$ = $1; }
    ;
M:
      M ORDER E  { $$ = makeNodeType(OpOrder,$2); free($2); addChild($$,$1); addChild($$,$3); }
    | E           { $$ = $1; }
    ;
E:
      E ADDSUB T { $$ = makeNodeType(OpAddsub,$2); free($2); addChild($$,$1); addChild($$,$3); }
    | T           { $$ = $1; }
    ;
T:
      T DIVSTAR F { $$ = makeNodeType(OpDivstar,$2); free($2); addChild($$,$1); addChild($$,$3); }
    | F            { $$ = $1; }
    ;
F:
      ADDSUB F %prec UMINUS
    {
        $$ = makeNodeType(OpUnaryMinus,$1); free($1);
        addChild($$,$2);
    }
    | '!' F
    {
        $$ = makeNode(OpNot);
        addChild($$,$2);
    }
    | '(' Exp ')' { $$ = $2; }
    | NUM         { $$ = makeNodeNum(Num,$1); }
    | CHARACTER   { $$ = makeNodeIdent(Character,$1); free($1); }
    | IDENT       { $$ = makeNodeIdent(Ident,$1); free($1); }
    | IDENT '(' Arguments ')'
    {
        $$ = makeNode(AppelFonct);
        addChild($$, makeNodeIdent(Ident,$1)); free($1);
        if ($3) addChild($$,$3);
    }
    | F '.' IDENT
    {
        $$ = makeNode(AccesChamp);
        addChild($$, $1);
        addChild($$, makeNodeIdent(Ident,$3)); free($3);
    }
    ;

Arguments:
      ListExp
    {
        $$ = makeNode(Arguments);
        if ($1) addChild($$,$1);
    }
    | /* epsilon */ { $$ = NULL; }
    ;

ListExp:
      ListExp ',' Exp
    {
        $$ = $1 ? $1 : makeNode(ListExp);
        addChild($$,$3);
    }
    | Exp
    {
        $$ = makeNode(ListExp);
        addChild($$,$1);
    }
    ;

/* LValue gauche pour l'affectation de champ de structure */
StructLValue:
      IDENT '.' IDENT
    {
        $$ = makeNode(AccesChamp);
        addChild($$, makeNodeIdent(Ident,$1)); free($1);
        addChild($$, makeNodeIdent(Ident,$3)); free($3);
    }
    | StructLValue '.' IDENT
    {
        $$ = makeNode(AccesChamp);
        addChild($$, $1);
        addChild($$, makeNodeIdent(Ident,$3)); free($3);
    }
    | IDENT '(' Arguments ')' '.' IDENT
    {
        Node *call = makeNode(AppelFonct);
        addChild(call, makeNodeIdent(Ident,$1)); free($1);
        if ($3) addChild(call,$3);
        $$ = makeNode(AccesChamp);
        addChild($$, call);
        addChild($$, makeNodeIdent(Ident,$6)); free($6);
    }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Erreur syntaxique ligne %d, colonne %d: %s\n", lineno, colno, s);
}

static void print_help(const char *prog) {
    printf("Usage: %s [OPTIONS] [FILE.tpc]\n", prog);
    printf("Compilateur TPC → NASM (elf64 AMD64)\n\n");
    printf("Options:\n");
    printf("  -t, --tree     Affiche l'arbre abstrait sur stdout\n");
    printf("  -s, --symtabs  Affiche les tables des symboles sur stdout\n");
    printf("  -h, --help     Affiche cette aide\n");
    printf("\nValeurs de retour:\n");
    printf("  0  aucune erreur\n");
    printf("  1  erreur lexicale ou syntaxique\n");
    printf("  2  erreur sémantique\n");
    printf("  3+ autre erreur\n");
    printf("\nExemples:\n");
    printf("  %s < prog.tpc\n", prog);
    printf("  %s -t prog.tpc\n", prog);
}

int main(int argc, char **argv) {
    char *input_file  = NULL;
    char *output_file = NULL;
    int   opt;

    static struct option long_opts[] = {
        {"tree",    no_argument, 0, 't'},
        {"symtabs", no_argument, 0, 's'},
        {"help",    no_argument, 0, 'h'},
        {0,0,0,0}
    };

    while ((opt = getopt_long(argc, argv, "tsh", long_opts, NULL)) != -1) {
        switch (opt) {
            case 't': print_tree   = 1; break;
            case 's': print_symtab = 1; break;
            case 'h': print_help(argv[0]); return 0;
            default:
                fprintf(stderr, "Option inconnue. Utilisez -h pour l'aide.\n");
                return 3;
        }
    }

    if (optind < argc)
        input_file = argv[optind];

    /* Ouvrir l'entrée */
    if (input_file) {
        yyin = fopen(input_file, "r");
        if (!yyin) {
            fprintf(stderr, "Erreur: impossible d'ouvrir '%s'\n", input_file);
            return 3;
        }
        /* Nom du fichier de sortie: remplace .tpc par .asm */
        output_file = malloc(strlen(input_file) + 8);
        if (!output_file) { fprintf(stderr,"Out of memory\n"); return 3; }
        strcpy(output_file, input_file);
        char *dot = strrchr(output_file, '.');
        if (dot && strcmp(dot, ".tpc") == 0) strcpy(dot, ".asm");
        else strcat(output_file, ".asm");
    } else {
        output_file = "_anonymous.asm";
    }

    /* Analyse syntaxique + construction AST */
    sym_init();
    int parse_result = yyparse();

    if (input_file && yyin) fclose(yyin);

    if (parse_result != 0) return 1;

    /* Afficher l'arbre si demandé */
    if (print_tree && syntax_tree) printTree(syntax_tree);

    /* Analyse sémantique */
    int sem_result = analyse_semantique(syntax_tree);

    if (print_symtab) print_symtables();

    if (sem_result != 0) {
        if (syntax_tree) deleteTree(syntax_tree);
        return 2;
    }

    /* Génération de code */
    FILE *asm_out = fopen(output_file, "w");
    if (!asm_out) {
        fprintf(stderr, "Erreur: impossible de créer '%s'\n", output_file);
        return 3;
    }
    generate_code(syntax_tree, asm_out);
    fclose(asm_out);

    if (syntax_tree) deleteTree(syntax_tree);
    if (input_file)  free(output_file);
    return 0;
}
