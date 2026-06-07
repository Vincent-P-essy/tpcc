/* semantic.c - Analyse sémantique pour tpcc */
#include "semantic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

int sem_errors   = 0;
int sem_warnings = 0;

/* ---- Messages ---- */
static void sem_error(int line, const char *fmt, ...) {
    va_list ap;
    sem_errors++;
    fprintf(stderr, "Erreur sémantique ligne %d: ", line);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fprintf(stderr, "\n");
}

static void sem_warning(int line, const char *fmt, ...) {
    va_list ap;
    sem_warnings++;
    fprintf(stderr, "Avertissement ligne %d: ", line);
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fprintf(stderr, "\n");
}

/* ---- Forward declarations ---- */
static void sem_decl_vars(Node *dv, int is_global, int *offset);
static void sem_champ_structs(Node *cs, Symbol *st);
static void sem_func_signature(Node *func);
static void sem_func_body(Node *func);
static void sem_corps(Node *corps);
static void sem_suite_instr(Node *suite);
static void sem_instr(Node *instr);
static TpcType sem_expr(Node *expr);

/* ---- Helpers ---- */
static TpcType get_type_from_node(Node *type_node) {
    if (type_node->label == Type) {
        if      (strcmp(type_node->type, "int")  == 0) return type_int();
        else if (strcmp(type_node->type, "void") == 0) return type_void();
        else                                           return type_char();
    } else { /* StructType */
        Node *id = type_node->firstChild;
        return type_struct(id ? id->ident : "");
    }
}

/* Vérifie les types des arguments d'un appel de fonction.
   params est en ordre inverse (dernier param en tête). */
static void check_call_args(Node *args_node, Symbol *fsym, int lineno) {
    Symbol *param_arr[MAX_PARAMS];
    int nparam = 0;
    for (Symbol *p = fsym->params; p && nparam < MAX_PARAMS; p = p->next)
        param_arr[nparam++] = p;

    int nargs = 0;
    if (args_node && args_node->label == Arguments) {
        Node *lexp = args_node->firstChild;
        if (lexp) {
            for (Node *e = lexp->firstChild; e; e = e->nextSibling) {
                TpcType atype = sem_expr(e);
                if (nargs < nparam) {
                    /* params stockés en ordre inverse: arg 0 ↔ param[nparam-1] */
                    TpcType ptype = param_arr[nparam - 1 - nargs]->type;
                    int compat = types_compatible(ptype, atype);
                    if (compat == -1)
                        sem_error(e->lineno,
                            "argument %d de '%s': type incompatible (%s attendu, %s fourni)",
                            nargs + 1, fsym->name, type_str(ptype), type_str(atype));
                    else if (compat == 0)
                        sem_warning(e->lineno,
                            "argument %d de '%s': conversion int vers char",
                            nargs + 1, fsym->name);
                }
                nargs++;
            }
        }
    }
    if (nargs != fsym->nparams)
        sem_error(lineno,
            "fonction '%s': %d argument(s) attendu(s), %d fourni(s)",
            fsym->name, fsym->nparams, nargs);
}

/* Parcourt Declarateurs et déclare chaque ident */
static void sem_declarateurs(Node *decl, TpcType t, int is_global, int *offset) {
    if (!decl) return;
    for (Node *id = decl->firstChild; id; id = id->nextSibling) {
        const char *name = id->ident;
        if (is_global) {
            if (sym_lookup_gvar(name) || sym_lookup_func(name)) {
                sem_error(id->lineno, "identificateur '%s' déjà déclaré globalement", name);
            } else {
                sym_add_gvar(name, t);
            }
        } else {
            if (sym_lookup_lvar(name)) {
                sem_error(id->lineno, "variable locale '%s' déjà déclarée dans cette fonction", name);
            } else {
                sym_add_lvar(name, t, offset);
            }
        }
    }
}

