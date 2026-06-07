
CC      = gcc
CFLAGS  = -Wall -Wno-implicit-function-declaration -Iobj -Isrc
LDFLAGS = -Wall
EXEC    = tpcc
SRC     = src/
BIN     = bin/
OBJ     = obj/

HEADERS = $(SRC)tree.h $(SRC)symtable.h $(SRC)semantic.h $(SRC)codegen.h

$(BIN)$(EXEC): $(OBJ)tree.o $(OBJ)symtable.o $(OBJ)semantic.o $(OBJ)codegen.o \
               $(OBJ)$(EXEC).tab.o $(OBJ)lex.yy.o
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJ)$(EXEC).tab.c $(OBJ)$(EXEC).tab.h: $(SRC)$(EXEC).y
	bison -d -o $(OBJ)$(EXEC).tab.c $<

$(OBJ)lex.yy.c: $(SRC)$(EXEC).lex $(OBJ)$(EXEC).tab.h
	flex -o $@ $<

$(OBJ)$(EXEC).tab.o: $(OBJ)$(EXEC).tab.c $(OBJ)$(EXEC).tab.h $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

$(OBJ)lex.yy.o: $(OBJ)lex.yy.c $(OBJ)$(EXEC).tab.h $(HEADERS)
	$(CC) -o $@ -c $< $(CFLAGS)

$(OBJ)tree.o:     $(SRC)tree.c     $(SRC)tree.h
$(OBJ)symtable.o: $(SRC)symtable.c $(SRC)symtable.h $(SRC)tree.h
$(OBJ)semantic.o: $(SRC)semantic.c $(SRC)semantic.h $(SRC)symtable.h $(SRC)tree.h
$(OBJ)codegen.o:  $(SRC)codegen.c  $(SRC)codegen.h  $(SRC)symtable.h $(SRC)tree.h

$(OBJ)%.o: $(SRC)%.c
	$(CC) -o $@ -c $< $(CFLAGS)

.PHONY: clean
clean:
	rm -f $(OBJ)lex.yy.c $(OBJ)$(EXEC).tab.c $(OBJ)$(EXEC).tab.h \
	      $(BIN)$(EXEC) $(OBJ)*.o
