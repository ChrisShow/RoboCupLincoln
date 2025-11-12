#include <iostream>
#include <string>
#include "listacelle.h"
using namespace std;


int main()
{
	Cella c1(0,0);
	Cella c2(1,1);
	Cella c3(2,2);
	Cella c4(3,3);
	Cella c5(4,4);
	MemoriaCelle mc;
	mc.push(c5);
	mc.push(c3);
	mc.push(c1);
	mc.push(c4);
	mc.push(c2);
	mc.push(c5);
	cout<<mc.scegliDirezione(c1, c2, c3);
}