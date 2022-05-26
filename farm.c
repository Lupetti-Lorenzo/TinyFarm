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
					xtermina("Errore -n: numero thread deve essere almeno 1", QUI);
				}
				break;
			case('q') : 
				buf_size = atof(optarg);
				if (buf_size<1) {
					xtermina("Errore -q: la dimensione del buffer deve essere almeno 1", QUI);
				}
				break;
			case('t') :
				delay = atof(optarg);
				if (delay<0) {
					xtermina("Errore -t: delay deve essere >=0", QUI);
				}
				break;
			default:
				//getopt dovrebbe accorgersi se metto un parametro diverso da nqt, quindi in teoria non dovrei mai arrivare a questo default
				print_usage(name_exec);
			  xtermina("Errore parametri input: qualcosa è andato storto nell'analisi degli argomenti da linea di comando", QUI);
		}
	}
	
	// stabilisco la connessione con il collector con cui mando messaggio di terminazione
	int fd_skt = 0;      // file descriptor associato al socket
	struct sockaddr_in serv_addr;
	// crea socket
	if ((fd_skt = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
		xtermina("Errore creazione socket master", QUI);
	// assegna indirizzo
	serv_addr.sin_family = AF_INET;
	// il numero della porta deve essere convertito 
	// in network order 
	serv_addr.sin_port = htons(PORT);
	serv_addr.sin_addr.s_addr = inet_addr(HOST);
	// apre connessione
	if (connect(fd_skt, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) 
		xtermina("Errore nella connessione con il server, controlla che sia in funzione e sulla porta giusta", QUI);


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
	for (int i = optind; i < argc; i++) {
		// controllo sigint
		xpthread_mutex_lock(data.sigmutex,QUI);
		if (data.sigint) {
			// è arrivato sigint, smetto di scrivere sul buffer, lascio il resto concludere
			break;
		}
		xpthread_mutex_unlock(data.sigmutex,QUI);
		
    // scrittura sul buffer 
    xsem_wait(&sem_free_slots,QUI);
    buffer[pindex % buf_size] = argv[i];
    xsem_post(&sem_data_items,QUI);
		pindex += 1;
		if (delay) sleep(delay/1000);
  }
	
	
	// scrivo i valori di terminazione ai worker
	for (int i = 0; i < nt; i++) {
		xsem_wait(&sem_free_slots,QUI);
    buffer[pindex % buf_size] = "FINE";
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
	int tmp = htonl(123456789);
	ssize_t e = writen(fd_skt,&tmp,sizeof(tmp));
	if(e!=sizeof(tmp)) xtermina("Errore write valore di terminazione", QUI);
	
	// chiudo connessione con il socket
	if(close(fd_skt)<0)
		xtermina("Errore chiusura socket master", QUI);
	
	return 0;
}