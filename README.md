# TINY FARM

Progetto finale per Laboratorio2

## Scelte implementative

### - `Comunicazione tra master e collector`

Sia il master che il collector per prima cosa stabiliscono una connessione tra di loro con cui il master può inviare il valore di terminazione (123456789).
Il collector, dopo aver stabilito la suddetta connessione entra in un loop in cui periodicamente controlla se è arrivato un client, e quindi devo gestirlo, oppure se il master è pronto a mandarmi il valore di terminazione.
Questo comportamento è stato implementato usando **select.select**, che come inputarray ha la connessione con il master e il server.
Il master manderà il valore di terminazione soltanto dopo aver fatto la join dei thread worker, quindi anche nel server i thread assegnati ai worker avranno sicuramente finito e quindi posso chiudere il server.

### `- Comunicazione tra worker e collector`

Il server adotta uno schema multithreading, assegnando un thread per ogni connessione in entrata.
I worker stabiliscono un unica connessione con cui mandare le coppie somma nome_file. Ogni volta che legge dal buffer manda un valore al server (0 per finire e 1 per continuare) per fargli sapere se deve finire oppure mettersi in ascolto di altri dati.
I dati vengono scambiati secondo un 'protocollo' diverso per numero e stringa, a cui entrambi i processi 'aderiscono'.
Se non riesce ad aprire il file oppure è vuoto, dopo aver stampato un messaggio su stdout skippa al prossimo valore che trova nel buffer, negli altri casi termina con un errore.

### `- Protocollo per mandare long`

La funzione **sendLong(int fd, long n)** manda un numero(anche a 64bit) al fd secondo un particolare schema per far fronte alla problematica che tramite le funzioni del c posso convertire in network order solo numeri fino a 32 bit.
Il numero lo spezzo in 3 parti: (Max32bit*quantita)+offset
max32bit è il numero massimo rappresentabile in macchina con 32 bit.
quantita(o len) è n%max32, quante volte ci sta max32bit nel numero da inviare
offset è resto della divisione n/max32bit(%), che aggiunto a max32bit*len forma il numero iniziale
Se il numero è a 32 bit, lun=0 e n=offset quindi (max32*0)+n = n, ma mando lo stesso con lo stesso "protocollo"
Questa funzione è utilizzata dal worker per inviare al server il risultato dell'analisi dei file che può essere piu grande di max32bit.

### `- Protocollo per mandare stringa(nomefile)`

La funzione **sendString(int fd, char *s)** è usata dal worker per scrivere i nomi dei file sul fd del socket.
Per prima cosa mando la lunghezza del nome (numero a 32bit), cosí il server sa quanti byte ricevere.
Poi mando carattere per carattere la stringa.

### `- Gestione di SIGINT`

Per prima cosa blocco il segnale in tutto il processo, per poi lanciare un thread gestore che si metterà in ascolto di SIGINT.
Il gestore ha due variabili booleane condivise con il master, insieme ad un mutex per coordinare letture/scritture delle variabili, racchiuse in una struct passata al momento del lancio del thread:

-**sigint** viene messo a true dal gestore quando arriva il segnale, nel master prima di ogni scrittura controllo se è true, in quel caso smetto di scrivere sul buffer.

-**fine** viene messo a true dal master dopo aver fatto la join dei worker, quindi ha finito e aspetta solo la join del gestore per concludere. Serve per dire al gestore di quittare, sennó non saprebbe quando finire in quanto sta li ad aspettare il segnale. Utilizzo la funzione **sigtimedwait** che ascolta il segnale e ogni tot secondi, specificati nel secondo parametro da un timeout, si interrompe e controlla se il master gli ha detto di finire.


### `- Organizzazione dei file`

Il codice del server scritto in python sta in collector.py, non mi è sembrato necessario spezzarlo in piu file dato che il codice è relativamente poco e leggibile.
Il codice in c è stato suddiviso in piú file: farm.c funzioni.c xerrori.c e generali.h.
generali.h comprende tutti gli include necessari al progetto, i define della porta e indirizzo con cui comunicare con il server e le struct che contengono i dati dei thread worker e del thread gestore.
xerrori.c è il file preso dagli esercizi fatti a lezione contenente le funzoni con la gestione dell'errore all'interno. Dato che xerrori.h è incluso negli altri file.c includo anche generali.h per scriverlo una sola volta.
funzioni.c è stato creato perché il codice in farm.c era troppo e iniziava a diventare confusionario, ci ho messo le funzioni readn e writen, il codice del thread worker e gestore e le funzioni sendLong e sendString.
farm.c include xerrori.h e funzioni.h e contiene il codice del masterworker, da cui genero l'eseguibile.
