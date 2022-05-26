#! /usr/bin/env python3
import sys, struct, socket, threading, select


# host e porta di default
HOST = "127.0.0.1"  # Standard loopback interface address (localhost)
PORT = 55955  # Port to listen on (non-privileged ports are > 1023)
 

# codice da eseguire nei singoli thread 
class ClientThread(threading.Thread):
    def __init__(self,conn,addr):
        threading.Thread.__init__(self)
        self.conn = conn
        self.addr = addr
    def run(self):
      # print("====", self.ident, "mi occupo di", self.addr)
      gestisci_connessione(self.conn,self.addr)
      # print("====", self.ident, "ho finito")



def main(host=HOST,port=PORT):
  # creiamo il server socket
  with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    try:
      # print("In attesa del master...")
      s.bind((host, port))
      s.listen()
      # prima stabilisco la connessione con masterworker per ricevere il messaggio di terminazione
      conn_master, addr_master = s.accept()
      # print(f"Connessione con master avvenuta: HOST: {host}  PORT: {port}")
      with conn_master:
        # uso select per vedere quando è disponibile un nuovo client oppure il master è pronto per terminare 
        inputsk = [s, conn_master]  
        while len(inputsk)>0:
          inready, _ , _ = select.select(inputsk,[],[],5)
          if len(inready) > 0:
            # server o master sono pronti
            for c in inready:
              if c is s:
                # il server socket ha ricevuto una richiesta di connessione
                conn, addr = s.accept()
                # la gestisco con un thread e mi rimetto in ascolto
                t = ClientThread(conn,addr)
                t.start()
              if c is conn_master:
                # il master ha inviato il valore di terminazione, ha fatto join dei thread quindi posso chiudere il server tranquillamente
                data = recv_all(conn_master,4)
                term = struct.unpack("!i",data[:4])[0] 
                if term == 123456789: # è arrivato il valore di terminazione giusto
                  inputsk.remove(s)
                  inputsk.remove(conn_master)
                  break  # esco dal while (ci uscirebbe lo stesso)
                
    except KeyboardInterrupt:
      pass
    
    # print('Va bene smetto...')
    s.shutdown(socket.SHUT_RDWR)


# gestisci una singola connessione
# con un client
def gestisci_connessione(conn,addr): 
  with conn:  
    while True:
      # ricevo valore di continuazione/terminazione
      data = recv_all(conn,4)
      val = struct.unpack("!i",data)[0]  
      if val == 0: break

      # ricevo max32bit e len
      data = recv_all(conn,8)
      max32  = struct.unpack("!i",data[:4])[0]  
      len  = struct.unpack("!i",data[4:])[0]  
      # ricevo offset
      data = recv_all(conn,4)
      offset  = struct.unpack("!i",data)[0]  
      # calcolo somma
      sum = (max32*len)+offset
      
      # ricevo lunghezza nomefile
      data = recv_all(conn,4)
      len  = struct.unpack("!i",data)[0]
      
      # ricevo caratteri uno ad uno
      nomefile = ""
      for i in range(len):
        data = recv_all(conn,1)
        char = (struct.unpack("!c",bytes(data))[0]).decode("utf-8") 
        nomefile += char
      # print su stdout
      print(f"{sum:>10} {nomefile}", file = sys.stdout)
      # print(f"Finito con {addr}")


# riceve esattamente n byte e li restituisce in un array di byte
# il tipo restituto è "bytes": una sequenza immutabile di valori 0-255
# analoga alla readn che abbiamo visto nel C
def recv_all(conn,n):
  chunks = b''
  bytes_recd = 0
  while bytes_recd < n:
    chunk = conn.recv(min(n - bytes_recd, 1024))
    if len(chunk) == 0:
      raise RuntimeError("socket connection broken")
    chunks += chunk
    bytes_recd = bytes_recd + len(chunk)
  return chunks



if len(sys.argv)==1:
  main()
else:
  print("Uso:\n\t %s [host] [port]" % sys.argv[0])



