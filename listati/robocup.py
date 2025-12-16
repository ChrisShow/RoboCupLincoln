# import eventuali


# classe Cella: rappresenta una tessera del labirinto 
# con delle coordinate cartesiane x e y
class Cella:

    # costruttore
    def __init__(self, x, y):
        self.x = x
        self.y = y
    
    # metodo che riceve una coppia di coordinate e ritorna un booleano
    # a seconda se esse corrispondano a quelle dell'oggetto oppure no
    def confronto(self, x, y):
        if self.x == x and self.y == y:
            return True
        return False
    
    # metodo che definisce come confrontare due oggetti Cella
    def __eq__(self, altraCella):
        if not isinstance(altraCella, Cella):
            return False
        return self.getX() == altraCella.getX() and self.getY() == altraCella.getY()
    
    # getters e setters
    def setX(self, x):
        self.x = x
    def setY(self, y):
        self.y = y
    def getX(self):
        return self.x
    def getY(self):
        return self.y

# classe MemoriaCelle: rappresenta una lista di celle visitate
# dalla più recentemente alla meno recentemente
class MemoriaCelle:

    # costruttore
    def __init__(self):
        self.memoria = []
        self.testa = 0  # Indice del primo elemento vuoto
    
    # metodo che inserisce un elemento in cima alla lista
    def push(self, elemento):
        if elemento is None:
            return
        self.memoria.insert(0, elemento)    
        
    # metodo che riceve una cella e restituisce un intero in base 
    # quante mosse prima è stata visitata quella cella
    # ritorna -1 se la cella passata come parametro non è stata ancora visitata
    # ritorna -2 se la cella passata come parametro è nulla
    def storicoCella(self, elemento):
        if elemento is None:
            return -2
        for i, cella in enumerate(self.memoria):
            if cella.confronto(elemento.getX(), elemento.getY()):
                return i
        return -1
    
    # metodo che riceve una cella e restituisce un booleano in base 
    # al fatto se la cella è già stata visitata in passato o meno
    # dalla mossa precedente in poi
    def cellaGiaVisitata(self, elemento):
        if elemento is None:
            return False
        for i, cella in enumerate(self.memoria, start=1):
            if cella.confronto(elemento.getX(), elemento.getY()):
                return True
        return False

    # Riceve 3 celle rappresentanti le direzioni dritto, destra e sinistra
    # e ritorna la direzione da prendere.
    # 0 = DRITTO, 1 = DESTRA, 2 = SINISTRA, 3 = DIETRO
    def scegliDirezione(self, dritto, destra, sinistra):

        storico_dritto = -1
        storico_destra = -1
        storico_sinistra = -1
        
        trovata_dritto = False
        trovata_destra = False
        trovata_sinistra = False
        
        # Inizializza i valori se la cella è None
        if dritto is None:
            storico_dritto = -1
            trovata_dritto = True
        
        if destra is None:
            storico_destra = -1
            trovata_destra = True
        
        if sinistra is None:
            storico_sinistra = -1
            trovata_sinistra = True
        
        # Scansiona la memoria per trovare le celle
        for i, cella in enumerate(self.memoria):
            if trovata_dritto and trovata_sinistra and trovata_destra:
                break
            
            if not trovata_dritto and cella.confronto(dritto.getX(), dritto.getY()):
                trovata_dritto = True
                storico_dritto = i
            
            if not trovata_destra and cella.confronto(destra.getX(), destra.getY()):
                trovata_destra = True
                storico_destra = i
            
            if not trovata_sinistra and cella.confronto(sinistra.getX(), sinistra.getY()):
                trovata_sinistra = True
                storico_sinistra = i
        
        # Restituisce la direzione
        if not trovata_dritto:
            return 0 # Vai dritto
        if not trovata_destra:
            return 1 # Vai a dx
        if not trovata_sinistra:
            return 2 # Vai a sx
        
        # Caso in cui abbiamo visitato già tutte le celle circostanti
        if storico_dritto > storico_destra and storico_dritto > storico_sinistra:
            return 0 # Vai dritto
        elif storico_destra > storico_dritto and storico_destra > storico_sinistra:
            return 1 # Vai a dx
        elif storico_sinistra > storico_dritto and storico_sinistra > storico_destra:
            return 2 # Vai a sx
        elif storico_dritto == -1 and storico_destra == -1 and storico_sinistra == -1:
            return 3 # Vai dietro

