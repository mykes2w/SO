#ifndef TAXI_H
#define TAXI_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#define PIPE_SERVIDOR "pipe_taxis"
#define PIPE_CLIENTE "pipe_cliente_%d"
#define PIPE_VEICULO "pipe_taxi_%d"

typedef struct{
pid_t pid_cliente;
char username[50];
int hora;
char origem[50];
char destino[100];
int distancia;
}PedidoCliente;

typedef struct{
char mensagem[100];
int sucesso;
}RespostaServidor;

#endif 
