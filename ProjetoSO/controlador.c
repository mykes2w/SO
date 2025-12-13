#include "taxi.h"
#include <sys/wait.h>
#include <errno.h>

typedef struct{
  int ativo;
  PedidoCliente dados;
  int id_taxi;
}Agendamento;

typedef struct{
  char nome[50];
  pid_t pid;
  int online;
}Utilizador;

Agendamento agenda[50];
Utilizador users[30];
int frota_perc[20]={0};

int nveiculos_max=0;
int veiculos_ocupados=0;
int tempo=0;
long total_kms=0;
pthread_mutex_t cadeado = PTHREAD_MUTEX_INITIALIZER;
pid_t frota_pids[20]={0};
pid_t clientes_ativos[20]={0};

void termina_control(int sinal){
  (void)sinal;
  printf("\n\n (C) A encerrar o sistema. \n");
  for(int i=0;i<20;i++){
      if(frota_pids[i]>0){
      kill(frota_pids[i],SIGUSR1);
      }
  }
  for(int i=0;i<30;i++){
    if(users[i].online && users[i].pid > 0){
        kill(users[i].pid,SIGINT);
    }
  }
  
  unlink(PIPE_SERVIDOR);
  exit(0);
}

void *tarefa_dadosveiculo(void *arg)
{
  int fd_leitura = *(int*)arg;
  free(arg);
  char buffer[100];

  FILE *f = fdopen(fd_leitura,"r");
  if(!f) return NULL;

  while(fgets(buffer,100,f) !=NULL){
    printf("%s",buffer);
    
    int id_v,perc_v;
    if(sscanf(buffer,"(Veiculo %d) Viagem a decorrer:%d%%",&id_v,&perc_v)==2){
      if(id_v>0 && id_v<=20) frota_perc[id_v-1] =perc_v;
    }
  }
  fclose(f);
  return NULL;
}

void *tarefa_tempo(void *arg){
  (void)arg;
  char id_str[20],dist_str[20];
  
  while(1){
    sleep(1);    
    pthread_mutex_lock(&cadeado);
    tempo++;
    for(int i=0;i<50;i++){
      if(agenda[i].ativo==1&&agenda[i].dados.hora<=tempo){
        if(veiculos_ocupados<nveiculos_max){
          int slot=-1;  
          for(int j=0;j<20;j++){
            if(frota_pids[j]==0){
              slot=j;
              break;
            }
          }
        
          if(slot!=-1){
            veiculos_ocupados++;
            agenda[i].ativo=2;
            int pipe_sup[2];
            if(pipe(pipe_sup)==-1) perror("Pipe");
          
            pid_t pid=fork();
            if(pid==0){
              close(pipe_sup[0]);
              dup2(pipe_sup[1],STDOUT_FILENO);
              close(pipe_sup[1]);
              
              char pid_cli_str[20];
              sprintf(id_str,"%d",slot+1);
              sprintf(dist_str,"%d",agenda[i].dados.distancia);
              sprintf(pid_cli_str,"%d",agenda[i].dados.pid_cliente);
              
              if(agenda[i].dados.origem[0]  =='\0') strcpy(agenda[i].dados.origem,"Desconhecido");
              if(agenda[i].dados.destino[0]=='\0') strcpy(agenda[i].dados.destino,"Desconhecido");
            
              execl("./veiculo","./veiculo",id_str,agenda[i].dados.origem,agenda[i].dados.destino,dist_str,pid_cli_str,NULL);
              perror("Erro no execl");
              exit(1);
          }
          close(pipe_sup[1]);
          
          frota_pids[slot]=pid;
          clientes_ativos[slot]=agenda[i].dados.pid_cliente;
          
          pthread_t t_mon;
          int *arg_fd=malloc(sizeof(int));
          *arg_fd=pipe_sup[0];
          pthread_create(&t_mon,NULL,tarefa_dadosveiculo,arg_fd);
          pthread_detach(t_mon);
          
          printf("Taxi %d enviado no instante %d para %s (Agendado:%04d) \n",slot+1,tempo,agenda[i].dados.username,agenda[i].dados.hora);
        }
        }
      }
    }
    pthread_mutex_unlock(&cadeado);
  }
  return NULL;
}