# variabili globali
memoriaCelle: MemoriaCelle = MemoriaCelle()
listaCelleNonVisitate = []
listaCelleNonPercorribili = []
cellaCorrente: Cella = Cella(0,0)
cellaDritto: Cella = None
cellaDestra: Cella = None
cellaSinistra: Cella = None
cellaDietro: Cella = None
direzione = 1
codRilevazione = 0
# TODO inizializza posizione telecamera e avvia telecamera

# DIREZIONI: 0=SOPRA, 1=DESTRA, 2=SINISTRA, 3=SOTTO

def guardaIntorno():
    str = ""
    ''' TODO str proviene da ARDUINO tramite una ricezione bloccante ed è una stringa formata da 3 numeri separati da una virgola
        I numeri possono essere solo 0 o 1, il primo indica se in quella direzione non c'è un ostacolo, il secondo si.
        Il primo dei 3 numeri controlla gli ostacoli dritto, il secondo a destra, il terzo a sinistra, il quarto dietro
    '''
    dritto = int(str.split(",")[0])
    destra = int(str.split(",")[1])
    sinistra = int(str.split(",")[2])

    if dritto == 0:
        """ TODO
        se non si è mai visitata la cella corrente:
            sposta telecamera dritto rispetto alla direzione in cui si trova il robottino
            scatta una foto e salvala nella cartella img con nome imgcodRilevazione.jpg
            codRilevazione++
            se nella foto è presente un colore tra verde, rosso e giallo:
                se è presente allora accendi i led del colore corrispondente
            altrimenti se nella foto è presente una lettera H o S o U:
                dì ad arduino di azionare la ruota e far cadere un medikit
            """
    elif dritto == 1:
        if direzione == 0:
            cellaDritto = Cella(cellaCorrente.getX(), cellaCorrente.getY()+1)
        elif direzione == 1:
            cellaDritto = Cella(cellaCorrente.getX()+1, cellaCorrente.getY())
        elif direzione == 2:
            cellaDritto = Cella(cellaCorrente.getX()-1, cellaCorrente.getY())
        elif direzione == 3:
            cellaDritto = Cella(cellaCorrente.getX(), cellaCorrente.getY()-1)
    if cellaDritto in listaCelleNonPercorribili:
        cellaDritto = None
    elif not cellaDritto in listaCelleNonVisitate:
        listaCelleNonVisitate.insert(0, Cella(cellaDritto.getX(), cellaDritto.getY()))
    
    if destra == 0:
        """ TODO
        se non si è mai visitata la cella corrente:
            sposta telecamera a destra rispetto alla direzione in cui si trova il robottino
            scatta una foto e salvala nella cartella img con nome imgcodRilevazione.jpg
            codRilevazione++
            se nella foto è presente un colore tra verde, rosso e giallo:
                se è presente allora accendi i led del colore corrispondente
            altrimenti se nella foto è presente una lettera H o S o U:
                dì ad arduino di azionare la ruota e far cadere un medikit
            """
    elif destra == 1:
        if direzione == 0:
            cellaDestra = Cella(cellaCorrente.getX()+1, cellaCorrente.getY())
        elif direzione == 1:
            cellaDestra = Cella(cellaCorrente.getX(), cellaCorrente.getY()-1)
        elif direzione == 2:
            cellaDestra = Cella(cellaCorrente.getX(), cellaCorrente.getY()+1)
        elif direzione == 3:
            cellaDestra = Cella(cellaCorrente.getX()-1, cellaCorrente.getY())
    if cellaDestra in listaCelleNonPercorribili:
        cellaDestra = None
    elif not cellaDestra in listaCelleNonVisitate:
        listaCelleNonVisitate.insert(0, Cella(cellaDestra.getX(), cellaDestra.getY()))

    if sinistra == 0:
        """ TODO
        se non si è mai visitata la cella corrente:
            sposta telecamera a sinistra rispetto alla direzione in cui si trova il robottino
            scatta una foto e salvala nella cartella img con nome imgcodRilevazione.jpg
            codRilevazione++
            se nella foto è presente un colore tra verde, rosso e giallo:
                se è presente allora accendi i led del colore corrispondente
            altrimenti se nella foto è presente una lettera H o S o U:
                dì ad arduino di azionare la ruota e far cadere un medikit
            """
    elif sinistra == 1:
        if direzione == 0:
            cellaSinistra = Cella(cellaCorrente.getX()-1, cellaCorrente.getY())
        elif direzione == 1:
            cellaSinistra = Cella(cellaCorrente.getX(), cellaCorrente.getY()+1)
        elif direzione == 2:
            cellaSinistra = Cella(cellaCorrente.getX(), cellaCorrente.getY()-1)
        elif direzione == 3:
            cellaSinistra = Cella(cellaCorrente.getX()+1, cellaCorrente.getY())
    if cellaSinistra in listaCelleNonPercorribili:
        cellaSinistra = None
    elif not cellaSinistra in listaCelleNonVisitate:
        listaCelleNonVisitate.insert(0, Cella(cellaSinistra.getX(), cellaSinistra.getY()))

    if direzione == 0:
        cellaDietro = Cella(cellaCorrente.getX(), cellaCorrente.getY()-1)
    elif direzione == 1:
        cellaDietro = Cella(cellaCorrente.getX()-1, cellaCorrente.getY())
    elif direzione == 2:
        cellaDietro = Cella(cellaCorrente.getX()+1, cellaCorrente.getY())
    elif direzione == 3:
        cellaDietro = Cella(cellaCorrente.getX(), cellaCorrente.getY()+1)

