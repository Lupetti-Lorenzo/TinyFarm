#define _GNU_SOURCE  
#include <stdio.h>    // permette di usare scanf printf etc ...
#include <stdlib.h>   // conversioni stringa exit() etc ...
#include <stdbool.h>  // gestisce tipo bool
#include <assert.h>   // permette di usare la funzione ass
#include <string.h>   // funzioni per stringhe
#include <errno.h>    // richiesto per usare errno
#include <unistd.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <regex.h>
#include <stdint.h>

// devo pure rinominare a rota e scrivere tutto in italiano
// vedere se termina farlo stampare su stdout
// chiedere al prof anche per la regexp, i file avranno una estenzione o possono essere senza, che devo farla in modo diverso in quel caso
//send long e send name vedere come fare per valore di ritorno tipo funzioni libreria e termina ecc
// fare funzioni.c e funzioni.h per sendlong.. e mettici pure writen

#include "xerrori.h"
#define HOST "127.0.0.1"
#define PORT 55612
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

void sendLong(int fd_skt, long n) {
	long max32 = INT32_MAX; 
	long offset, len;
	if (n < max32) {
		offset = n;
		len = 0;
	}
	else {
		offset = n % max32;
		len = n / max32;
	}
	// mando prima il numeromax poi la lunghezza e poi offset
	int tmp, e;
 	tmp = htonl(max32);
	e = writen(fd_skt,&tmp,sizeof(tmp));
	if(e!=sizeof(tmp)) termina("Errore write max32");
	tmp = htonl(len);
	e = writen(fd_skt,&tmp,sizeof(tmp));
	if(e!=sizeof(tmp)) termina("Errore write len");
	tmp = htonl(offset);
	e = writen(fd_skt,&tmp,sizeof(tmp));
	if(e!=sizeof(tmp)) termina("Errore write offset");
}

// scrive sul filedesc la stringa 
void sendString(int fd_skt, char *s) {
	// mando lunghezza del nome_file
	int tmp, e;
	long len = strlen(s);
	tmp = htonl(len);
	e = writen(fd_skt,&tmp,sizeof(tmp));
	if(e!=sizeof(tmp)) termina("Errore write lunghezza nome file");

	// mando carattere per carattere il nome		
	for (int i = 0; i < len; i++) {
		char tmp_char = s[i];
		e = writen(fd_skt,&tmp_char,sizeof(tmp_char));
		if(e!=sizeof(tmp_char)) termina("Errore write carattere");
	}
}

