#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *out;
static int  label_count   = 0;
static int  in_main       = 0;  /* 1 si on génère main */
static int  stack_size    = 0;  /* taille allouée sur la pile pour la fonction courante */
static int  rsp_save_slot = 0;  /* offset depuis rbp du slot de sauvegarde de rsp (= stack_size + 8) */

static const char *argregs32[] = {"edi","esi","edx","ecx","r8d","r9d"};
static const char *argregs64[] = {"rdi","rsi","rdx","rcx","r8","r9"};

static void cg_prog(Node *prog);
static void cg_global_decl_vars(void);
static void cg_func(Node *func);
static void cg_suite_instr(Node *suite);
static void cg_instr(Node *instr);
static void cg_expr(Node *expr);         /* résultat dans eax */
static void cg_emit_call(Node *id_node, Node *args_node);
static void cg_builtin_functions(void);
static TpcType cg_infer_type(Node *expr);
static void cg_lvalue_addr(Node *lv);    /* adresse dans rax */
static void emit_push(const char *reg);
static void emit_pop(const char *reg);

/* ----- Utilitaires ----- */
static int new_label(void) { return label_count++; }

static void emit_push(const char *reg) {
    fprintf(out, "    push %s\n", reg);
}

static void emit_pop(const char *reg) {
    fprintf(out, "    pop %s\n", reg);
}

static void emit_call_align_save(void) {
    fprintf(out, "    mov [rbp - %d], rsp\n", rsp_save_slot);
    fprintf(out, "    and rsp, -16\n");
}
static void emit_call_align_restore(void) {
    fprintf(out, "    mov rsp, [rbp - %d]\n", rsp_save_slot);
}

/* Reconstruit la table locale pour une fonction à partir de l'AST */
static int build_local_table(Node *header, Node *corps) {
    sym_clear_locals();
    int next_offset = 0;

    /* Paramètres */
    Node *params_node = NULL;
    if (header) {
        Node *n = header->firstChild; /* type retour */
        if (n) n = n->nextSibling;    /* nom */
        if (n) params_node = n->nextSibling; /* Parametres */
    }
    if (params_node && params_node->label == Parametres) {
        Node *ltv = params_node->firstChild;
        if (ltv) {
            Node *child = ltv->firstChild;
            while (child) {
                if (child->label == Type || child->label == StructType) {
                    TpcType pt;
                    if (child->label == Type) {
                        if (strcmp(child->type,"int")==0) pt = type_int();
                        else pt = type_char();
                    } else {
                        Node *id = child->firstChild;
                        pt = type_struct(id ? id->ident : "");
                    }
                    Node *pname = child->nextSibling;
                    if (pname) {
                        sym_add_param(pname->ident, pt, &next_offset);
                        child = pname->nextSibling;
                    } else child = child->nextSibling;
                } else child = child->nextSibling;
            }
        }
    }

    /* Variables locales du corps */
    if (corps) {
        Node *child = corps->firstChild;
        Node *dv = NULL;
        if (child && child->label == DeclVars) dv = child;
        if (dv) {
            Node *c = dv->firstChild;
            while (c) {
                if (c->label == Type || c->label == StructType) {
                    TpcType t;
                    if (c->label == Type) {
                        if (strcmp(c->type,"int")==0) t = type_int();
                        else t = type_char();
                    } else {
                        Node *id = c->firstChild;
                        t = type_struct(id ? id->ident : "");
                    }
                    Node *decl = c->nextSibling;
                    if (decl && decl->label == Declarateurs) {
                        for (Node *id = decl->firstChild; id; id = id->nextSibling)
                            sym_add_lvar(id->ident, t, &next_offset);
                        c = decl->nextSibling;
                    } else c = c->nextSibling;
                } else c = c->nextSibling;
            }
        }
    }

    int total = -next_offset;
    if (total < 0) total = 0;
    return total;
}

