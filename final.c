#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#define MAX_NAME_LENGTH 30
#define MAX_BUFFER 100
#define SHARED_MEM_SIZE 4096

// Structures pour la mémoire partagée et les arguments de chat
typedef struct {
   char buffer[SHARED_MEM_SIZE];  
   size_t write_pos;             
   size_t read_pos;
   bool is_full;                 
} SharedMemory;

typedef struct {
   char utilisateur[MAX_NAME_LENGTH + 1];
   char destinataire[MAX_NAME_LENGTH + 1];
   bool _bot;
   bool _manuel;
} ChatArgs;

char read_path[72];
char write_path[72];
SharedMemory *shared_mem = NULL;
pid_t child_pid = -1;
pid_t parent_pid = -1;

// Variable globale pour les arguments de chat
ChatArgs global_args;

// Déclaration de la fonction avant son utilisation
void read_and_print_shared_memory(const ChatArgs args);

// Gestionnaire de signaux
void signal_handler(int sig) {
   if (sig == SIGTERM) {
      unlink(read_path);
      unlink(write_path); 
      exit(0);
   } 
   else if (sig == SIGINT) {
      // Appeler la fonction pour lire et afficher les messages non lus
      if (shared_mem) {
         read_and_print_shared_memory(global_args);  // Utiliser la variable globale
      }
   }
}

// Fonction pour valider le nom d'utilisateur
bool valid_name(const char *name) {
   const char *invalid_char = "/-[]";
   
   if (strlen(name) > MAX_NAME_LENGTH - 1) {
      fprintf(stderr, "Nom d'utilisateur doit avoir moins de 30 caractères.\n");
      exit(2);
   }
   
   for (size_t i = 0; i < strlen(name); i++) {
      if (strchr(invalid_char, name[i]) != NULL) {
         fprintf(stderr, "Nom d'utilisateur ne doit pas contenir ces caractères: '/', '-', '[', ']'\n");
         exit(3);
      }
   }
   
   if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
      fprintf(stderr, "Nom d'utilisateur ne peut pas être: '.' ou '..'\n");
      exit(3);
   }
   return true;
}

// Fonction pour analyser les arguments
void parse_arguments(int argc, char *argv[], ChatArgs *args) {
   if (argc < 3) {
      fprintf(stderr, "chat pseudo_utilisateur pseudo_destinataire [--bot] [--manuel]\n");
      exit(1);
   }

   strncpy(args->utilisateur, argv[1], MAX_NAME_LENGTH);
   valid_name(args->utilisateur);
   strncpy(args->destinataire, argv[2], MAX_NAME_LENGTH);
   valid_name(args->destinataire);

   args->_bot = false;
   args->_manuel = false;
   for (int i = 3; i < argc; i++) {
      if (strcmp(argv[i], "--bot") == 0) {
         args->_bot = true;
      } else if (strcmp(argv[i], "--manuel") == 0) {
         args->_manuel = true;
      }
   }
}

// Fonction pour créer un pipe
void create_pipe(const char *path) {
   if (mkfifo(path, 0666) == -1 && errno != EEXIST) {
      perror("Erreur lors de la création des pipes.\n");
      exit(4);
   }
}

// Fonction pour écrire dans la mémoire partagée
void write_to_shared_memory(const char *message) {
   size_t message_len = strlen(message);
   
   if (message_len + 1 > SHARED_MEM_SIZE) {
      fprintf(stderr, "Message trop long pour être stocké.\n");
      return;
   }

   if (shared_mem->is_full || shared_mem->write_pos + message_len + 1 > SHARED_MEM_SIZE) {
      shared_mem->is_full = true;
   }

   if (!shared_mem->is_full) {
      strncpy(shared_mem->buffer + shared_mem->write_pos, message, message_len + 1);
      shared_mem->write_pos += message_len + 1;
   }
}

