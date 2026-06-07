/* tree.h */
#ifndef TREE_H
#define TREE_H

typedef enum
{
  Prog, DeclVars, DeclFoncts, DeclFonct,
  StructDecl, ChampStructs, StructType,
  Declarateurs, EnTeteFonct, Parametres, ListTypVar,
  Corps, SuiteInstr, Instr,
  InstrAssign, InstrIf, InstrIfElse, InstrWhile,
  InstrCall, InstrReturn, InstrReturnVoid, InstrBlock, InstrEmpty,
  Exp, TB, FB, M, E, T, AppelFonct, AccesChamp,
  OpOr, OpAnd, OpEq, OpOrder, OpAddsub, OpDivstar, OpUnaryMinus, OpNot,
  Type, Ident, Num, Character,
  Arguments, ListExp
} label_t;

typedef struct Node
{
  label_t label;
  struct Node *firstChild, *nextSibling;
  int lineno;
  char *ident;
  int num;
  char *type;
} Node;

Node *makeNode(label_t label);
Node *makeNodeIdent(label_t label, const char *ident);
Node *makeNodeNum(label_t label, int num);
Node *makeNodeType(label_t label, const char *type);
void addSibling(Node *node, Node *sibling);
void addChild(Node *parent, Node *child);
void deleteTree(Node *node);
void printTree(Node *node);

#define FIRSTCHILD(node)  node->firstChild
#define SECONDCHILD(node) node->firstChild->nextSibling
#define THIRDCHILD(node)  node->firstChild->nextSibling->nextSibling

#endif /* TREE_H */