static Symbol *resolve(const char *name) {
    Symbol *s = sym_lookup_lvar(name);
    if (!s) s = sym_lookup_gvar(name);
    return s;
}

static void load_sym(Symbol *s) {
    if (!s) { fprintf(out, "    xor eax, eax\n"); return; }
    if (s->kind == SYM_GVAR) {
        if (s->type.kind == TY_CHAR)
            fprintf(out, "    movzx eax, byte [_%s]\n", s->name);
        else
            fprintf(out, "    mov eax, dword [_%s]\n", s->name);
    } else {
        if (s->type.kind == TY_CHAR)
            fprintf(out, "    movzx eax, byte [rbp%d]\n", s->offset);
        else
            fprintf(out, "    mov eax, dword [rbp%d]\n", s->offset);
    }
}

/* Émettre le stockage de eax vers un symbole */
static void store_sym(Symbol *s) {
    if (!s) return;
    if (s->kind == SYM_GVAR) {
        if (s->type.kind == TY_CHAR)
            fprintf(out, "    mov byte [_%s], al\n", s->name);
        else
            fprintf(out, "    mov dword [_%s], eax\n", s->name);
    } else {
        if (s->type.kind == TY_CHAR)
            fprintf(out, "    mov byte [rbp%d], al\n", s->offset);
        else
            fprintf(out, "    mov dword [rbp%d], eax\n", s->offset);
    }
}

/* Convertir un caractère littéral ('a', '\n', etc.) en valeur ASCII */
static int char_lit_val(const char *s) {
    /* s est de la forme 'x' ou '\n' etc. */
    if (!s || s[0] != '\'') return 0;
    if (s[1] == '\\') {
        switch (s[2]) {
            case 'n': return 10;
            case 't': return 9;
            case 'r': return 13;
            case '\'': return '\'';
            case '\\': return '\\';
            default:  return s[2];
        }
    }
    return (unsigned char)s[1];
}

/* ===== INFÉRENCE DE TYPE (pour accès champs) ===== */
static TpcType cg_infer_type(Node *expr) {
    if (!expr) return type_int();
    switch (expr->label) {
    case Num:       return type_int();
    case Character: return type_char();
    case Ident: {
        Symbol *s = resolve(expr->ident ? expr->ident : "");
        return s ? s->type : type_int();
    }
    case AccesChamp: {
        TpcType base = cg_infer_type(expr->firstChild);
        if (base.kind != TY_STRUCT) return type_int();
        Symbol *st = sym_lookup_struct(base.struct_name);
        Node *fn = expr->firstChild ? expr->firstChild->nextSibling : NULL;
        if (!st || !fn) return type_int();
        for (int i = 0; i < st->nfields; i++)
            if (strcmp(st->fields[i].name, fn->ident ? fn->ident : "") == 0)
                return st->fields[i].type;
        return type_int();
    }
    case AppelFonct: {
        Node *id = expr->firstChild;
        if (!id) return type_int();
        Symbol *fs = sym_lookup_func(id->ident ? id->ident : "");
        return fs ? fs->type : type_int();
    }
    default: return type_int();
    }
}

/* ===== ADRESSE D'UN LVALUE dans rax ===== */
static void cg_lvalue_addr(Node *lv) {
    if (!lv) { fprintf(out, "    xor rax, rax\n"); return; }
    if (lv->label == Ident) {
        Symbol *s = resolve(lv->ident ? lv->ident : "");
        if (!s) { fprintf(out, "    xor rax, rax\n"); return; }
        if (s->kind == SYM_GVAR)
            fprintf(out, "    lea rax, [_%s]\n", s->name);
        else
            fprintf(out, "    lea rax, [rbp%+d]\n", s->offset);
    } else if (lv->label == AccesChamp) {
        Node *base_node  = lv->firstChild;
        Node *field_node = base_node ? base_node->nextSibling : NULL;
        if (!field_node) { fprintf(out, "    xor rax, rax\n"); return; }
        TpcType base_type = cg_infer_type(base_node);
        cg_lvalue_addr(base_node);
        /* rax = adresse de la base */
        if (base_type.kind == TY_STRUCT) {
            Symbol *st = sym_lookup_struct(base_type.struct_name);
            if (st && field_node->ident) {
                for (int i = 0; i < st->nfields; i++) {
                    if (strcmp(st->fields[i].name, field_node->ident) == 0) {
                        if (st->fields[i].offset != 0)
                            fprintf(out, "    add rax, %d\n",
                                    st->fields[i].offset);
                        break;
                    }
                }
            }
        }
    } else {
        /* Cas général : évaluer l'expression (p.ex. appel retournant struct ptr) */
        cg_expr(lv);
        fprintf(out, "    cdqe\n"); /* étendre eax→rax */
    }
}

