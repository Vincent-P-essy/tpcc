/* symtable.c */
#include "symtable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Symbol *g_structs  = NULL;
Symbol *g_vars     = NULL;
Symbol *g_funcs    = NULL;
Symbol *l_vars     = NULL;
Symbol *current_func = NULL;

void sym_init(void) {
    g_structs = g_vars = g_funcs = l_vars = current_func = NULL;
}

void sym_clear_locals(void) {
    Symbol *s = l_vars;
    while (s) {
        Symbol *next = s->next;
        free(s);
        s = next;
    }
    l_vars = NULL;
}

/* ---- Constructeurs de types ---- */
TpcType type_int(void)  { TpcType t; t.kind = TY_INT;  t.struct_name[0]=0; return t; }
TpcType type_char(void) { TpcType t; t.kind = TY_CHAR; t.struct_name[0]=0; return t; }
TpcType type_void(void) { TpcType t; t.kind = TY_VOID; t.struct_name[0]=0; return t; }
TpcType type_struct(const char *name) {
    TpcType t; t.kind = TY_STRUCT;
    strncpy(t.struct_name, name, MAX_NAME-1);
    t.struct_name[MAX_NAME-1] = 0;
    return t;
}

const char *type_str(TpcType t) {
    static char buf[MAX_NAME+16];
    switch (t.kind) {
        case TY_INT:    return "int";
        case TY_CHAR:   return "char";
        case TY_VOID:   return "void";
        case TY_STRUCT: snprintf(buf, sizeof(buf), "struct %s", t.struct_name); return buf;
    }
    return "?";
}

/* 1=compatible, 0=warning (int→char), -1=erreur */
int types_compatible(TpcType dst, TpcType src) {
    if (dst.kind == src.kind) {
        if (dst.kind == TY_STRUCT)
            return strcmp(dst.struct_name, src.struct_name) == 0 ? 1 : -1;
        return 1;
    }
    /* int → char : warning */
    if (dst.kind == TY_CHAR && src.kind == TY_INT) return 0;
    /* char → int : ok implicite */
    if (dst.kind == TY_INT  && src.kind == TY_CHAR) return 1;
    return -1;
}

/* ---- Taille ---- */
int struct_compute_size(Symbol *s) {
    if (!s || s->kind != SYM_SDEF) return 0;
    if (s->struct_size > 0) return s->struct_size;
    int total = 0;
    for (int i = 0; i < s->nfields; i++) total += s->fields[i].size;
    s->struct_size = total;
    return total;
}

int type_size(TpcType t) {
    switch (t.kind) {
        case TY_INT:  return 4;
        case TY_CHAR: return 1;
        case TY_VOID: return 0;
        case TY_STRUCT: {
            Symbol *s = sym_lookup_struct(t.struct_name);
            return s ? struct_compute_size(s) : 0;
        }
    }
    return 0;
}

/* ---- Allocation d'un symbole ---- */
static Symbol *new_sym(const char *name, SymKind kind, TpcType type) {
    Symbol *s = calloc(1, sizeof(Symbol));
    if (!s) { fprintf(stderr, "Out of memory\n"); exit(3); }
    strncpy(s->name, name, MAX_NAME-1);
    s->kind = kind;
    s->type = type;
    return s;
}

/* ---- Structures ---- */
Symbol *sym_add_struct(const char *name) {
    Symbol *s = new_sym(name, SYM_SDEF, type_void());
    s->next = g_structs;
    g_structs = s;
    return s;
}

int sym_struct_add_field(Symbol *s, const char *fname, TpcType ftype) {
    if (s->nfields >= MAX_FIELDS) return -1;
    Field *f = &s->fields[s->nfields];
    strncpy(f->name, fname, MAX_NAME-1);
    f->type = ftype;
    f->size = type_size(ftype) < 1 ? 4 : type_size(ftype);
    /* align to 4 bytes */
    if (f->size < 4) f->size = 4;
    f->offset = s->struct_size;
    s->struct_size += f->size;
    s->nfields++;
    return 0;
}

