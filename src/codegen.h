/* codegen.h */
#ifndef CODEGEN_H
#define CODEGEN_H

#include "tree.h"
#include "symtable.h"
#include <stdio.h>

/* Génère le code NASM dans le fichier de sortie.
   Retourne 0 si succès. */
int generate_code(Node *tree, FILE *out);

#endif /* CODEGEN_H */