/* ===== PROGRAMME ===== */
static void cg_prog(Node *prog) {
    Node *child = prog->firstChild;
    Node *df = NULL;
    if (child && child->label == DeclVars) df = child->nextSibling;
    else df = child;

    /* section .bss pour les globales (parcourt la table des symboles) */
    if (g_vars) {
        fprintf(out, "section .bss\n");
        cg_global_decl_vars();
        fprintf(out, "\n");
    }

    fprintf(out, "section .text\n");
    fprintf(out, "global _start\n\n");

    /* point d'entrée : _start appelle main puis exit */
    fprintf(out, "_start:\n");
    fprintf(out, "    call main\n");
    fprintf(out, "    mov rdi, rax\n");
    fprintf(out, "    mov rax, 60\n");
    fprintf(out, "    syscall\n\n");

    /* fonctions utilisateur */
    if (df) {
        for (Node *func = df->firstChild; func; func = func->nextSibling)
            cg_func(func);
    }

    /* fonctions built-in */
    cg_builtin_functions();
}

/* ===== VARIABLES GLOBALES (.bss) ===== */
static void cg_global_decl_vars(void) {
    for (Symbol *s = g_vars; s; s = s->next) {
        if (s->type.kind == TY_CHAR)
            fprintf(out, "    _%s: resb 1\n", s->name);
        else if (s->type.kind == TY_INT)
            fprintf(out, "    _%s: resd 1\n", s->name);
        else { /* struct */
            Symbol *st = sym_lookup_struct(s->type.struct_name);
            int sz = st ? struct_compute_size(st) : 8;
            if (sz < 1) sz = 8;
            fprintf(out, "    _%s: resb %d\n", s->name, sz);
        }
    }
}

