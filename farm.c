// devo pure rinominare a rota e scrivere tutto in italiano
// vedere se termina farlo stampare su stdout
// chiedere al prof anche per la regexp, i file avranno una estenzione o possono essere senza, che devo farla in modo diverso in quel caso
//send long e send name vedere come fare per valore di ritorno tipo funzioni libreria e termina ecc
// sambiare termina con xtermina

#include "xerrori.h"
#include "funzioni.h"


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
	int tmp = htonl(1234567);
	ssize_t e = writen(fd_skt,&tmp,sizeof(tmp));
	if(e!=sizeof(tmp)) termina("Errore write valore di terminazione");
	
	// chiudo connessione con il socket
	if(close(fd_skt)<0)
		termina("Errore chiusura socket master");
	
	return 0;
}