void *trata_taxis(void *arg){
  (void)arg;
  pid_t pid_morto;
  int estado;
  char pipe_cliente[50];
  char msg[100];
  int fd_cli;

  while(1){
    pid_morto=wait(&estado);
    if(pid_morto<0){
      sleep(1);
      continue;
    }
    
    pthread_mutex_lock(&cadeado);
    veiculos_ocupados--;

    if((WIFEXITED(estado) && WEXITSTATUS(estado)==2)){
      strcpy(msg,"Viagem interrompida porque cancelaste");
    }
    else if((WIFEXITED(estado) && WEXITSTATUS(estado)==1)||WIFSIGNALED(estado)){
      strcpy(msg,"A sua viagem foi cancelada pelo ADMIN ");
    }else{
      strcpy(msg,"Chegou ao destino! Obrigado pela viagem.");
    }

    for(int i=0;i<20;i++){
      if(frota_pids[i]==pid_morto){
        frota_perc[i]=0;
        for(int k=0;k<50;k++){
          if(agenda[k].ativo==2&&agenda[k].dados.pid_cliente==clientes_ativos[i]){
            if(WEXITSTATUS(estado)!=1){
              total_kms+=agenda[k].dados.distancia;
            }
            agenda[k].ativo=0;
            break;
        }
        }
        if(clientes_ativos[i]>0){
          sprintf(pipe_cliente,PIPE_CLIENTE,clientes_ativos[i]);
          fd_cli=open(pipe_cliente, O_WRONLY);
          if(fd_cli !=-1){
            write(fd_cli,msg,strlen(msg)+1);
            close(fd_cli);
            printf("%s\n",msg);
            kill(clientes_ativos[i],SIGUSR2);
          }else{
            printf("Erro ao notificar cliente\n");
          }
        }
        frota_pids[i]=0;
        clientes_ativos[i]=0;
        break;
      }
    }
    pthread_mutex_unlock(&cadeado);
  }
  return NULL;
}

