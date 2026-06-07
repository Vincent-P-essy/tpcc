/* symtable.h - Tables des symboles pour tpcc */
#ifndef SYMTABLE_H
#define SYMTABLE_H

#define MAX_NAME   128
#define MAX_FIELDS 64
#define MAX_PARAMS 32

/* ---- Types ---- */
typedef enum { TY_INT, TY_CHAR, TY_VOID, TY_STRUCT } TypeKind;

typedef struct {
    TypeKind kind;
    char struct_name[MAX_NAME]; /* rempli si kind == TY_STRUCT */
} TpcType;

/* ---- Catégories de symboles ---- */
typedef enum { SYM_GVAR, SYM_LVAR, SYM_PARAM, SYM_FUNC, SYM_SDEF } SymKind;

/* ---- Champ d'une structure ---- */
typedef struct {
    char name[MAX_NAME];
    TpcType type;
    int offset; /* offset dans la structure */
    int size;
} Field;

/* ---- Entrée de table des symboles ---- */
typedef struct Symbol {
    char name[MAX_NAME];
    SymKind kind;
    TpcType type;       /* type de la variable/param, ou type de retour de fonction */
    int offset;         /* locaux/params: offset négatif depuis rbp; gvar: ignoré */
    int size;           /* taille en octets */
    /* pour les fonctions */
    int nparams;
    struct Symbol *params; /* liste chaînée des paramètres (ordre inverse de déclaration) */
    /* pour les définitions de struct */
    Field fields[MAX_FIELDS];
    int nfields;
    int struct_size;
    /* chaînage */
    struct Symbol *next;
} Symbol;

/* ---- Tables globales ---- */
extern Symbol *g_structs;  /* définitions de structures */
extern Symbol *g_vars;     /* variables globales */
extern Symbol *g_funcs;    /* fonctions */
extern Symbol *l_vars;     /* variables locales + paramètres de la fonction courante */

extern Symbol *current_func; /* fonction en cours d'analyse */

/* ---- Initialisation ---- */
void sym_init(void);
void sym_clear_locals(void);

/* ---- Ajout de symboles ---- */
Symbol *sym_add_struct(const char *name);
int     sym_struct_add_field(Symbol *s, const char *fname, TpcType ftype);
Symbol *sym_add_gvar(const char *name, TpcType type);
Symbol *sym_add_lvar(const char *name, TpcType type, int *next_offset);
Symbol *sym_add_param(const char *name, TpcType type, int *next_offset);
Symbol *sym_add_func(const char *name, TpcType ret_type);

/* ---- Recherche ---- */
Symbol *sym_lookup_struct(const char *name);
Symbol *sym_lookup_gvar(const char *name);
Symbol *sym_lookup_lvar(const char *name);
Symbol *sym_lookup_func(const char *name);
Symbol *sym_lookup_any(const char *name); /* local puis global */

/* ---- Utilitaires ---- */
int     type_size(TpcType t);
int     struct_compute_size(Symbol *s);
TpcType type_int(void);
TpcType type_char(void);
TpcType type_void(void);
TpcType type_struct(const char *name);
const char *type_str(TpcType t);
int     types_compatible(TpcType dst, TpcType src); /* 1=ok, 0=warning, -1=error */

void print_symtables(void);

#endif /* SYMTABLE_H */
