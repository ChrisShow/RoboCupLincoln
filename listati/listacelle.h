#include <iostream>
#include <vector>
using namespace std;

class Cella
{
	private:
		int x;
		int y;
	
	public:
	Cella(int x, int y)
	{
		this->y=y;
		this->x=x;
	}
	bool confronto(int x,int y) const
	{
		if(this->x==x && this->y==y)
			return true;
		return false;	
	}
	void setX(int x)
	{
		this->x=x;
	}
	void setY(int y)
	{
		this->y=y;
	}
	int getX() const {
		return this->x;
	}
	int getY() const {
		return this->y;
	}
};

class MemoriaCelle {
	private:
	    vector<Cella> memoria;
	    int testa;                        // Indice del primo elemento vuoto
	public:
    // Costruttore: inizializza la pila vuota
    MemoriaCelle() {}
    
    // Metodo push: inserisce un elemento in cima alla pila
    void push(const Cella& elemento) {
    	if(elemento == NULL) return;
		memoria.push_back(elemento);
    }
    
    //metodo utilizzato per restituire un intero in base allo storico delle celle visitate
	//ritorna -1 se la cella non è stata ancora visitata
    int storicoCella(const Cella& elemento)
    {
    	if(elemento == NULL) return -1;
    	int i=0;
    	for(const Cella& cella:this->memoria)
    	{
    		if(cella.confronto(elemento.getX(),elemento.getY()))
    		{
    			return i;
			}
			i++;
		}
		return -1;
	}
	
	// Metodo che riceve 3 celle rappresentanti le direzioni dritto, destra e sinistra e ritorna la direzione da prendere
	// 0 = DRITTO, 1 = DESTRA, 2 = SINISTRA
	scegliDirezione(const Cella& dritto, const Cella& destra, const Cella& sinistra) {
		int storicoDritto, storicoDestra, storicoSinistra, i = 0;
		bool trovataDritto = false, trovataDestra = false, trovataSinistra = false;
		if(dritto == NULL) {
			storicoDritto = -1;
			trovataDritto = true;
		}
		if(destra == NULL) {
			storicoDestra = -1;
			trovataDestra = true;
		}
		if(sinistra == NULL) {
			storicoSinistra = -1;
			trovataSinistra = true;
		}
		for(const Cella& cella : this->memoria) {
			if(trovataDritto && trovataSinistra && trovataDestra) break;
			if(!trovataDritto && cella.confronto(dritto.getX(), dritto.getY())) {
				trovataDritto = true;
				storicoDritto = i;
			}
			if(!trovataDestra && cella.confronto(destra.getX(), destra.getY())) {
				trovataDestra = true;
				storicoDestra = i;
			}
			if(!trovataSinistra && cella.confronto(sinistra.getX(), sinistra.getY())) {
				trovataSinistra = true;
				storicoSinistra = i;
			}
		}
		if(!trovataDritto) return 0;
		if(!trovataDestra) return 1;
		if(!trovataSinistra) return 2;
		if(storicoDritto < storicoDestra && storicoDritto < storicoSinistra) return 0;
		else if(storicoDestra < storicoDritto && storicoDestra < storicoSinistra) return 1;
		else if(storicoSinistra < storicoDritto && storicoSinistra < storicoDestra) return 2;
	}
};