/* ===== FONCTION ===== */
static void cg_func(Node *func) {
    Node *header = func->firstChild;
    Node *corps  = header ? header->nextSibling : NULL;
    if (!header || !corps) return;

    Node *ret_node  = header->firstChild;
    Node *name_node = ret_node ? ret_node->nextSibling : NULL;
    if (!name_node) return;

    const char *fname = name_node->ident;
    in_main = (strcmp(fname, "main") == 0);

    /* Reconstruire la table locale */
    stack_size = build_local_table(header, corps);
    /* Slot de sauvegarde de rsp + garantir rsp ≡ 0 (mod 16) après le prologue.
       rbp ≡ 0 (mod 16), donc rsp_save_slot doit être ≡ 0 (mod 16). */
    rsp_save_slot = stack_size + 8;
    if (rsp_save_slot % 16 != 0)
        rsp_save_slot += 16 - (rsp_save_slot % 16);

    /* Prologue */
    fprintf(out, "%s:\n", fname);
    fprintf(out, "    push rbp\n");
    fprintf(out, "    mov rbp, rsp\n");
    fprintf(out, "    sub rsp, %d\n", rsp_save_slot);

    /* Sauvegarder les paramètres.
       l_vars est en ordre inverse (insertion en tête) : on collecte tous les params,
       puis on les mappe dans l'ordre de déclaration :
         - params 1..6  ← registres edi/esi/edx/ecx/r8d/r9d
         - params 7+    ← pile appelante à [rbp+16], [rbp+24], ... */
    {
        Symbol *all_params[32];
        int nall = 0;
        for (Symbol *s = l_vars; s; s = s->next)
            if (s->kind == SYM_PARAM)
                all_params[nall++] = s;
        /* all_params[0] = dernier déclaré, all_params[nall-1] = premier déclaré */
        int nreg = nall < 6 ? nall : 6;
        for (int i = 0; i < nreg; i++) {
            Symbol *s = all_params[nall - 1 - i];
            if (s->type.kind == TY_CHAR)
                fprintf(out, "    mov byte [rbp%d], %sl\n", s->offset, argregs32[i]);
            else
                fprintf(out, "    mov dword [rbp%d], %s\n", s->offset, argregs32[i]);
        }
        for (int i = nreg; i < nall; i++) {
            Symbol *s = all_params[nall - 1 - i];
            int stack_off = 16 + (i - 6) * 8;
            if (s->type.kind == TY_CHAR) {
                fprintf(out, "    movzx eax, byte [rbp+%d]\n", stack_off);
                fprintf(out, "    mov byte [rbp%d], al\n", s->offset);
            } else {
                fprintf(out, "    mov eax, dword [rbp+%d]\n", stack_off);
                fprintf(out, "    mov dword [rbp%d], eax\n", s->offset);
            }
        }
    }

    /* Corps */
    Node *body_child = corps->firstChild;
    Node *suite = NULL;
    if (body_child) {
        if (body_child->label == DeclVars) suite = body_child->nextSibling;
        else suite = body_child;
    }
    cg_suite_instr(suite);

    /* Épilogue par défaut (cas void ou main sans return explicite) */
    if (in_main) {
        fprintf(out, "    xor eax, eax\n");
        fprintf(out, "    leave\n");
        fprintf(out, "    ret\n\n");
    } else {
        fprintf(out, "    xor eax, eax\n");
        fprintf(out, "    leave\n");
        fprintf(out, "    ret\n\n");
    }
}

/* ===== INSTRUCTIONS ===== */
static void cg_suite_instr(Node *suite) {
    if (!suite) return;
    for (Node *instr = suite->firstChild; instr; instr = instr->nextSibling)
        cg_instr(instr);
}

static void cg_instr(Node *instr) {
    if (!instr) return;
    switch (instr->label) {

    case InstrAssign: {
        Node *lhs_node = instr->firstChild;
        Node *exp_node = lhs_node ? lhs_node->nextSibling : NULL;
        if (!lhs_node) break;

        if (lhs_node->label == Ident) {
            /* Affectation simple */
            cg_expr(exp_node);
            Symbol *s = resolve(lhs_node->ident ? lhs_node->ident : "");
            store_sym(s);
        } else if (lhs_node->label == AccesChamp) {
            /* Affectation à un champ: évaluer valeur, puis calculer adresse */
            cg_expr(exp_node);
            emit_push("rax");
            cg_lvalue_addr(lhs_node);
            emit_pop("rbx");
            /* Déterminer la taille du champ */
            TpcType ft = cg_infer_type(lhs_node);
            if (ft.kind == TY_CHAR)
                fprintf(out, "    mov byte [rax], bl\n");
            else
                fprintf(out, "    mov dword [rax], ebx\n");
        }
        break;
    }

    case InstrIf: {
        int lbl = new_label();
        Node *cond = instr->firstChild;
        Node *then = cond ? cond->nextSibling : NULL;
        cg_expr(cond);
        fprintf(out, "    test eax, eax\n");
        fprintf(out, "    jz .L%d_end\n", lbl);
        cg_instr(then);
        fprintf(out, ".L%d_end:\n", lbl);
        break;
    }

    case InstrIfElse: {
        int lbl = new_label();
        Node *cond = instr->firstChild;
        Node *then = cond ? cond->nextSibling : NULL;
        Node *els  = then ? then->nextSibling : NULL;
        cg_expr(cond);
        fprintf(out, "    test eax, eax\n");
        fprintf(out, "    jz .L%d_else\n", lbl);
        cg_instr(then);
        fprintf(out, "    jmp .L%d_end\n", lbl);
        fprintf(out, ".L%d_else:\n", lbl);
        cg_instr(els);
        fprintf(out, ".L%d_end:\n", lbl);
        break;
    }

    case InstrWhile: {
        int lbl = new_label();
        Node *cond = instr->firstChild;
        Node *body = cond ? cond->nextSibling : NULL;
        fprintf(out, ".L%d_start:\n", lbl);
        cg_expr(cond);
        fprintf(out, "    test eax, eax\n");
        fprintf(out, "    jz .L%d_end\n", lbl);
        cg_instr(body);
        fprintf(out, "    jmp .L%d_start\n", lbl);
        fprintf(out, ".L%d_end:\n", lbl);
        break;
    }

    case InstrCall: {
        Node *id_node   = instr->firstChild;
        Node *args_node = id_node ? id_node->nextSibling : NULL;
        cg_emit_call(id_node, args_node);
        break;
    }

    case InstrReturn: {
        Node *exp = instr->firstChild;
        cg_expr(exp);
        fprintf(out, "    leave\n");
        fprintf(out, "    ret\n");
        break;
    }

    case InstrReturnVoid: {
        fprintf(out, "    leave\n");
        fprintf(out, "    ret\n");
        break;
    }

    case InstrBlock:
        cg_suite_instr(instr->firstChild);
        break;

    case InstrEmpty:
        break;

    default:
        break;
    }
}