/* ---- DeclVars ---- */
static void sem_decl_vars(Node *dv, int is_global, int *offset) {
    if (!dv) return;
    Node *child = dv->firstChild;
    while (child) {
        if (child->label == StructDecl) {
            Node *name_id = child->firstChild;
            Node *cs      = name_id ? name_id->nextSibling : NULL;
            const char *sname = name_id ? name_id->ident : "";
            if (sym_lookup_struct(sname)) {
                sem_error(child->lineno, "type struct '%s' déjà déclaré", sname);
            } else {
                Symbol *st = sym_add_struct(sname);
                sem_champ_structs(cs, st);
            }
            child = child->nextSibling;
        } else if (child->label == Type || child->label == StructType) {
            TpcType t = get_type_from_node(child);
            if (t.kind == TY_STRUCT && !sym_lookup_struct(t.struct_name))
                sem_error(child->lineno, "type 'struct %s' non déclaré", t.struct_name);
            Node *decl = child->nextSibling;
            if (decl && decl->label == Declarateurs) {
                sem_declarateurs(decl, t, is_global, offset);
                child = decl->nextSibling;
            } else {
                child = child->nextSibling;
            }
        } else {
            child = child->nextSibling;
        }
    }
}

/* ---- ChampStructs ---- */
static void sem_champ_structs(Node *cs, Symbol *st) {
    if (!cs) return;
    Node *child = cs->firstChild;
    while (child) {
        if (child->label == Type || child->label == StructType) {
            TpcType t = get_type_from_node(child);
            if (t.kind == TY_STRUCT && !sym_lookup_struct(t.struct_name))
                sem_error(child->lineno, "type 'struct %s' non déclaré", t.struct_name);
            Node *decl = child->nextSibling;
            if (decl && decl->label == Declarateurs) {
                for (Node *id = decl->firstChild; id; id = id->nextSibling)
                    sym_struct_add_field(st, id->ident, t);
                child = decl->nextSibling;
            } else {
                child = child->nextSibling;
            }
        } else {
            child = child->nextSibling;
        }
    }
}

/* ---- 1er passage: enregistrer la signature d'une fonction ---- */
static void sem_func_signature(Node *func) {
    Node *header = func->firstChild;
    if (!header) return;

    Node *ret_type_node = header->firstChild;
    Node *name_node     = ret_type_node ? ret_type_node->nextSibling : NULL;
    Node *params_node   = name_node    ? name_node->nextSibling : NULL;
    if (!ret_type_node || !name_node) return;

    TpcType ret_type = get_type_from_node(ret_type_node);
    const char *fname = name_node->ident;

    if (sym_lookup_func(fname)) {
        sem_error(name_node->lineno, "fonction '%s' déjà déclarée", fname);
        return;
    } else if (sym_lookup_gvar(fname)) {
        sem_error(name_node->lineno, "identificateur '%s' déjà déclaré comme variable globale", fname);
    }

    Symbol *fsym = sym_add_func(fname, ret_type);

    sym_clear_locals();
    int next_offset = 0;

    if (params_node && params_node->label == Parametres) {
        Node *ltv = params_node->firstChild;
        if (ltv) {
            Node *child = ltv->firstChild;
            while (child) {
                if (child->label == Type || child->label == StructType) {
                    TpcType pt = get_type_from_node(child);
                    if (pt.kind == TY_STRUCT && !sym_lookup_struct(pt.struct_name))
                        sem_error(child->lineno, "type 'struct %s' non déclaré", pt.struct_name);
                    Node *pname_node = child->nextSibling;
                    if (pname_node) {
                        const char *pname = pname_node->ident;
                        if (sym_lookup_lvar(pname)) {
                            sem_error(pname_node->lineno, "paramètre '%s' dupliqué", pname);
                        } else {
                            sym_add_param(pname, pt, &next_offset);
                            fsym->nparams++;
                        }
                        child = pname_node->nextSibling;
                    } else {
                        child = child->nextSibling;
                    }
                } else {
                    child = child->nextSibling;
                }
            }
        }
    }

    /* Transférer l_vars → fsym->params directement, sans copie */
    fsym->params = l_vars;
    l_vars = NULL;
}

