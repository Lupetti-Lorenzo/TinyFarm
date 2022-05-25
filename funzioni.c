#include "xerrori.h"

/* Read "n" bytes from a descriptor */
ssize_t readn(int fd, void *ptr, size_t n) {  
   size_t   nleft;
   ssize_t  nread;
 
   nleft = n;
   while (nleft > 0) {
     if((nread = read(fd, ptr, nleft)) < 0) {
        if (nleft == n) return -1; /* error, return -1 */
        else break; /* error, return amount read so far */
     } else if (nread == 0) break; /* EOF */
     nleft -= nread;
     ptr   += nread;
   }
   return(n - nleft); /* return >= 0 */
}

/* Write "n" bytes to a descriptor */
ssize_t writen(int fd, void *ptr, size_t n) {  
   size_t   nleft;
   ssize_t  nwritten;
 
   nleft = n;
   while (nleft > 0) {
     if((nwritten = write(fd, ptr, nleft)) < 0) {
        if (nleft == n) return -1; /* error, return -1 */
        else break; /* error, return amount written so far */
     } else if (nwritten == 0) break; 
     nleft -= nwritten;
     ptr   += nwritten;
   }
   return(n - nleft); /* return >= 0 */
}


// manda un numero(anche a 64bit) al fd, il problema è che tramite socket posso mandare solo numeri a 32 bit massimo, quindi ho trovato questo modo di rappresentarlo in numeri da 32 bit.
// il numero lo spezzo in 3 parti: (Max32bit*quantita)+offset
// max32bit è il numero massimo rapp. in macchina con 32 bit.
// quantita(o len) è n%max32, quante volte ci sta nel numero
// offset è il numero(sicuramente a 32bit) che rimane da aggiungere al totale
// se il numero è a 32 bit, lun=0 e n=offset quindi (max32*0)+n = n, ma mando lo stesso con lo stesso "protocollo"
void sendLong(int fd, long n) {
	long max32 = INT32_MAX; 
	long offset, len;
	if (n < max32) { // numero a 32 bit
		offset = n;
		len = 0;
	}
	else { // numero > max32
		offset = n % max32;
		len = n / max32;
	}
	// mando prima il numeromax poi la lunghezza e poi offset
	int tmp, e;
 	tmp = htonl(max32);
	e = writen(fd,&tmp,sizeof(tmp));
	if(e!=sizeof(tmp)) xtermina("Errore write max32", QUI);
	tmp = htonl(len);
	e = writen(fd,&tmp,sizeof(tmp));
	if(e!=sizeof(tmp)) xtermina("Errore write len", QUI);
	tmp = htonl(offset);
	e = writen(fd,&tmp,sizeof(tmp));
	if(e!=sizeof(tmp)) xtermina("Errore write offset", QUI);
}

// scrive sul filedesc la stringa - prima manda lunghezza del nome(32 bit) e poi il nome carattere per carattere
void sendString(int fd, char *s) {
	// mando lunghezza della stringa
	int tmp, e;
	long len = strlen(s);
	tmp = htonl(len);
	e = writen(fd,&tmp,sizeof(tmp));
	if(e!=sizeof(tmp)) xtermina("Errore write lunghezza nome file", QUI);

	// mando carattere per carattere		
	for (int i = 0; i < len; i++) {
		char tmp_char = s[i];
		e = writen(fd,&tmp_char,sizeof(tmp_char));
		if(e!=sizeof(tmp_char)) xtermina("Errore write carattere", QUI);
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
    char *nome_file = a->buffer[*(a->cindex) % a->buf_size];
		*(a->cindex) += 1;
		xpthread_mutex_unlock(a->cmutex,QUI);
    xsem_post(a->sem_free_slots,QUI);

		if (strcmp(nome_file, "") == 0) break; // ho incontrato un valore di terminazione
		
		// apro file
		FILE *f = fopen(nome_file, "r"); // non uso xfopen per non far terminare il thread in caso di errore, me ne accorgo stampo il messaggio e provo con il prossimo nomefile
		if(f==NULL) {
	    fprintf(stderr,"== %d == %s == Linea: %d, File: %s\n",getpid(),"Errore apertura file",QUI);
			continue;
	  }
		
		// leggo interi dal file binario
		int e = fseek(f, 0, SEEK_END); //sposto il puntatore in fondo
		if (e != 0) xtermina("Errore fseek",QUI);
		long t = ftell(f); //mi dice quanto e grande il file fino al puntatore
		if (t < 0) xtermina("Errore ftell",QUI);
		int n = t/sizeof(long); //grandezza array
		long *a = malloc(n*sizeof(long)); //array di long letti dal file
		if (a == NULL) xtermina("Errore malloc",QUI);
		rewind(f); //puntatore del file resettato
		//if (e != 0) xtermina("Padre> Errore rewind");
		e = fread(a, sizeof(long), n, f); //leggo n long dal file f e li metto in a
		
		// calcolo somma
		long sum = 0;
		for (int i = 0; i < n; i++) {
			sum += a[i]*i;
		}
		
		free(a);
		if (fclose(f) != 0) xtermina("Errore chiusura file",QUI);
		
		// mando il risultato al collector
		// per prima cosa stabilisco la connessione con il server
		int fd_skt = 0;      // file descriptor associato al socket
	  struct sockaddr_in serv_addr;
	  // crea socket
	  if ((fd_skt = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
	    xtermina("Errore creazione socket",QUI);
	  // assegna indirizzo
	  serv_addr.sin_family = AF_INET;
	  // il numero della porta deve essere convertito 
	  // in network order 
	  serv_addr.sin_port = htons(PORT);
	  serv_addr.sin_addr.s_addr = inet_addr(HOST);
	  // apre connessione
	  if (connect(fd_skt, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) 
	    xtermina("Errore apertura connessione soket",QUI);
		
    // mando somma
	  sendLong(fd_skt, sum);

		// mando nome file
		sendString(fd_skt, nome_file);
		
		// chiudo connessione
		 if(close(fd_skt)<0)
    	xtermina("Errore chiusura socket",QUI);
  }
  pthread_exit(NULL); 
}    



// thread che effettua la gestione di SIGINT
void *sigintHandler(void *v) {
	handlerData *data = (handlerData *)v; 
	// creo mask con SIGINT
  sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask,SIGINT);
	int e = -1;
	siginfo_t s;
	struct timespec time = {1, 0};
	// mi metto in ascolto di sigint, con un timeout per controllare periodicamente se il padre mi dice di finire
	while (e == -1) {
		xpthread_mutex_lock(data->sigmutex,QUI);
		if (data->fine) break;
		xpthread_mutex_unlock(data->sigmutex,QUI);
		e = sigtimedwait(&mask,&s, &time);
		if (e != -1 && e != SIGINT) xtermina("Errore sigtimewait",QUI);
	}
  
	if (e == SIGINT) // if poco utile visto che lo controllo gia sopra, ma per sicurezza
	{ 
		// ho ricevuto SIGINT
		xpthread_mutex_lock(data->sigmutex,QUI);
		data->sigint = true;
		xpthread_mutex_unlock(data->sigmutex,QUI);
	}
  pthread_exit(NULL); 
}


void print_usage(char *nome_exec) {
	printf("Uso: %s file [file ...] \nOptions:\n-n nthread Numero thread worker(>0)\n-q qlen Lunghezza buffer prod/cons(>0)\n-t delay Tempo(millisec) tra scritture del master sul buffer(>=0)",nome_exec);
}