/* ===== EXPRESSIONS (résultat dans eax) ===== */
static void cg_expr(Node *expr) {
    if (!expr) { fprintf(out, "    xor eax, eax\n"); return; }
    switch (expr->label) {

    case Num:
        fprintf(out, "    mov eax, %d\n", expr->num);
        break;

    case Character: {
        int val = char_lit_val(expr->ident ? expr->ident : "");
        fprintf(out, "    mov eax, %d\n", val);
        break;
    }

    case Ident: {
        Symbol *s = resolve(expr->ident);
        load_sym(s);
        break;
    }

    case AppelFonct: {
        Node *id_node   = expr->firstChild;
        Node *args_node = id_node ? id_node->nextSibling : NULL;
        cg_emit_call(id_node, args_node);
        /* résultat dans eax/rax */
        break;
    }

    case AccesChamp: {
        /* Lecture d'un champ: adresse dans rax, puis chargement */
        cg_lvalue_addr(expr);
        TpcType ft = cg_infer_type(expr);
        if (ft.kind == TY_CHAR)
            fprintf(out, "    movzx eax, byte [rax]\n");
        else
            fprintf(out, "    mov eax, dword [rax]\n");
        break;
    }

    case OpAddsub: {
        Node *left  = expr->firstChild;
        Node *right = left ? left->nextSibling : NULL;
        cg_expr(left);
        emit_push("rax");
        cg_expr(right);
        fprintf(out, "    mov ebx, eax\n");
        emit_pop("rax");
        if (expr->type && expr->type[0] == '+')
            fprintf(out, "    add eax, ebx\n");
        else
            fprintf(out, "    sub eax, ebx\n");
        break;
    }

    case OpDivstar: {
        Node *left  = expr->firstChild;
        Node *right = left ? left->nextSibling : NULL;
        cg_expr(left);
        emit_push("rax");
        cg_expr(right);
        fprintf(out, "    mov ecx, eax\n");  /* diviseur/opérande droit */
        emit_pop("rax");
        if (expr->type && expr->type[0] == '*') {
            fprintf(out, "    imul eax, ecx\n");
        } else {
            /* division ou modulo */
            fprintf(out, "    cdq\n");          /* sign-extend eax→edx:eax */
            fprintf(out, "    idiv ecx\n");     /* eax=quotient, edx=reste */
            if (expr->type && expr->type[0] == '%')
                fprintf(out, "    mov eax, edx\n");
        }
        break;
    }

    case OpEq: {
        Node *left  = expr->firstChild;
        Node *right = left ? left->nextSibling : NULL;
        int lbl = new_label();
        cg_expr(left);
        emit_push("rax");
        cg_expr(right);
        emit_pop("rbx");
        fprintf(out, "    cmp ebx, eax\n");
        if (expr->type && strcmp(expr->type,"==")==0) {
            fprintf(out, "    je .L%d_true\n", lbl);
        } else {
            fprintf(out, "    jne .L%d_true\n", lbl);
        }
        fprintf(out, "    xor eax, eax\n");
        fprintf(out, "    jmp .L%d_end\n", lbl);
        fprintf(out, ".L%d_true:\n", lbl);
        fprintf(out, "    mov eax, 1\n");
        fprintf(out, ".L%d_end:\n", lbl);
        break;
    }

    case OpOrder: {
        Node *left  = expr->firstChild;
        Node *right = left ? left->nextSibling : NULL;
        int lbl = new_label();
        cg_expr(left);
        emit_push("rax");
        cg_expr(right);
        emit_pop("rbx");
        fprintf(out, "    cmp ebx, eax\n");
        const char *jmp = "jl";
        if (expr->type) {
            if      (strcmp(expr->type,"<" )==0) jmp = "jl";
            else if (strcmp(expr->type,"<=") ==0) jmp = "jle";
            else if (strcmp(expr->type,">" )==0) jmp = "jg";
            else if (strcmp(expr->type,">=") ==0) jmp = "jge";
        }
        fprintf(out, "    %s .L%d_true\n", jmp, lbl);
        fprintf(out, "    xor eax, eax\n");
        fprintf(out, "    jmp .L%d_end\n", lbl);
        fprintf(out, ".L%d_true:\n", lbl);
        fprintf(out, "    mov eax, 1\n");
        fprintf(out, ".L%d_end:\n", lbl);
        break;
    }

    case OpAnd: {
        Node *left  = expr->firstChild;
        Node *right = left ? left->nextSibling : NULL;
        int lbl = new_label();
        cg_expr(left);
        fprintf(out, "    test eax, eax\n");
        fprintf(out, "    jz .L%d_false\n", lbl);
        cg_expr(right);
        fprintf(out, "    test eax, eax\n");
        fprintf(out, "    jz .L%d_false\n", lbl);
        fprintf(out, "    mov eax, 1\n");
        fprintf(out, "    jmp .L%d_end\n", lbl);
        fprintf(out, ".L%d_false:\n", lbl);
        fprintf(out, "    xor eax, eax\n");
        fprintf(out, ".L%d_end:\n", lbl);
        break;
    }

    case OpOr: {
        Node *left  = expr->firstChild;
        Node *right = left ? left->nextSibling : NULL;
        int lbl = new_label();
        cg_expr(left);
        fprintf(out, "    test eax, eax\n");
        fprintf(out, "    jnz .L%d_true\n", lbl);
        cg_expr(right);
        fprintf(out, "    test eax, eax\n");
        fprintf(out, "    jnz .L%d_true\n", lbl);
        fprintf(out, "    xor eax, eax\n");
        fprintf(out, "    jmp .L%d_end\n", lbl);
        fprintf(out, ".L%d_true:\n", lbl);
        fprintf(out, "    mov eax, 1\n");
        fprintf(out, ".L%d_end:\n", lbl);
        break;
    }

    case OpNot: {
        int lbl = new_label();
        cg_expr(expr->firstChild);
        fprintf(out, "    test eax, eax\n");
        fprintf(out, "    jnz .L%d_false\n", lbl);
        fprintf(out, "    mov eax, 1\n");
        fprintf(out, "    jmp .L%d_end\n", lbl);
        fprintf(out, ".L%d_false:\n", lbl);
        fprintf(out, "    xor eax, eax\n");
        fprintf(out, ".L%d_end:\n", lbl);
        break;
    }

    case OpUnaryMinus: {
        cg_expr(expr->firstChild);
        if (expr->type && expr->type[0] == '-')
            fprintf(out, "    neg eax\n");
        break;
    }

    default:
        fprintf(out, "    xor eax, eax\n");
        break;
    }
}