/* ---- 2ème passage: analyser le corps d'une fonction ---- */
static void sem_func_body(Node *func) {
    Node *header = func->firstChild;
    Node *corps  = header ? header->nextSibling : NULL;
    if (!header) return;

    Node *ret_type_node = header->firstChild;
    Node *name_node     = ret_type_node ? ret_type_node->nextSibling : NULL;
    if (!name_node) return;

    Symbol *fsym = sym_lookup_func(name_node->ident);
    if (!fsym) return;

    current_func = fsym;

    /* Restaurer l_vars avec les params pour que sym_lookup_lvar les trouve */
    l_vars = fsym->params;

    if (corps) sem_corps(corps);

    /* Libérer uniquement les locaux ajoutés par sem_corps (pas les params) */
    Symbol *s = l_vars;
    while (s && s->kind == SYM_LVAR) {
        Symbol *next_s = s->next;
        free(s);
        s = next_s;
    }
    l_vars = NULL;
}

/* ---- Corps ---- */
static void sem_corps(Node *corps) {
    Node *child = corps->firstChild;
    Node *dv = NULL, *suite = NULL;
    if (child) {
        if (child->label == DeclVars) {
            dv = child; suite = child->nextSibling;
        } else {
            suite = child;
        }
    }
    /* Continuer l'allocation de pile sous les paramètres déjà en l_vars */
    int min_offset = 0;
    for (Symbol *s = l_vars; s; s = s->next)
        if (s->offset < min_offset) min_offset = s->offset;
    int local_off = min_offset;
    if (dv) {
        Node *child2 = dv->firstChild;
        while (child2) {
            if (child2->label == Type || child2->label == StructType) {
                TpcType t = get_type_from_node(child2);
                if (t.kind == TY_STRUCT && !sym_lookup_struct(t.struct_name))
                    sem_error(child2->lineno, "type 'struct %s' non déclaré", t.struct_name);
                Node *decl = child2->nextSibling;
                if (decl && decl->label == Declarateurs) {
                    for (Node *id = decl->firstChild; id; id = id->nextSibling) {
                        if (sym_lookup_lvar(id->ident)) {
                            sem_error(id->lineno,
                                "variable locale '%s' a le même nom qu'un paramètre ou est déjà déclarée",
                                id->ident);
                        } else {
                            sym_add_lvar(id->ident, t, &local_off);
                        }
                    }
                    child2 = decl->nextSibling;
                } else {
                    child2 = child2->nextSibling;
                }
            } else if (child2->label == StructDecl) {
                Node *name_id = child2->firstChild;
                Node *cs      = name_id ? name_id->nextSibling : NULL;
                const char *sname = name_id ? name_id->ident : "";
                if (sym_lookup_struct(sname)) {
                    sem_error(child2->lineno, "type struct '%s' déjà déclaré", sname);
                } else {
                    Symbol *st = sym_add_struct(sname);
                    sem_champ_structs(cs, st);
                }
                child2 = child2->nextSibling;
            } else {
                child2 = child2->nextSibling;
            }
        }
    }
    if (suite) sem_suite_instr(suite);
}

/* ---- Instructions ---- */
static void sem_suite_instr(Node *suite) {
    if (!suite) return;
    for (Node *instr = suite->firstChild; instr; instr = instr->nextSibling)
        sem_instr(instr);
}

