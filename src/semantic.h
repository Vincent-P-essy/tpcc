/* semantic.h */
#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "tree.h"
#include "symtable.h"

extern int sem_errors;
extern int sem_warnings;

/* Lance l'analyse sémantique complète.
   Retourne 0 si ok, nombre d'erreurs sinon. */
int analyse_semantique(Node *tree);

#endif /* SEMANTIC_H */