/* ===== HELPER APPEL DE FONCTION (convention AMD64) =====
   Gère ≤6 args (registres) et >6 args (7e arg et plus sur la pile). */
static void cg_emit_call(Node *id_node, Node *args_node) {
    Node *arglist[32];
    int nargs = 0;
    if (args_node && args_node->label == Arguments) {
        Node *lexp = args_node->firstChild;
        if (lexp)
            for (Node *e = lexp->firstChild; e && nargs < 32; e = e->nextSibling)
                arglist[nargs++] = e;
    }
    int nreg   = nargs < 6 ? nargs : 6;
    int nstack = nargs > 6 ? nargs - 6 : 0;

    if (nstack > 0) {
        /* Padding de 8 octets si nstack est impair : maintient rsp ≡ 0 (mod 16) avant call. */
        int padding = (nstack % 2 != 0) ? 8 : 0;
        if (padding)
            fprintf(out, "    sub rsp, 8\n");
        /* Pousser les args de pile de droite à gauche → arg[6] finit en haut = [rbp+16]. */
        for (int i = nargs - 1; i >= nreg; i--) {
            cg_expr(arglist[i]);
            emit_push("rax");
        }
        /* Args 1..6 : temp push puis pop dans les registres. */
        for (int i = 0; i < nreg; i++) {
            cg_expr(arglist[i]);
            emit_push("rax");
        }
        for (int i = nreg - 1; i >= 0; i--)
            emit_pop(argregs64[i]);
        emit_call_align_save();
        fprintf(out, "    call %s\n", id_node ? id_node->ident : "");
        emit_call_align_restore();
        fprintf(out, "    add rsp, %d\n", nstack * 8 + padding);
    } else {
        for (int i = 0; i < nreg; i++) {
            cg_expr(arglist[i]);
            emit_push("rax");
        }
        for (int i = nreg - 1; i >= 0; i--)
            emit_pop(argregs64[i]);
        emit_call_align_save();
        fprintf(out, "    call %s\n", id_node ? id_node->ident : "");
        emit_call_align_restore();
    }
}