# funzione principale del programma   
def main():
    guardaIntorno()
    while not listaCelleNonVisitate:
        prossimaDirezione = memoriaCelle.scegliDirezione(cellaDritto, cellaDestra, cellaSinistra)
        esito = 1
        # TODO esito = muoviProssimaDirezione(prossimaDirezione) AD ARDUINO
        if esito == 1:
            if prossimaDirezione == 0:
                if cellaDritto in listaCelleNonVisitate:
                    listaCelleNonVisitate.remove(cellaDritto)
                memoriaCelle.push(Cella(cellaDritto.getX(), cellaDritto.getY()))
                cellaCorrente = Cella(cellaDritto)
            elif prossimaDirezione == 1:
                if cellaDestra in listaCelleNonVisitate:
                    listaCelleNonVisitate.remove(cellaDestra)
                memoriaCelle.push(Cella(cellaDestra.getX(), cellaDestra.getY()))
                cellaCorrente = Cella(cellaDestra)
            elif prossimaDirezione == 2:
                if cellaSinistra in listaCelleNonVisitate:
                    listaCelleNonVisitate.remove(cellaSinistra)
                memoriaCelle.push(Cella(cellaSinistra.getX(), cellaSinistra.getY()))
                cellaCorrente = Cella(cellaSinistra)
            elif prossimaDirezione == 3:
                if cellaDietro in listaCelleNonVisitate:
                    listaCelleNonVisitate.remove(cellaDietro)
                memoriaCelle.push(Cella(cellaDietro.getX(), cellaDietro.getY()))
                cellaCorrente = Cella(cellaDietro)
        elif esito == 0:
            if prossimaDirezione == 0 and not cellaDritto in listaCelleNonPercorribili:
                listaCelleNonPercorribili.insert(0, Cella(cellaDritto.getX(), cellaDritto.getY()))
            elif prossimaDirezione == 1 and not cellaDestra in listaCelleNonPercorribili:
                listaCelleNonPercorribili.insert(0, Cella(cellaDestra.getX(), cellaDestra.getY()))
            elif prossimaDirezione == 2 and not cellaSinistra in listaCelleNonPercorribili:
                listaCelleNonPercorribili.insert(0, Cella(cellaSinistra.getX(), cellaSinistra.getY()))
            elif prossimaDirezione == 3 and not cellaDietro in listaCelleNonPercorribili:
                listaCelleNonPercorribili.insert(0, Cella(cellaDietro.getX(), cellaDietro.getY()))
        
        guardaIntorno()

if __name__ == "__main__":
    main()


