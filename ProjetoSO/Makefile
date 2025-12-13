all: controlador veiculo cliente

controlador: controlador.c taxi.h
	gcc controlador.c -o controlador -lpthread
veiculo: veiculo.c taxi.h
	gcc veiculo.c -o veiculo
cliente: cliente.c taxi.h
	gcc cliente.c -o cliente

clean:
	rm -f controlador veiculo cliente
	 