static void sem_instr(Node *instr) {
    if (!instr) return;
    switch (instr->label) {
        case InstrAssign: {
            Node *lhs_node = instr->firstChild;
            Node *exp_node = lhs_node ? lhs_node->nextSibling : NULL;
            if (!lhs_node) break;

            if (lhs_node->label == Ident) {
                const char *name = lhs_node->ident;
                Symbol *s = sym_lookup_any(name);
                if (!s) {
                    sem_error(lhs_node->lineno, "variable '%s' non déclarée", name);
                } else {
                    TpcType rhs = sem_expr(exp_node);
                    int compat = types_compatible(s->type, rhs);
                    if (compat == -1) {
                        sem_error(lhs_node->lineno,
                            "types incompatibles dans l'affectation à '%s' (%s = %s)",
                            name, type_str(s->type), type_str(rhs));
                    } else if (compat == 0) {
                        sem_warning(lhs_node->lineno,
                            "affectation de int à char pour '%s'", name);
                    }
                }
            } else if (lhs_node->label == AccesChamp) {
                TpcType lhs_type = sem_expr(lhs_node);
                TpcType rhs_type = sem_expr(exp_node);
                int compat = types_compatible(lhs_type, rhs_type);
                if (compat == -1) {
                    sem_error(lhs_node->lineno,
                        "types incompatibles dans l'affectation de champ (%s = %s)",
                        type_str(lhs_type), type_str(rhs_type));
                } else if (compat == 0) {
                    sem_warning(lhs_node->lineno,
                        "affectation de int à char dans un champ de structure");
                }
            } else {
                sem_error(lhs_node->lineno, "partie gauche d'affectation invalide");
            }
            break;
        }
        case InstrIf: {
            sem_expr(instr->firstChild);
            Node *then = instr->firstChild ? instr->firstChild->nextSibling : NULL;
            sem_instr(then);
            break;
        }
        case InstrIfElse: {
            sem_expr(instr->firstChild);
            Node *then = instr->firstChild ? instr->firstChild->nextSibling : NULL;
            Node *els  = then ? then->nextSibling : NULL;
            sem_instr(then);
            sem_instr(els);
            break;
        }
        case InstrWhile: {
            sem_expr(instr->firstChild);
            Node *body = instr->firstChild ? instr->firstChild->nextSibling : NULL;
            sem_instr(body);
            break;
        }
        case InstrCall: {
            Node *id_node   = instr->firstChild;
            Node *args_node = id_node ? id_node->nextSibling : NULL;
            const char *fname = id_node ? id_node->ident : NULL;
            if (fname) {
                Symbol *fsym = sym_lookup_func(fname);
                if (!fsym) {
                    sem_error(id_node->lineno, "fonction '%s' non déclarée", fname);
                } else {
                    check_call_args(args_node, fsym, id_node->lineno);
                }
            }
            break;
        }
        case InstrReturn: {
            Node *exp = instr->firstChild;
            TpcType ret = sem_expr(exp);
            if (current_func) {
                int compat = types_compatible(current_func->type, ret);
                if (compat == -1) {
                    sem_error(instr->lineno,
                        "type de retour incompatible dans '%s' (attendu %s, trouvé %s)",
                        current_func->name,
                        type_str(current_func->type), type_str(ret));
                } else if (compat == 0) {
                    sem_warning(instr->lineno,
                        "conversion implicite int→char dans le return de '%s'",
                        current_func->name);
                }
            }
            break;
        }
        case InstrReturnVoid: {
            if (current_func && current_func->type.kind != TY_VOID) {
                sem_error(instr->lineno,
                    "return sans valeur dans la fonction '%s' qui renvoie %s",
                    current_func->name, type_str(current_func->type));
            }
            break;
        }
        case InstrBlock: {
            sem_suite_instr(instr->firstChild);
            break;
        }
        case InstrEmpty:
            break;
        default:
            break;
    }
}

