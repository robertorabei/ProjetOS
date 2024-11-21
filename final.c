#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#define MAX_USERNAME_LENGTH 30
#define BUFFER_SIZE 256
#define PIPE_DIR "/tmp"


// STRUCTURE GENERALE DU PROGRAMME CHAT
typedef struct {
    char pseudo_utilisateur[51];
    char pseudo_destinataire[51];
    bool is_bot;
    bool is_manual;
} ChatArgs;

// INTIALISATION DES VARIABLES GLOBALES
pid_t child_pid = 0;


// HANDLERS DES SIGNAUX -- A FAIRE : SIGINT quand signalé plusieurs fois termine le programme 
void handle_sigint(int sig) {
    // Une fois signalé affiche les messages en attente
}
void handle_sigterm(int sig) {
    // Gestion du signal SIGTERM (terminaison propre) quand "close" est écrit
    exit(0); // Quitter proprement
}

// PARTIE FONCTIONS DU PROGRAMME CHAT
bool is_valid_username(const char *username) {
    const char *invalid_char = "/-[]";

    if (strlen(username) > 30) {
        return false;
    }

    for (size_t i = 0; i < strlen(username); i++) {
        if (strchr(invalid_char, username[i]) != NULL) {
            return false;
        }
    }

    if (strcmp(username, ".") == 0 || strcmp(username, "..") == 0) {
        return false;
    }

    return true;
}
void parse_arguments(int argc, char *argv[], ChatArgs *args) {
    if (argc < 3) {
        fprintf(stderr, "Usage: chat pseudo_utilisateur pseudo_destinataire [--bot] [--manuel]\n");
        exit(1);
    }


    strncpy(args->pseudo_utilisateur, argv[1], 50);
    args->pseudo_utilisateur[50] = '\0'; 
    strncpy(args->pseudo_destinataire, argv[2], 50);
    args->pseudo_destinataire[50] = '\0'; 

    if (!is_valid_username(args->pseudo_utilisateur) || !is_valid_username(args->pseudo_destinataire)) {
        fprintf(stderr, "Error: Invalid username. Usernames cannot contain '/', '-', '[', ']' nor be '.', '..' and should be less than 30 bytes.\n");
        exit(3);
    }

    args->is_bot = false;
    args->is_manual = false;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--bot") == 0) {
            args->is_bot = true;
        } else if (strcmp(argv[i], "--manuel") == 0) {
            args->is_manual = true;
        }
    }
}


// PARTIE CREATIONS ET FONCTIONNEMENT DES
void create_fifo(const char *path) {
    if (mkfifo(path, 0666) == -1 && errno != EEXIST) {
        perror("Error creating FIFO");
        exit(EXIT_FAILURE);
    }
}
void read_process(ChatArgs args, const char *read_path) {
   // Processus enfant : lecture des messages
     int fd_read = open(read_path, O_RDONLY);
     if (fd_read == -1) {
         perror("Error in read path");
         exit(EXIT_FAILURE);
     }
     char buffer[BUFFER_SIZE];
     while (1) {
         ssize_t bytes_read = read(fd_read, buffer, BUFFER_SIZE - 1);
         if (bytes_read > 0) {
             buffer[bytes_read] = '\0';
             
             if (strcmp(buffer, "close") == 0) {
                 printf("Chat session ended.\n");
                 kill(getppid(), SIGTERM); // Informer le processus parent de terminer
                 break;
             }
             
             if (args.is_bot)
                 printf("[%s]: %s\n", args.pseudo_destinataire, buffer);
             else
                 printf("[\x1B[4m%s\x1B[0m]: %s\n", args.pseudo_destinataire, buffer);
         }
     }
     close(fd_read);
}

void write_process(ChatArgs args, const char *write_path, pid_t child_pid) {
    // Processus parent : écriture des messages
    int fd_write = open(write_path, O_WRONLY);
    if (fd_write == -1) {
        perror("Error in write path");
        kill(child_pid, SIGTERM); // Demander à l'enfant de se terminer
        exit(EXIT_FAILURE);
    }
    char buffer[BUFFER_SIZE];
    while (1) {
        if (fgets(buffer, BUFFER_SIZE, stdin) != NULL) {
            size_t len = strlen(buffer);
            if (buffer[len - 1] == '\n') {
                buffer[len - 1] = '\0';
            }
            write(fd_write, buffer, strlen(buffer));
            
            if (strcmp(buffer, "close") == 0) {
                printf("Chat session ended.\n");
                kill(child_pid, SIGTERM); // Demander à l'enfant de se terminer
                break;}
            
            if (!args.is_bot) {
                printf("[\x1B[4m%s\x1B[0m]: %s\n", args.pseudo_utilisateur, buffer);
                fflush(stdout);
            }
        }
    }
    close(fd_write);
}


int main(int argc, char* argv[]) {
    ChatArgs args;
    parse_arguments(argc, argv, &args);

    char read_path[100];
    char write_path[100];
    snprintf(write_path, sizeof(write_path), "%s/%s-%s.chat", PIPE_DIR, args.pseudo_utilisateur, args.pseudo_destinataire);
    snprintf(read_path, sizeof(read_path), "%s/%s-%s.chat", PIPE_DIR, args.pseudo_destinataire, args.pseudo_utilisateur);

    create_fifo(write_path);
    create_fifo(read_path);

    printf("Chat started between %s and %s.\n", args.pseudo_utilisateur, args.pseudo_destinataire);

    signal(SIGINT, handle_sigint);   // Gérer Ctrl+C
    signal(SIGTERM, handle_sigterm); // Gérer une terminaison propre

    child_pid = fork();
    if (child_pid == -1) {
        perror("Erreur de fork");
        return EXIT_FAILURE;
    }

    if (child_pid == 0) {
        read_process(args, read_path);
    } else {
        write_process(args, write_path, child_pid);
    }

    return EXIT_SUCCESS;
}
