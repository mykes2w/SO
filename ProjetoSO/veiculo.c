#include "taxi.h"
#include <errno.h>

char pipe_um[50]; //variavel para guardar o id do taxi 

void trata_cancel(int s){
  (void)s;
  fprintf(stderr,"(V) Cancelado pelo controlador.\n");
  unlink(pipe_um);
  exit(1);
}

int main(int argc,char *argv[]){
  setbuf(stdout,NULL); //desliga o buffer do printf
  signal(SIGUSR1,trata_cancel); //ativa o botao panmico
  if(argc<6){
    printf("(V) Erro: Faltam argumentos.\n");
    return 1;
  }
  int veiculo_id=atoi(argv[1]);
  char *origem = argv[2];
  char *destino = argv[3];
  int distancia=atoi(argv[4]);
  int pid_cliente = atoi(argv[5]);
  
  fprintf(stderr,"Sou o taxi %d, e vou contactar o cliente PID %d\n",veiculo_id,pid_cliente);
  
  char pipe_cli[50];
  char comando_rec[100];
  
  sprintf(pipe_um,PIPE_VEICULO,veiculo_id); //constroi o nome do pipe : pipe_taxi_1
  if(mkfifo(pipe_um,0666)==-1 && errno!=EEXIST){ //cria ficheiro no disco
    perror("mkfifo veiculo");
  }
  
  printf("(Taxi %d) a dirigir-se para %s \n",veiculo_id,origem);
  sleep(5);//demora 5 seg a chegar ao cliente
  
  sprintf(pipe_cli,PIPE_CLIENTE,pid_cliente); // constroi o nome do pipe cliente com o id do cliente : pipe_cliente_1234
  int fd_cli=open(pipe_cli,O_WRONLY); //abre a porta pa falar c cliente
  if(fd_cli!=-1){
    char msg[100];
    sprintf(msg,"Taxi %d chegou.",veiculo_id);
    write(fd_cli,msg,strlen(msg)+1); //manda msg para o cliente 
    close(fd_cli);
    kill(pid_cliente,SIGUSR2); //acorda o cliente
  }else{
    printf("Taxi %d - Cliente Desapareceu\n",veiculo_id);
    unlink(pipe_um);
    return 1;
  }
  
  printf("Taxi %d - A espera que o cliente entre\n",veiculo_id);
  int fd_um = open(pipe_um,O_RDONLY); //sem nonblock para ele ficar a espera que ele entre 
  if(fd_um==-1){
    perror("Erro pipe veiculo");
    unlink(pipe_um);
    return 1;
  }
  
  int cliente_in=0;
  while(read(fd_um,comando_rec,sizeof(comando_rec))>0){
    if(strncmp(comando_rec,"entrar", 6)==0){
      printf("Taxi %d - Cliente entrou. Destino Confirmado: %s\n",veiculo_id,destino);
      cliente_in=1;
      break;
    }
  }
  if(!cliente_in){
    printf("Taxi %d - Cliente nao entrou , a terminar.\n",veiculo_id);
    close(fd_um);
    unlink(pipe_um);
    return 0;
  }
  
  close(fd_um); //fecha o pipe
  fd_um = open(pipe_um,O_RDONLY | O_NONBLOCK); //abre outro c nonblock pq vamos precisar que o cliente fale
  int passo=distancia/10;
  if(passo<1) passo=1;

  for(int k=passo;k<=distancia;k+=passo){
    sleep(1);
    memset(comando_rec,0,sizeof(comando_rec)); //tenta ler algum comando do cliente 
    if(read(fd_um,comando_rec,sizeof(comando_rec))>0){ //como esta nonblock ele prossegue se nao houver nada 
      if(strncmp(comando_rec,"sair",4)==0){
        printf("Taxi %d - Cliente parou a viagem! \n",veiculo_id);
        printf("concluido (interrompido)\n");
        close(fd_um);
        unlink(pipe_um);
        return 2;
      }
    }
    int perc=(k*100)/distancia;
    if(perc>100) perc=100;
    printf("(Veiculo %d) Viagem a decorrer: %d%% da viagem concluida (%d/%d) km \n",veiculo_id,perc,k,distancia);
  }
  sleep(1);
  printf("concluido\n");
  fprintf(stderr,"(Veiculo %d) Chegamos ao destino. Serviço acabou\n",veiculo_id);
  
  close(fd_um);
  unlink(pipe_um);
  return 0;
}