/* ---- Expressions ---- */
static TpcType sem_expr(Node *expr) {
    if (!expr) return type_int();
    switch (expr->label) {
        case Num:       return type_int();
        case Character: return type_char();
        case Ident: {
            const char *name = expr->ident;
            Symbol *s = sym_lookup_any(name);
            if (!s) {
                sem_error(expr->lineno, "variable '%s' non déclarée", name);
                return type_int();
            }
            return s->type;
        }
        case AppelFonct: {
            Node *id_node   = expr->firstChild;
            Node *args_node = id_node ? id_node->nextSibling : NULL;
            const char *fname = id_node ? id_node->ident : NULL;
            if (!fname) return type_int();
            Symbol *fsym = sym_lookup_func(fname);
            if (!fsym) {
                sem_error(expr->lineno, "fonction '%s' non déclarée", fname);
                return type_int();
            }
            if (fsym->type.kind == TY_VOID) {
                sem_error(expr->lineno,
                    "la fonction '%s' est void et ne peut pas être utilisée comme expression",
                    fname);
            }
            check_call_args(args_node, fsym, expr->lineno);
            return fsym->type;
        }
        case AccesChamp: {
            Node *base_node  = expr->firstChild;
            Node *field_node = base_node ? base_node->nextSibling : NULL;
            TpcType base_type = sem_expr(base_node);
            if (base_type.kind != TY_STRUCT) {
                sem_error(expr->lineno,
                    "l'accès par '.' requiert une structure (trouvé %s)",
                    type_str(base_type));
                return type_int();
            }
            Symbol *st = sym_lookup_struct(base_type.struct_name);
            if (!st) {
                sem_error(expr->lineno,
                    "type 'struct %s' non déclaré", base_type.struct_name);
                return type_int();
            }
            if (!field_node || !field_node->ident) {
                sem_error(expr->lineno, "nom de champ manquant");
                return type_int();
            }
            for (int i = 0; i < st->nfields; i++) {
                if (strcmp(st->fields[i].name, field_node->ident) == 0)
                    return st->fields[i].type;
            }
            sem_error(expr->lineno,
                "la structure '%s' n'a pas de champ '%s'",
                base_type.struct_name, field_node->ident);
            return type_int();
        }
        case OpOr: case OpAnd:
        case OpEq: case OpOrder:
            sem_expr(expr->firstChild);
            sem_expr(expr->firstChild ? expr->firstChild->nextSibling : NULL);
            return type_int();
        case OpAddsub: case OpDivstar:
            sem_expr(expr->firstChild);
            sem_expr(expr->firstChild ? expr->firstChild->nextSibling : NULL);
            return type_int();
        case OpUnaryMinus:
            sem_expr(expr->firstChild);
            return type_int();
        case OpNot:
            sem_expr(expr->firstChild);
            return type_int();
        default:
            return type_int();
    }
}

/* ---- Point d'entrée principal ---- */
int analyse_semantique(Node *tree) {
    if (!tree) return 0;

    /* Pré-peupler les fonctions built-in */
    sym_add_func("putchar", type_void())->nparams = 1;
    sym_add_func("putint",  type_void())->nparams = 1;
    sym_add_func("getchar", type_int()) ->nparams = 0;
    sym_add_func("getint",  type_int()) ->nparams = 0;

    Node *child = tree->firstChild;
    Node *dv = NULL, *df = NULL;
    if (child && child->label == DeclVars) {
        dv = child; df = child->nextSibling;
    } else {
        df = child;
    }

    /* 1. Variables et structs globales */
    if (dv) {
        int dummy = 0;
        sem_decl_vars(dv, 1, &dummy);
    }

    /* 2a. Signatures de toutes les fonctions (1er passage)
       Toutes les fonctions sont enregistrées avant d'analyser les corps,
       pour permettre les appels mutuels et les appels avant déclaration. */
    if (df) {
        for (Node *func = df->firstChild; func; func = func->nextSibling)
            sem_func_signature(func);
    }

    /* 2b. Corps de toutes les fonctions (2ème passage) */
    if (df) {
        for (Node *func = df->firstChild; func; func = func->nextSibling)
            sem_func_body(func);
    }

    /* 3. Vérifier que main existe et renvoie int */
    Symbol *main_sym = sym_lookup_func("main");
    if (!main_sym) {
        fprintf(stderr, "Erreur sémantique: le programme doit avoir une fonction 'main'\n");
        sem_errors++;
    } else if (main_sym->type.kind != TY_INT) {
        fprintf(stderr, "Erreur sémantique: la fonction 'main' doit renvoyer int\n");
        sem_errors++;
    }

    return sem_errors;
}