/* ===== FONCTIONS BUILT-IN ===== */
static void cg_builtin_functions(void) {
    /* putchar(c) : c dans edi */
    fprintf(out,
        "; --- putchar(c) ---\n"
        "putchar:\n"
        "    push rbp\n"
        "    mov rbp, rsp\n"
        "    sub rsp, 16\n"
        "    mov [rsp], dil\n"
        "    mov rax, 1\n"
        "    mov rdi, 1\n"
        "    mov rsi, rsp\n"
        "    mov rdx, 1\n"
        "    syscall\n"
        "    leave\n"
        "    ret\n\n"
    );

    /* putint(n) : n dans edi, affiche en décimal suivi d'un newline */
    fprintf(out,
        "; --- putint(n) ---\n"
        "putint:\n"
        "    push rbp\n"
        "    mov rbp, rsp\n"
        "    sub rsp, 32\n"
        "    mov eax, edi\n"
        "    ; gérer les négatifs\n"
        "    test eax, eax\n"
        "    jns .putint_pos\n"
        "    neg eax\n"
        "    push rax\n"
        "    mov byte [rsp-1], '-'\n"
        "    mov rax, 1\n"
        "    mov rdi, 1\n"
        "    lea rsi, [rsp-1]\n"
        "    mov rdx, 1\n"
        "    syscall\n"
        "    pop rax\n"
        ".putint_pos:\n"
        "    ; convertir en décimal (digits à l'envers dans le buffer)\n"
        "    lea rdi, [rbp-16]\n"  /* fin du buffer */
        "    mov byte [rdi], 10\n"  /* newline */
        "    dec rdi\n"
        "    xor ecx, ecx\n"
        "    mov ebx, 10\n"
        ".putint_loop:\n"
        "    xor edx, edx\n"
        "    div ebx\n"
        "    add edx, '0'\n"
        "    mov [rdi], dl\n"
        "    dec rdi\n"
        "    inc ecx\n"
        "    test eax, eax\n"
        "    jnz .putint_loop\n"
        "    inc rdi\n"  /* rdi pointe sur le premier chiffre */
        "    inc ecx\n"  /* inclure le newline */
        "    mov rsi, rdi\n"
        "    mov rdx, rcx\n"
        "    mov rdi, 1\n"
        "    mov rax, 1\n"
        "    syscall\n"
        "    leave\n"
        "    ret\n\n"
    );

    /* getchar() → eax */
    fprintf(out,
        "; --- getchar() ---\n"
        "getchar:\n"
        "    push rbp\n"
        "    mov rbp, rsp\n"
        "    sub rsp, 16\n"
        "    mov rax, 0\n"
        "    mov rdi, 0\n"
        "    lea rsi, [rbp-1]\n"
        "    mov rdx, 1\n"
        "    syscall\n"
        "    movzx eax, byte [rbp-1]\n"
        "    leave\n"
        "    ret\n\n"
    );

    /* getint() → eax, lit entier décimal suivi de newline */
    fprintf(out,
        "; --- getint() ---\n"
        "getint:\n"
        "    push rbp\n"
        "    mov rbp, rsp\n"
        "    sub rsp, 32\n"
        "    xor r8d, r8d\n"    /* résultat */
        "    mov r9d, 1\n"      /* signe (+1 ou -1) */
        "    xor r10d, r10d\n"  /* nb de chiffres lus */
        "; lire premier char pour le signe éventuel\n"
        ".getint_first:\n"
        "    mov rax, 0\n"
        "    mov rdi, 0\n"
        "    lea rsi, [rbp-1]\n"
        "    mov rdx, 1\n"
        "    syscall\n"
        "    movzx ebx, byte [rbp-1]\n"
        "    cmp ebx, '-'\n"
        "    jne .getint_chkplus\n"
        "    mov r9d, -1\n"
        "    jmp .getint_loop\n"
        ".getint_chkplus:\n"
        "    cmp ebx, '+'\n"
        "    je .getint_loop\n"
        "    jmp .getint_digit\n"
        ".getint_loop:\n"
        "    mov rax, 0\n"
        "    mov rdi, 0\n"
        "    lea rsi, [rbp-1]\n"
        "    mov rdx, 1\n"
        "    syscall\n"
        "    movzx ebx, byte [rbp-1]\n"
        ".getint_digit:\n"
        "    cmp ebx, 10\n"   /* newline */
        "    je .getint_done\n"
        "    cmp ebx, '0'\n"
        "    jl .getint_err\n"
        "    cmp ebx, '9'\n"
        "    jg .getint_err\n"
        "    sub ebx, '0'\n"
        "    imul r8d, r8d, 10\n"
        "    add r8d, ebx\n"
        "    inc r10d\n"
        "    jmp .getint_loop\n"
        ".getint_done:\n"
        "    test r10d, r10d\n"
        "    jz .getint_err\n"
        "    imul r8d, r9d\n"
        "    mov eax, r8d\n"
        "    leave\n"
        "    ret\n"
        ".getint_err:\n"
        "    mov rax, 60\n"
        "    mov rdi, 5\n"
        "    syscall\n\n"
    );
}

/* ===== POINT D'ENTRÉE ===== */
int generate_code(Node *tree, FILE *output) {
    out = output;
    label_count = 0;
    fprintf(out, "; Programme TPC compilé par tpcc\n");
    fprintf(out, "; nasm -f elf64 prog.asm -o prog.o && ld -o prog prog.o\n\n");
    cg_prog(tree);
    return 0;
}
