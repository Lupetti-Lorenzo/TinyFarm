#define _GNU_SOURCE   // permette di usare estensioni GNU
#include <stdio.h>    // permette di usare scanf printf etc ...
#include <stdlib.h>   // conversioni stringa exit() etc ...
#include <stdbool.h>  // gestisce tipo bool
#include <assert.h>   // permette di usare la funzione ass
#include <string.h>   // funzioni per stringhe
#include <errno.h>    // richiesto per usare errno
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>        
#include <fcntl.h>   
#include <getopt.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <regex.h>
#include <stdint.h>

#define HOST "127.0.0.1"
#define PORT 55955
#define QUI __LINE__,  __FILE__

// CONSUMATORE 
typedef struct {
  int *cindex;  // indice nel buffer
  char **buffer; 
	int buf_size;
	pthread_mutex_t *cmutex; // mutex per sincronizzare i consumatori sulle operazioni sul buffer
  sem_t *sem_free_slots;
  sem_t *sem_data_items;
} cdati;

// SIGINT HANDLER - variabili condivise al main(e mutex) per comunicare
typedef struct {
	bool sigint; // dice al master di smettere di scrivere sul buffer
	bool fine;   // serve a dire al thread di terminare
	pthread_mutex_t *sigmutex; 
} handlerData;