void *tarefa_atendec(void *arg){
  (void)arg;
  int fd,fd_resp;
  PedidoCliente p;
  char resposta_nome[50];
  RespostaServidor resp;
  
  if(mkfifo(PIPE_SERVIDOR,0666)==-1 && errno!=EEXIST){
    fd=open(PIPE_SERVIDOR,O_RDWR);
    exit(1);
  }
  fd=open(PIPE_SERVIDOR,O_RDWR);
  printf("Controlador ativo , a espera de agendamentos.\n");

  while(1){
    int n=read(fd,&p,sizeof(PedidoCliente));
    if(n==sizeof(PedidoCliente)){
      if(p.hora==-2){
        pthread_mutex_lock(&cadeado);
        int aceito=1;
        int slot_user=-1;
        
        for(int i=0;i<30;i++){
          if(users[i].online && strcmp(users[i].nome,p.username)==0){
            aceito=0;
            break;
          }
          if(!users[i].online && slot_user==-1) slot_user=i;
        }
        sprintf(resposta_nome,PIPE_CLIENTE,p.pid_cliente);
        fd_resp=open(resposta_nome,O_WRONLY);
        if(fd_resp!=-1){
          if(aceito && slot_user != -1){
            strcpy(users[slot_user].nome,p.username);
            users[slot_user].pid=p.pid_cliente;
            users[slot_user].online=1;
            
            strcpy(resp.mensagem,"Ok");
            resp.sucesso=1;
            printf("Login: Cliente %s PID:%d\n",p.username,p.pid_cliente);
          }else{
            strcpy(resp.mensagem,"Nome duplicado ou servidor cheio.");
            resp.sucesso=0;
            printf("Login Recusado: %s\n",p.username);
          }
          write(fd_resp,&resp,sizeof(RespostaServidor));
          close(fd_resp);
        }
        pthread_mutex_unlock(&cadeado);
        continue;
      }
      if(p.hora==-1){
        sprintf(resposta_nome,PIPE_CLIENTE,p.pid_cliente);
        fd_resp=open(resposta_nome,O_WRONLY);
        if(fd_resp!=-1){
          pthread_mutex_lock(&cadeado);
          int encontrou=0;
          for(int i=0;i<50;i++){
            if(agenda[i].ativo>0 && agenda[i].dados.pid_cliente==p.pid_cliente){
              char estado_str[20];
              if(agenda[i].ativo==1) strcpy(estado_str,"Pendente");
              else strcpy(estado_str,"Em viagem");
              snprintf(resp.mensagem,100,"ID:%d (%s) %04dh %s-      >%s",i+1,estado_str,agenda[i].dados.hora,agenda[i].dados.origem,agenda[i].dados.destino);
              resp.sucesso=1;
              write(fd_resp,&resp,sizeof(RespostaServidor));
              usleep(1000);
              encontrou=1;
            }
          }
          pthread_mutex_unlock(&cadeado);
          if(!encontrou) strcpy(resp.mensagem,"Nao tem agendamentos ativos");
          else strcpy(resp.mensagem,"Fim");
          write(fd_resp,&resp,sizeof(RespostaServidor));
          close(fd_resp);
        }
        continue;
      }
        
      if(p.hora==-3){
        int id_cancelar=p.distancia;
        int index=id_cancelar-1;
        
        printf("O cliente %s (PID:%d) cancelou a sua viagem (ID %d).\n",p.username,p.pid_cliente,id_cancelar);
        pthread_mutex_lock(&cadeado);
        if(index >=0 && index<50 && agenda[index].ativo>0 && agenda[index].dados.pid_cliente==p.pid_cliente){
          if(agenda[index].ativo==1){
            agenda[index].ativo=0;
            strcpy(resp.mensagem,"Agendamento cancelado");
          }
          else if(agenda[index].ativo==2){
            for(int j=0;j<20;j++){
              if(clientes_ativos[j]==p.pid_cliente){
                kill(frota_pids[j],SIGUSR1);
                break;
              }
            }
            agenda[index].ativo=0;
            strcpy(resp.mensagem,"Viagem interrompida");
          }
          resp.sucesso=1;
        }else{
          strcpy(resp.mensagem,"ID invalido ou nao pertence a si");
          resp.sucesso=0;
        }
        pthread_mutex_unlock(&cadeado);
        sprintf(resposta_nome,PIPE_CLIENTE,p.pid_cliente);
        fd_resp=open(resposta_nome,O_WRONLY);
        if(fd_resp!=-1){
          write(fd_resp,&resp,sizeof(RespostaServidor));
          close(fd_resp);
        }
        continue;
      }
      
      printf("Recebi pedido de agendamento do Cliente %s para as %dh para %s \n",p.username,p.hora,p.destino);
      pthread_mutex_lock(&cadeado);
      int guardado=0;
      for(int i=0;i<50;i++){
        if(agenda[i].ativo==0){
          agenda[i].ativo=1;
          agenda[i].dados=p;
          guardado=1;
          break;
        }
      }
      if(guardado){
        strcpy(resp.mensagem,"Agendamento confirmado.");
        resp.sucesso=1;
      }
      else{
      strcpy(resp.mensagem,"Agenda cheia!");
      resp.sucesso=0;
      }
      pthread_mutex_unlock(&cadeado);
      
      sprintf(resposta_nome,PIPE_CLIENTE,p.pid_cliente);
      fd_resp=open(resposta_nome,O_WRONLY);
      if(fd_resp!=-1){
        write(fd_resp,&resp,sizeof(RespostaServidor));
        close(fd_resp);
      }
    }
  }
  return NULL;
}

