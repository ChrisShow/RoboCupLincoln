class Cella:
    def __init__(self, x, y):
        self.x = x
        self.y = y
    
    def confronto(self, x, y):
        """Verifica se la cella ha le coordinate specificate"""
        if self.x == x and self.y == y:
            return True
        return False
    
    def setX(self, x):
        self.x = x
    
    def setY(self, y):
        self.y = y
    
    def getX(self):
        return self.x
    
    def getY(self):
        return self.y


class MemoriaCelle:
    def __init__(self):
        self.memoria = []
        self.testa = 0  # Indice del primo elemento vuoto
    
    def push(self, elemento):
        """Inserisce un elemento in cima alla lista"""
        if elemento is None:
            return
        self.memoria.insert(0, elemento)    
        
    def storicoCella(self, elemento):
        """
        Restituisce un intero in base allo storico delle celle visitate.
        Ritorna -1 se la cella non è stata ancora visitata
        """
        if elemento is None:
            return -1
        
        for i, cella in enumerate(self.memoria):
            if cella.confronto(elemento.getX(), elemento.getY()):
                return i
        
        return -1
    
    def scegliDirezione(self, dritto, destra, sinistra):
        """
        Riceve 3 celle rappresentanti le direzioni dritto, destra e sinistra
        e ritorna la direzione da prendere.
        0 = DRITTO, 1 = DESTRA, 2 = SINISTRA
        """
        storico_dritto = None
        storico_destra = None
        storico_sinistra = None
        
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
            return 0
        if not trovata_destra:
            return 1
        if not trovata_sinistra:
            return 2
        
        if storico_dritto > storico_destra and storico_dritto > storico_sinistra:
            return 0
        elif storico_destra > storico_dritto and storico_destra > storico_sinistra:
            return 1
        elif storico_sinistra > storico_dritto and storico_sinistra > storico_destra:
            return 2
