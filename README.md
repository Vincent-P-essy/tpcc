# tpcc — Compilateur TPC → NASM x86-64

Projet de compilation L3 — Université, S6 2025-2026  
**Auteur :** Vincent Plessy

---

## Description

`tpcc` est un compilateur complet pour le langage **TPC** (un sous-ensemble du C), qui produit de l'assembleur **NASM ELF64 AMD64** exécutable directement sous Linux sans libc.

### Fonctionnalités du langage TPC supportées

| Fonctionnalité | Support |
|---|---|
| Types `int`, `char`, `void` | ✅ |
| Variables globales et locales | ✅ |
| Structures (`struct`) avec champs | ✅ |
| Fonctions (déclaration, appel, récursion) | ✅ |
| Passages d'arguments > 6 (convention AMD64 pile) | ✅ |
| Opérateurs arithmétiques `+ - * / %` | ✅ |
| Opérateurs de comparaison `== != < <= > >=` | ✅ |
| Opérateurs logiques `&& \|\|` (court-circuit) | ✅ |
| `if`, `if/else`, `while` | ✅ |
| `return` (avec ou sans valeur) | ✅ |
| Conversion implicite `char → int` | ✅ |
| Commentaires `/* */` et `//` | ✅ |
| Fonctions I/O : `putchar`, `putint`, `getchar`, `getint` | ✅ |

---

## Architecture

```
src/
├── tpcc.lex       Analyseur lexical (Flex)
├── tpcc.y         Grammaire + point d'entrée main() (Bison)
├── tree.c/h       Arbre syntaxique abstrait (AST)
├── symtable.c/h   Tables des symboles (variables, fonctions, structs)
├── semantic.c/h   Analyse sémantique (2 passes)
└── codegen.c/h    Génération de code NASM
```

### Pipeline de compilation

```
Fichier .tpc
    │
    ▼ Flex (tpcc.lex)
Tokens
    │
    ▼ Bison (tpcc.y)
AST (arbre syntaxique abstrait)
    │
    ▼ Analyse sémantique (semantic.c)
Vérifications de types + tables de symboles
    │
    ▼ Génération de code (codegen.c)
Fichier .asm (NASM ELF64)
    │
    ▼ nasm + ld
Exécutable Linux
```

### Analyse sémantique — 2 passes

L'analyse se fait en deux passes sur les fonctions :
1. **Passe 1** — enregistrement de toutes les signatures (permet les appels mutuels et les appels avant déclaration)
2. **Passe 2** — analyse des corps de fonctions

### Génération de code — convention AMD64

- Passage des arguments : registres `rdi, rsi, rdx, rcx, r8, r9` pour les 6 premiers, pile pour les suivants
- Alignement de la pile à 16 octets garanti avant chaque `call`
- `_start` → `main` → `exit(main())` via syscall Linux (`rax=60`)
- Fonctions I/O implémentées en assembleur pur (syscalls `read`/`write`)

---

## Compilation

### Prérequis

```bash
gcc    # >= 4.8
flex   # >= 2.6
bison  # >= 3.0
nasm   # >= 2.13   (pour assembler les .asm générés)
ld     # (binutils, pour linker)
```

### Build

```bash
make
```

Le binaire est produit dans `bin/tpcc`.

```bash
make clean   # supprime les fichiers générés
```

---

## Utilisation

```bash
# Lire depuis stdin
./bin/tpcc < prog.tpc

# Compiler un fichier (produit prog.asm)
./bin/tpcc prog.tpc

# Options de debug
./bin/tpcc -t prog.tpc     # affiche l'AST
./bin/tpcc -s prog.tpc     # affiche les tables des symboles
./bin/tpcc -h              # aide
```

### Assembler et exécuter le code généré

```bash
./bin/tpcc prog.tpc
nasm -f elf64 prog.asm -o prog.o
ld -o prog prog.o
./prog
```

### Codes de retour du compilateur

| Code | Signification |
|------|--------------|
| 0 | Succès, code généré |
| 1 | Erreur lexicale ou syntaxique |
| 2 | Erreur sémantique |
| 3 | Erreur système (fichier introuvable, mémoire…) |

---

## Exemple

```c
/* hello.tpc */
int main(void) {
    int i;
    i = 1;
    while (i <= 5) {
        putint(i);
        putchar('\n');
        i = i + 1;
    }
    return 0;
}
```

```bash
./bin/tpcc hello.tpc && nasm -f elf64 hello.asm -o hello.o && ld -o hello hello.o && ./hello
```

Sortie :
```
1
2
3
4
5
```

---

## Tests

```bash
bash run_tests.sh          # teste tous les cas (retour code compilateur)
bash run_tests.sh --asm    # teste aussi l'assemblage et l'édition de liens
```

La suite de tests couvre :
- **`test/good/`** — 22 programmes corrects (retour attendu : 0)
- **`test/syn-err/`** — 8 programmes avec erreurs lexicales/syntaxiques (retour attendu : 1)
- **`test/sem-err/`** — 15 programmes avec erreurs sémantiques (retour attendu : 2)
- **`test/warn/`** — 3 programmes avec avertissements de conversion de type (retour attendu : 0)

Score actuel : **48/48 (100%)**

---

## Erreurs et avertissements détectés

### Erreurs sémantiques (arrêtent la compilation)
- Variable ou fonction non déclarée
- Double déclaration (variable ou fonction)
- Nombre d'arguments incorrect lors d'un appel
- Utilisation d'une fonction `void` dans une expression
- Accès à un champ inexistant ou via un non-struct
- Absence de fonction `main` retournant `int`
- Type de retour incompatible

### Avertissements (compilation continue)
- Conversion implicite `int → char` (perte potentielle de données)
