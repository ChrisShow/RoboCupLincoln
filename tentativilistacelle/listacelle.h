#include <iostream>
#include <vector>
using namespace std;

class Cella
{
	public:
		int x;
		int y;
	Cella(int x, int y)
	{
		this->y=y;
		this->x=x;
	}
	bool confronto(int x,int y)
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
	int getX(){
		return this->x;
	}
	int getY(){
		return this->y;
	}
};

class MemoriaCelle {
private:
    vector<Cella> memoria;
    int testa;                        // Indice del primo elemento vuoto
	public:
    // Costruttore: inizializza la pila vuota
    MemoriaCelle()
    {
    	
	}
    // Metodo push: inserisce un elemento in cima alla pila
    void push(Cella elemento) {
	memoria.push_back(elemento);
    }
    //metodo utilizzato per restituire un intero in base allo storico delle celle visitate, ritorna -1 se la cella non è stata ancora visitata
    int ricerca( Cella elemento)
    {
    	int i=0;
    	for(Cella cella:this->memoria)
    	{
    		if(cella.confronto(elemento.getX(),elemento.getY()))
    		{
    			return i;
			}
			i++;
		}
		return -1;
	}
};