/* ---- Variables globales ---- */
Symbol *sym_add_gvar(const char *name, TpcType type) {
    Symbol *s = new_sym(name, SYM_GVAR, type);
    s->size = type_size(type);
    if (s->size < 1) s->size = 1;
    s->next = g_vars;
    g_vars = s;
    return s;
}

/* ---- Variables locales / paramètres ---- */
/* next_offset: pointeur vers le prochain slot disponible (décrémente) */
Symbol *sym_add_lvar(const char *name, TpcType type, int *next_offset) {
    Symbol *s = new_sym(name, SYM_LVAR, type);
    /* Allouer 8 octets par variable sur la pile pour simplifier l'alignement */
    *next_offset -= 8;
    s->offset = *next_offset;
    s->size = type_size(type) < 1 ? 1 : type_size(type);
    s->next = l_vars;
    l_vars = s;
    return s;
}

Symbol *sym_add_param(const char *name, TpcType type, int *next_offset) {
    Symbol *s = new_sym(name, SYM_PARAM, type);
    *next_offset -= 8;
    s->offset = *next_offset;
    s->size = type_size(type) < 1 ? 1 : type_size(type);
    s->next = l_vars;
    l_vars = s;
    return s;
}

/* ---- Fonctions ---- */
Symbol *sym_add_func(const char *name, TpcType ret_type) {
    Symbol *s = new_sym(name, SYM_FUNC, ret_type);
    s->next = g_funcs;
    g_funcs = s;
    return s;
}

/* ---- Recherche ---- */
Symbol *sym_lookup_struct(const char *name) {
    for (Symbol *s = g_structs; s; s = s->next)
        if (strcmp(s->name, name) == 0) return s;
    return NULL;
}

Symbol *sym_lookup_gvar(const char *name) {
    for (Symbol *s = g_vars; s; s = s->next)
        if (strcmp(s->name, name) == 0) return s;
    return NULL;
}

Symbol *sym_lookup_lvar(const char *name) {
    for (Symbol *s = l_vars; s; s = s->next)
        if (strcmp(s->name, name) == 0) return s;
    return NULL;
}

Symbol *sym_lookup_func(const char *name) {
    for (Symbol *s = g_funcs; s; s = s->next)
        if (strcmp(s->name, name) == 0) return s;
    return NULL;
}

Symbol *sym_lookup_any(const char *name) {
    Symbol *s = sym_lookup_lvar(name);
    if (s) return s;
    s = sym_lookup_gvar(name);
    if (s) return s;
    return NULL;
}

/* ---- Affichage des tables ---- */
static void print_type(TpcType t) {
    printf("%s", type_str(t));
}

void print_symtables(void) {
    printf("\n=== TABLE DES STRUCTURES ===\n");
    for (Symbol *s = g_structs; s; s = s->next) {
        printf("  struct %s (taille=%d)\n", s->name, s->struct_size);
        for (int i = 0; i < s->nfields; i++) {
            printf("    .%s : %s  (offset=%d, size=%d)\n",
                   s->fields[i].name, type_str(s->fields[i].type),
                   s->fields[i].offset, s->fields[i].size);
        }
    }
    printf("\n=== TABLE DES VARIABLES GLOBALES ===\n");
    for (Symbol *s = g_vars; s; s = s->next) {
        printf("  %s : ", s->name); print_type(s->type);
        printf("  (taille=%d)\n", s->size);
    }
    printf("\n=== TABLE DES FONCTIONS ===\n");
    for (Symbol *s = g_funcs; s; s = s->next) {
        printf("  %s (", s->name); print_type(s->type); printf(")");
        printf("  nparams=%d\n", s->nparams);
        for (Symbol *p = s->params; p; p = p->next) {
            printf("    param %s : %s\n", p->name, type_str(p->type));
        }
    }
    printf("\n=== TABLE LOCALE COURANTE ===\n");
    for (Symbol *s = l_vars; s; s = s->next) {
        printf("  %s : ", s->name); print_type(s->type);
        printf("  (offset=%d, kind=%s)\n", s->offset,
               s->kind==SYM_PARAM ? "param" : "local");
    }
}