int main(){
  struct sigaction sa;
  pthread_t t_pipe,t_ges,t_timer;
  char comando[100];
  int id_cancelar;

  sa.sa_handler=termina_control;
  sa.sa_flags=0;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT,&sa,NULL);

  char *env =getenv("NVEICULOS");
  nveiculos_max=env ? atoi(env) :5;
  if(nveiculos_max>10){
    nveiculos_max=10;
  }
  memset(agenda,0,sizeof(agenda));
  memset(users,0,sizeof(users));

  printf("(C)Iniciada Frota Max :%d\n",nveiculos_max);
  
  pthread_create(&t_pipe,NULL,tarefa_atendec,NULL);
  pthread_create(&t_ges,NULL,trata_taxis,NULL);
  pthread_create(&t_timer,NULL,tarefa_tempo,NULL);

  while(1){
    printf("\n Menu do Controlador\n-'listar'\n-'utiliz'\n-'frota'\n-'km'\n-'cancelar <id>'\n-'hora'\n-'terminar'\n\n");
    scanf("%s",comando);
    
    if(strcmp(comando,"terminar")==0){
      termina_control(SIGINT);
    }
    else if(strcmp(comando,"utiliz")==0){
      pthread_mutex_lock(&cadeado);
      printf("Utilizadores Conectados.\n");
      for(int i=0;i<30;i++){
        if(users[i].online){
          char estado[20] = "Livre";
          for(int k=0; k<50;k++){
            if(agenda[k].ativo>0 && agenda[k].dados.pid_cliente==users[i].pid){
              if(agenda[k].ativo==1) strcpy(estado,"Aguardar");
              if(agenda[k].ativo==2) strcpy(estado,"Em viagem");
              break;
            }
          }
          printf("Utilizador: %s (PID %d) - Estado: %s\n",users[i].nome,users[i].pid,estado);
        }
      }
      pthread_mutex_unlock(&cadeado);
    }
    else if(strcmp(comando,"frota")==0){
      pthread_mutex_lock(&cadeado);
      printf("Progresso da frota\n");
      for(int i=0;i<nveiculos_max;i++){
        if(frota_pids[i]>0){
          printf("Taxi %d: %d%% completo\n",i+1,frota_perc[i]);
        }else{
            printf("Taxi %d: Parado\n",i+1);
        }
      }
      pthread_mutex_unlock(&cadeado);
    }
    else if(strcmp(comando,"listar")==0){
      pthread_mutex_lock(&cadeado);
      printf("Agendamentos\n");
      int vazia=1;
      for(int i=0;i<50;i++){
        if(agenda[i].ativo==1){
          printf("ID %d (Por fazer) %s @ %04d -> %s\n",i+1,agenda[i].dados.username,agenda[i].dados.hora,agenda[i].dados.destino);
          vazia=0;
        }else if(agenda[i].ativo==2){
          printf("ID %d (Em viagem) %s @ %04d (Taxi atribuido)\n",i+1,agenda[i].dados.username,agenda[i].dados.hora);
          vazia=0;
        }
      }
      if(vazia) printf("(Agenda Vazia)\n");
      pthread_mutex_unlock(&cadeado);
    }
    else if(strcmp(comando,"km")==0){
      pthread_mutex_lock(&cadeado);
      printf("Total KMs: %ld\n",total_kms);
      pthread_mutex_unlock(&cadeado);
    }
    else if(strcmp(comando,"cancelar")==0){
      if(scanf("%d",&id_cancelar)==1){
        int index=id_cancelar-1;
        pthread_mutex_lock(&cadeado);
        if(index>=0 && index<50 && agenda[index].ativo >0){
          if(agenda[index].ativo==2){
            for(int j=0;j<20;j++){
              if(clientes_ativos[j]==agenda[index].dados.pid_cliente){
                kill(frota_pids[j],SIGUSR1);
                break;
              }
            }
          }
          agenda[index].ativo=0;
          printf("Viagem com ID %d cancelada pelo Admin\n",id_cancelar);
        }else{
          printf("Taxi %d não existe ou não esta ativo.\n",id_cancelar);
        }
        pthread_mutex_unlock(&cadeado);
      }
      else{
        printf("Tens que indicar o ID 'cancelar 1' \n");
        while(getchar()!='\n');
      }
    }
    else if(strcmp(comando,"hora")==0){
      pthread_mutex_lock(&cadeado);
      printf("Tempo atual: %d segundos\n",tempo);
      pthread_mutex_unlock(&cadeado);
    }
    else{
      printf("Comando desconhecido: %s\n",comando);
      while(getchar()!='\n');
    }
  }
  return 0;
}