// funzione eseguita dai thread consumatori
void *tbodyc(void *arg)
{  
  cdati *a = (cdati *)arg; 
		
  while(true) {	
		// lettura dal buffer 
    xsem_wait(a->sem_data_items,QUI);
		xpthread_mutex_lock(a->cmutex,QUI);
    char *file_name = a->buffer[*(a->cindex) % a->buf_size];
		*(a->cindex) += 1;
		xpthread_mutex_unlock(a->cmutex,QUI);
    xsem_post(a->sem_free_slots,QUI);

		if (strcmp(file_name, "") == 0) break; // ho incontrato un valore di terminazione
		
		// apro file
		FILE *f = fopen(file_name, "r");
		if(f==NULL) {
	    perror("Errore apertura file");
	    fprintf(stderr,"== %d == Linea: %d, File: %s\n",getpid(),QUI);
			continue;
	  }
		
		// leggo interi dal file binario
		int e = fseek(f, 0, SEEK_END); //sposto il puntatore in fondo
		if (e != 0) termina("Consumatore> Errore fseek");
		long t = ftell(f); //mi dice quanto e grande il file fino al puntatore
		if (t < 0) termina("Consumatore> Errore ftell");
		int n = t/sizeof(long); //grandezza array
		long *a = malloc(n*sizeof(long)); //array di long letti dal file
		if (a == NULL) termina("Consumatore> Errore malloc");
		rewind(f); //puntatore del file resettato
		//if (e != 0) termina("Padre> Errore rewind");
		e = fread(a, sizeof(long), n, f); //leggo n long dal file f e li metto in a
		
		// calcolo somma
		long sum = 0;
		for (int i = 0; i < n; i++) {
			sum += a[i]*i;
		}
		
		free(a);
		e = fclose(f);
		if (e != 0) termina("Errore chiusura file");
		
		// mando il risultato al collector
		// per prima cosa stabilisco la connessione con il server
		int fd_skt = 0;      // file descriptor associato al socket
	  struct sockaddr_in serv_addr;
	  int tmp;
	  // crea socket
	  if ((fd_skt = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
	    termina("Errore creazione socket");
	  // assegna indirizzo
	  serv_addr.sin_family = AF_INET;
	  // il numero della porta deve essere convertito 
	  // in network order 
	  serv_addr.sin_port = htons(PORT);
	  serv_addr.sin_addr.s_addr = inet_addr(HOST);
	  // apre connessione
	  if (connect(fd_skt, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) 
	    termina("Errore apertura connessione soket");

		
		// mando in ordine: somma, lunghezza nome file, nome file 
    // mando somma
	  sendLong(fd_skt, sum);
   
		
		
		// chiudo connessione
		 if(close(fd_skt)<0)
    	termina("Errore chiusura socket");
  }
  pthread_exit(NULL); 
}    



// thread che effettua la gestione di SIGINT
void *sigintHandler(void *v) {
	handlerData *data = (handlerData *)v; 
	
  sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask,SIGINT);
	int e = -1;
	while (e == -1) {
		xpthread_mutex_lock(data->sigmutex,QUI);
		if (data->fine) break;
		xpthread_mutex_unlock(data->sigmutex,QUI);
		siginfo_t s;
		struct timespec time = {1, 0};
		e = sigtimedwait(&mask,&s, &time);
		if (e != -1 && e != SIGINT) termina("Errore sigtimewait");
	}
  
	if (e == SIGINT) 
	{
		// ho ricevuto SIGINT
		xpthread_mutex_lock(data->sigmutex,QUI);
		data->sigint = true;
		xpthread_mutex_unlock(data->sigmutex,QUI);
	}
  return NULL;
}



void print_usage(char *nome_exec) {
	printf("Uso: %s file [file ...] \nOptions:\n-n nthread Numero thread worker(>0)\n-q qlen Lunghezza buffer prod/cons(>0)\n-t delay Tempo(millisec) tra scritture del master sul buffer(>=0)",nome_exec);
}




int main(int argc, char *argv[])
{
  // controlla numero argomenti
	char *name_exec = argv[0];
  if(argc<2) {
      print_usage(name_exec);
      return 1;
  }

	// blocco SIGINT 
  sigset_t mask;
	sigemptyset(&mask);
  sigaddset(&mask,SIGINT); // aggiungo sigint
  pthread_sigmask(SIG_BLOCK,&mask,NULL); 

	// faccio partire thread gestore
	pthread_t tid;
	pthread_mutex_t sigmutex = PTHREAD_MUTEX_INITIALIZER;
	handlerData data;
	data.fine = false;
	data.sigint = false;
	data.sigmutex = &sigmutex;
	xpthread_create(&tid,NULL,sigintHandler,&data,
			        QUI);


	// controllo argomenti opzionali
	int nt = 4;				 // numero thread worker
	int buf_size = 8;  // dimesione buffer produttori-consumatori
	int delay = 0;     // delay(millisecondi) scrittura sul buffer del masterworker   
	int option;
	while ((option = getopt(argc, argv, "n:q:t:")) != -1) {
		switch(option) {
			case('n') :
				nt = atof(optarg);
				if (nt<1) {
					termina("Errore -n: numero thread deve essere almeno 1");
				}
				break;
			case('q') : 
				buf_size = atof(optarg);
				if (buf_size<1) {
					termina("Errore -q: la dimensione del buffer deve essere almeno 1");
				}
				break;
			case('t') :
				delay = atof(optarg);
				if (delay<0) {
					termina("Errore -t: delay deve essere >=0");
				}
				break;
			default:
				print_usage(name_exec);
				termina("");
		}
	}

	
	// trovo il numero di nomifile passati per argomento 
	// metto in un array gli indici in argv contenenti nomifile 
	int numfiles = 0;
	int fileindexes[argc-1]; // al massimo sara grande quanto argc-1
	regex_t regex;
	int reti;
	// compila regexp 
	reti = regcomp(&regex, "^(\\w|[._-])+\\.[A-Za-z]{3}$", REG_EXTENDED);
	if (reti) termina("Could not compile regex\n");
	// la testo su ogni parametro
	for (int i = 1; i < argc; i++) {
		// esegui regexp
		reti = regexec(&regex, argv[i], 0, NULL, 0);
		if (!reti) { // match
			fileindexes[numfiles] = i;
			numfiles++;
		}
	}
	// libero memoria
	regfree(&regex);


	// faccio partire il collector
	if(xfork(QUI)==0) {
	if(execl("./collector.py", "collector", 0))
		termina("exec fallita");
	}
	sleep(1);

	
	
	// stabilisco la connessione con il collector con cui mando messaggio di terminazione
	int fd_skt = 0;      // file descriptor associato al socket
	struct sockaddr_in serv_addr;
	int tmp = 0;
	// crea socket
	if ((fd_skt = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
		termina("Errore creazione socket master");
	// assegna indirizzo
	serv_addr.sin_family = AF_INET;
	// il numero della porta deve essere convertito 
	// in network order 
	serv_addr.sin_port = htons(PORT);
	serv_addr.sin_addr.s_addr = inet_addr(HOST);
	// apre connessione
	if (connect(fd_skt, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) 
		termina("Errore apertura connessione soket master");


	// creazione buffer produttori consumatori e relativi semafori 
  char *buffer[buf_size];
  sem_t sem_free_slots, sem_data_items;
  xsem_init(&sem_free_slots,0,buf_size,QUI);
  xsem_init(&sem_data_items,0,0,QUI);

	//CREAZIONE THREAD CONSUMATORI
	// dati consumatori
  int cindex=0;
	pthread_mutex_t cmutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_t consumatori[nt];
  cdati b;
	b.buffer = buffer;
	b.buf_size = buf_size;
	b.cindex = &cindex;
	b.cmutex = &cmutex;
	b.sem_data_items = &sem_data_items;
	b.sem_free_slots = &sem_free_slots;
	// creazione consumatori
  for(int i=0;i<nt;i++) {
    xpthread_create(&consumatori[i],NULL,tbodyc,&b,
			        QUI);
  }	

	// PRODUTTORE - thread principale
	// prendo gli argomenti passati dalla linea di comando e li metto nel buffer
	int pindex = 0; // indice buffer 
	for (int i = 0; i < numfiles; i++) {
		// controllo sigint
		xpthread_mutex_lock(data.sigmutex,QUI);
		if (data.sigint) {
			// è arrivato sigint, smetto di scrivere sul buffer, lascio il resto concludere
			break;
		}
		xpthread_mutex_unlock(data.sigmutex,QUI);
		
    // scrittura sul buffer 
    xsem_wait(&sem_free_slots,QUI);
    buffer[pindex % buf_size] = argv[fileindexes[i]];
    xsem_post(&sem_data_items,QUI);
		pindex += 1;
		if (delay) sleep(delay/1000);
  }
	
	// scrivo i valori di terminazione ai worker
	for (int i = 0; i < nt; i++) {
		xsem_wait(&sem_free_slots,QUI);
    buffer[pindex % buf_size] = "";
    xsem_post(&sem_data_items,QUI);
		pindex += 1;
	}

	// aspetto terminino tutti consumatori
  for(int i=0;i<nt;i++) 
    xpthread_join(consumatori[i],NULL,QUI);

	// dico al gestore di quittare
	if (!data.sigint){
		xpthread_mutex_lock(data.sigmutex,QUI);
		data.fine = true;
		xpthread_mutex_unlock(data.sigmutex,QUI);
	}
	// faccio join del thread gestore
	xpthread_join(tid,NULL,QUI);

	// mando messaggio di terminazione al collector
	tmp = htonl(1234567);
	ssize_t e = writen(fd_skt,&tmp,sizeof(tmp));
	if(e!=sizeof(tmp)) puts("Errore write valore di terminazione");
	
	// chiudo connessione con il socket
	if(close(fd_skt)<0)
		termina("Errore chiusura socket master");
	
	return 0;
}