// Fonction pour lire et afficher la mémoire partagée
void read_and_print_shared_memory(const ChatArgs args) {
    // Tant qu'il y a des messages à lire dans la mémoire partagée
    while (shared_mem->read_pos < shared_mem->write_pos) {
        // Récupération du message à lire
        char *message = shared_mem->buffer + shared_mem->read_pos;
        
        // Affichage du message avec ou sans le mode bot
        if (!args._bot) {
            printf("[\x1B[4m%s\x1B[0m] %s", args.destinataire, message);
        } else {
            printf("[%s] %s", args.destinataire, message);
        }
        fflush(stdout);

        // Avancer la position de lecture après avoir affiché le message
        shared_mem->read_pos += strlen(message) + 1;
    }

    // Réinitialiser les positions de lecture et d'écriture après avoir tout affiché
    if (shared_mem->read_pos >= shared_mem->write_pos) {
        shared_mem->write_pos = 0;
        shared_mem->read_pos = 0;
        shared_mem->is_full = false;  // Réinitialiser l'état de la mémoire partagée
    }
}

// Processus de lecture
void read_process(ChatArgs args, const char *read_path, pid_t parent_pid) {
   int fd_read = open(read_path, O_RDONLY);
   if (fd_read == -1) {
      perror("Erreur lors de l'ouverture du pipe de lecture");
      exit(EXIT_FAILURE);
   }
   char buffer[MAX_BUFFER];
    
   while (1) {
      ssize_t bytes_read = read(fd_read, buffer, sizeof(buffer));
      
      if (bytes_read > 0) {
         buffer[bytes_read] = '\0';
         if (args._manuel || (args._manuel && args._bot)) {
             printf("\a");
             write_to_shared_memory(buffer);
         } else {
             if (args._bot) {
                 printf("[%s] %s", args.destinataire, buffer);
                 fflush(stdout);
             } else {
                 printf("[\x1B[4m%s\x1B[0m] %s", args.destinataire, buffer);
             }
            fflush(stdout);
         }
      }
      else if (bytes_read == 0) {
         kill(parent_pid, SIGTERM);
         break;
      }
    }   
    close(fd_read);
}

// Processus d'écriture
void write_process(ChatArgs args, const char *write_path, pid_t child_pid) {
    int fd_write = open(write_path, O_WRONLY);
    if (fd_write == -1) {
        perror("Erreur dans le pipe d'écriture.\n");
        exit(4);
    }

    char buffer[MAX_BUFFER];
    while (1) {

        if (fgets(buffer, MAX_BUFFER, stdin) == NULL) {
            break;
        }

        // Écriture dans le pipe
        ssize_t bytes_written = write(fd_write, buffer, strlen(buffer));
        if (bytes_written == -1) {
            break;
        }

        if (!args._bot){
            printf("[\x1B[4m%s\x1B[0m] %s", args.utilisateur, buffer);
        }
        fflush(stdout);

        // Gestion du mode manuel (affichage des messages en mémoire partagée)
        if (args._manuel) {
            read_and_print_shared_memory(args);
        }
    }
   close(fd_write);
   kill(child_pid, SIGTERM);
}

// Fonction principale
int main(int argc, char *argv[]) {
   signal(SIGTERM, signal_handler);
   signal(SIGINT, signal_handler);
   
   ChatArgs args;
   
   parse_arguments(argc, argv, &args);
   
   // Affecter les arguments globaux pour pouvoir les utiliser dans le gestionnaire de signaux
   global_args = args;
   
   snprintf(read_path, sizeof(read_path), "/tmp/%s-%s.chat", args.destinataire, args.utilisateur);
   snprintf(write_path, sizeof(write_path), "/tmp/%s-%s.chat", args.utilisateur, args.destinataire);

   create_pipe(read_path);
   create_pipe(write_path);
   
   if (args._manuel) {
      shared_mem = mmap(NULL, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
      if (shared_mem == MAP_FAILED) {
         perror("Erreur lors de la création de la mémoire partagée");
         exit(5);
      }
      shared_mem->write_pos = 0;
      shared_mem->read_pos = 0;
      shared_mem->is_full = false;
   }

   pid_t parent_pid = getpid();
   pid_t child_pid = fork();

   if (child_pid == -1) {
      perror("Erreur de fork.\n");
      exit(4);
   }

   if (child_pid == 0) {
      read_process(args, read_path, parent_pid);
   } else {
      write_process(args, write_path, child_pid);
   }

   if (args._manuel) {
      munmap(shared_mem, sizeof(SharedMemory));
   }

   unlink(read_path);
   unlink(write_path);
   
   return 0;
}
