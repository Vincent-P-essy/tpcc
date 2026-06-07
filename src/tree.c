/* tree.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tree.h"
extern int lineno;

static char *my_strdup(const char *s) {
  if (!s) return NULL;
  size_t len = strlen(s) + 1;
  char *copy = malloc(len);
  if (!copy) return NULL;
  memcpy(copy, s, len);
  return copy;
}

static const char *StringFromLabel[] = {
  "Prog","DeclVars","DeclFoncts","DeclFonct",
  "StructDecl","ChampStructs","StructType",
  "Declarateurs","EnTeteFonct","Parametres","ListTypVar",
  "Corps","SuiteInstr","Instr",
  "InstrAssign","InstrIf","InstrIfElse","InstrWhile",
  "InstrCall","InstrReturn","InstrReturnVoid","InstrBlock","InstrEmpty",
  "Exp","TB","FB","M","E","T","AppelFonct","AccesChamp",
  "OpOr","OpAnd","OpEq","OpOrder","OpAddsub","OpDivstar","OpUnaryMinus","OpNot",
  "Type","Ident","Num","Character",
  "Arguments","ListExp"
};

Node *makeNode(label_t label) {
  Node *node = malloc(sizeof(Node));
  if (!node) { fprintf(stderr, "Out of memory\n"); exit(3); }
  node->label = label;
  node->firstChild = node->nextSibling = NULL;
  node->lineno = lineno;
  node->ident = NULL;
  node->num = 0;
  node->type = NULL;
  return node;
}

Node *makeNodeIdent(label_t label, const char *ident) {
  Node *node = makeNode(label);
  node->ident = my_strdup(ident);
  return node;
}

Node *makeNodeNum(label_t label, int num) {
  Node *node = makeNode(label);
  node->num = num;
  return node;
}

Node *makeNodeType(label_t label, const char *type) {
  Node *node = makeNode(label);
  node->type = my_strdup(type);
  return node;
}

void addSibling(Node *node, Node *sibling) {
  if (!node || !sibling) return;
  Node *curr = node;
  while (curr->nextSibling) curr = curr->nextSibling;
  curr->nextSibling = sibling;
}

void addChild(Node *parent, Node *child) {
  if (!parent || !child) return;
  if (!parent->firstChild) parent->firstChild = child;
  else addSibling(parent->firstChild, child);
}

void deleteTree(Node *node) {
  if (!node) return;
  deleteTree(node->firstChild);
  deleteTree(node->nextSibling);
  free(node->ident);
  free(node->type);
  free(node);
}

void printTree(Node *node) {
  if (!node) return;
  static int rightmost[128];
  static int depth = 0;
  int i;
  for (i = 1; i < depth; i++)
    printf(rightmost[i] ? "    " : "\u2502   ");
  if (depth > 0)
    printf(rightmost[depth] ? "\u2514\u2500\u2500 " : "\u251c\u2500\u2500 ");
  printf("%s", StringFromLabel[node->label]);
  if (node->ident) printf(" (%s)", node->ident);
  if (node->type)  printf(" <%s>", node->type);
  if (node->label == Num) printf(" [%d]", node->num);
  printf("\n");
  depth++;
  for (Node *child = node->firstChild; child; child = child->nextSibling) {
    rightmost[depth] = child->nextSibling ? 0 : 1;
    printTree(child);
  }
  depth--;
}
