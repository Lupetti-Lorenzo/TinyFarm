# definizione del compilatore e dei flag di compilazione
# che vengono usate dalle regole implicite
CC=gcc
CFLAGS=-g -Wall -O -std=c99
LDLIBS=-lm -lrt -pthread

# singolo eseguilbile da compilare
MAIN=farm

# se si scrive solo make di default compila main 
all: $(MAIN)

$(MAIN): $(MAIN).o xerrori.o

# target che cancella eseguibili e file oggetto
clean:
	rm -f $(MAIN) *.o  

# target che crea l'archivio dei sorgenti
zip:
	zip $(MAIN).zip makefile *.c *.h *.py *.md
