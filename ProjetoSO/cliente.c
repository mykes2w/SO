#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "taxi.h"

char cliente_pipe_geral[50]; //guardao nome do cliente  que vem no pipe
char nome[50]; //guarda o nome do utilizador
int taxi_id=0;

void trata_sair(int s){ //caso faça ctrl c , ele apaga o pipe
  (void)s;
  printf("\n(C) A encerrar sistema\n");
  unlink(cliente_pipe_geral);
  exit(0);
}

void verifica_comando(){
  char buffer[200];
  int fd=open(cliente_pipe_geral, O_RDONLY | O_NONBLOCK); //tenta abrir o pipe , se n tiver la nada retorna -1
  if(fd!=-1){
    int n=read(fd,buffer,sizeof(buffer)-1);
    if(n>0){
      buffer[n]='\0';
      printf("\nComando recebido: %s\n",buffer);
      
      int id_temp;
      if(sscanf(buffer,"Taxi %d chegou",&id_temp)==1){ //guarda o id do taxi
        taxi_id=id_temp; //sabemos qual é o nosso taxi
        printf("O taxi %d esta a sua espera \n",taxi_id);
        printf("Escreva 'entrar <destino' para iniciar a viagem.\n");
      }
      else{
      printf("\n\n%s\n",buffer);
      if(strstr(buffer,"Obrigado") != NULL || strstr(buffer,"cancelada")!=NULL || strstr(buffer,"interrompida")!=NULL){ //caso acontecer isto agendo nova viagem
        taxi_id=0;
        printf("Pode agendar nova viagem\n");
        }
      }
      printf("\n- ");
      fflush(stdout); //obriga o ecra a mostrar otextopara o cliente ver na altura certa
    }
    close(fd);
  }
}
void trata_sin(int s){ //quando recebe o sigusr2 , basicamente quando é para despertar o cliente
  (void)s;
  verifica_comando();
  printf("\n- ");
  fflush(stdout);
}
int main(int argc,char *argv[]){
  setbuf(stdout,NULL); //desliga o buffering para veres prints instantaneos
  if(argc<2){
    printf("Erro: Tem que escrever './cliente <Nome>'\n");
    return 1;
  }
  strcpy(nome,argv[1]); 
  
  int fd_serv,fd_resp;
  PedidoCliente p;
  RespostaServidor resp;
  char comando[50];
  char argumento[50];
  struct sigaction sa,sa_alrm;

  p.pid_cliente =getpid();
  sprintf(cliente_pipe_geral,PIPE_CLIENTE,p.pid_cliente); //configura o nome do pipe privado
  
  sa.sa_handler=trata_sair;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags=0;
  sigaction(SIGINT,&sa,NULL);
  
  struct sigaction sa_taxi;
  sa_taxi.sa_handler=trata_sin;
  sa_taxi.sa_flags=SA_RESTART; //restart para que ele acorde e nao devolva erro , volte a pedir o que é suposto do cliente
  sigemptyset(&sa_taxi.sa_mask);
  sigaction(SIGUSR2,&sa_taxi,NULL);

  if(mkfifo(cliente_pipe_geral,0666)==-1){ //cria pipe no disco
    if(access(cliente_pipe_geral,F_OK)!=0){
      perror("Erro ao criar pipe");
      return 1;
    }
  }
  
  int fd_aberto =open(cliente_pipe_geral , O_RDWR); //mantem aberto para manter o pipe vivo
  
  fd_serv=open(PIPE_SERVIDOR,O_WRONLY);
  if(fd_serv!=-1){
    p.hora=-2; //quando é login
    strcpy(p.username,nome);
    write(fd_serv,&p,sizeof(PedidoCliente));
    close(fd_serv); //fecha para nao ocupar o servidor
    
    fd_resp=open(cliente_pipe_geral,O_RDONLY); //espera pela resposta do servidor 
    read(fd_resp,&resp,sizeof(RespostaServidor));
    close(fd_resp);
    if(strcmp(resp.mensagem,"Ok")!=0){ //se a mensagem nao for ok 
      printf("Erro de Login: %s\n",resp.mensagem);
      unlink(cliente_pipe_geral);
      return 1; //sai da app
    }
  }else{
    printf("Servidor desligado\n");
    unlink(cliente_pipe_geral);
    return 1;
  }

  printf("Cliente %s com PID(%d) entrou.\n",nome,p.pid_cliente);

  while(1){
    verifica_comando(); //verifica se ha mensagens 
    printf("\n Menu do Cliente \n-'agendar <instante> <origem> <destino> <kms>'\n-'cancelar <id>'\n-'consultar'\n-'entrar <destino>'\n-'sair'\n-'terminar: \n\n");
    scanf("%s",comando);
    verifica_comando();

    if(strcmp(comando,"terminar")==0){
        break;
    }
    else if(strcmp(comando,"sair")==0){
      if(taxi_id==0){
        printf("Usa 'terminar' para fechar a app. 'sair' é pa sair do taxi.\n");
        continue;
      }
      printf("Esta neste momento numa viagem. Quer sair do 'Taxi' ou da 'App'?\n");
      char opcao;
      while(getchar() != '\n');
      scanf("%c",&opcao);
      if(opcao=='A' || opcao=='a'){
        break;
      }
      char pipe_taxi[50];
      sprintf(pipe_taxi,PIPE_VEICULO,taxi_id); //vai buscar o nr do taxi 
      int fd_taxi=open(pipe_taxi,O_WRONLY); //abres o pipe do taxi
      if(fd_taxi!=-1){
        write(fd_taxi,"sair",5); //envias a palavra sair e o id do taxi
        close(fd_taxi);
        printf("saiu do taxi a meio da viagem.\n");
        taxi_id=0;
      }else{
        printf("Erroaoc omunicar com taxi\n");
      }
    }
    else if(strcmp(comando,"entrar")==0){
      scanf("%s",argumento);
      verifica_comando();
      if(taxi_id==0){
        printf("Nenhum taxi chegou ainda\n");
        continue;
      }
      char pipe_taxi[50];
      sprintf(pipe_taxi,PIPE_VEICULO,taxi_id);
      int fd_taxi=open(pipe_taxi,O_WRONLY);
      if(fd_taxi!=-1){
        char msg_entrar[100];
        sprintf(msg_entrar,"entrar %s",argumento);
        write(fd_taxi,msg_entrar,strlen(msg_entrar)+1);
        close(fd_taxi);
        printf("Entrou no taxi %d , boa viagem\n",taxi_id);
      }else {
        printf("Nao consegui entrar no taxi\n");
      }
    }
    else if(strcmp(comando,"agendar")==0){
      strcpy(p.username,nome);
      if(scanf("%d %s %s %d",&p.hora,p.origem,p.destino,&p.distancia)!=4)
      {
        printf("Formato invalido. Escreva: agendar HHMM Origem Destino KMs\n");
        while(getchar()!='\n');
        continue;
      }

      fd_serv=open(PIPE_SERVIDOR,O_WRONLY);
      if(fd_serv!=-1)
      {
        write(fd_serv,&p,sizeof(PedidoCliente));
        close(fd_serv);
        fd_resp=open(cliente_pipe_geral,O_RDONLY);
        read(fd_resp,&resp,sizeof(RespostaServidor));
        close(fd_resp);
        printf("%s\n",resp.mensagem);
      }
    }
    else if(strcmp(comando,"consultar")==0){
      p.hora=-1;
      p.pid_cliente=getpid();
      fd_serv=open(PIPE_SERVIDOR,O_WRONLY);
      if(fd_serv==-1) { printf("Servidor offline\n"); continue;}
      write(fd_serv,&p,sizeof(PedidoCliente));
      close(fd_serv);
      fd_resp =open(cliente_pipe_geral,O_RDONLY);
      printf("\n Agendamentos \n");
      while(read(fd_resp,&resp,sizeof(RespostaServidor))>0){
        if(strcmp(resp.mensagem ,"Fim")==0) break;
        printf("%s\n",resp.mensagem);
      }
      close(fd_resp);
    }
    else if(strcmp(comando,"cancelar")==0){
      int id_temp;
      if(scanf("%d",&id_temp)==1){
        p.hora=-3;
        p.distancia=id_temp;
        p.pid_cliente=getpid();
        
        fd_serv=open(PIPE_SERVIDOR,O_WRONLY);
        if(fd_serv==-1){
          printf("Servidor offline \n");
          continue;
        }
        write(fd_serv,&p,sizeof(PedidoCliente));
        close(fd_serv);
        
        fd_resp=open(cliente_pipe_geral,O_RDONLY);
        read(fd_resp,&resp,sizeof(RespostaServidor));
        close(fd_resp);
        printf("%s\n",resp.mensagem);
      }else{
        printf("Invalido , 'cancelar <id>'\n");
        while(getchar()!='\n');
      }
    }else{
      printf("Comando ivnalido\n");
      while(getchar()!='\n');
    }
  }
  unlink(cliente_pipe_geral);
  return 0;
}

