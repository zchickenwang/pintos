#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

#include "libhttp.h"
#include "wq.h"

/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
wq_t work_queue;
int num_threads;
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;

void serve_request(void (*request_handler)(int));

struct proxy_party {
  int fd_to;
  int fd_from;
};

void serve_proxy(void *aux);

void send_file(int fd, char *path, struct stat *buffer);
bool send_index_if_exists(int fd, char *path);
void send_ls(int fd, char *path);

/*
 * Reads an HTTP request from stream (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a list
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 */
void handle_files_request(int fd) {
  struct http_request *request = http_request_parse(fd);

  // server_files_directory + request->path
  char path[100];
  if (server_files_directory != NULL) {
    strcpy(path, server_files_directory);
  } else {
    dprintf(fd, "File directory argument must not be null!\n");
    http_start_response(fd, 404);
    return;
  }
  if (request->path != NULL) {
    strcat(path, request->path);
  }
  
  struct stat buffer;
  if (stat(path, &buffer) < 0) 
  { // 404
    http_start_response(fd, 404);
  }

  if (S_ISREG(buffer.st_mode))
  { // file
    send_file(fd, path, &buffer);
  }

  if (S_ISDIR(buffer.st_mode)) // TODO do we need to add ending slash
  {
    bool sent = send_index_if_exists(fd, path);
    if (!sent)
    {
      send_ls(fd, path);
    }
  }
}

void send_file(int fd, char *path, struct stat *buffer) {
  // load content
  char *data = 0;
  long length = 0;
  FILE *f = fopen(path, "rb");

  if (f)
  {
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc(length);
    if (data)
    {
      fread(data, 1, length, f);
    }
    fclose (f);
  }

  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", http_get_mime_type(path));
  char content_size[sizeof(buffer->st_size)];
  sprintf(content_size, "%d", (int) buffer->st_size);
  http_send_header(fd, "Content-Length", content_size);
  http_end_headers(fd);
  http_send_data(fd, data, length);
}

bool send_index_if_exists(int fd, char *path) {
  // if path is directory, check if index.html exists
  char index_path[strlen(path) + 11];
  strcpy(index_path, path);
  strcat(index_path, "index.html");

  struct stat index_buffer;
  int status = stat(index_path, &index_buffer);
  if (status < 0) {
    return false;
  }
  if (S_ISREG(index_buffer.st_mode)) {
      // send index.html
      char *index_html = 0;
      long length;
      FILE *f = fopen(index_path, "rb");

      if (f) {
        fseek(f, 0, SEEK_END);
        length = ftell(f);
        fseek(f, 0, SEEK_SET);
        index_html = malloc(length);
        if (index_html) {
          fread(index_html, 1, length, f);
        }
        fclose(f);
      }

      http_start_response(fd, 200);
      http_send_header(fd, "Content-Type", http_get_mime_type(index_path));
      char content_size[sizeof(index_buffer.st_size)];
      sprintf(content_size, "%d", (int) index_buffer.st_size);
      http_send_header(fd, "Content-Length", content_size);
      http_end_headers(fd);
      http_send_string(fd, index_html);
      return true;
  }
  return false;
}

void send_ls(int fd, char *path) {
  // if path is directory but no index.html, send ls + parent link
  DIR *dir;
  struct dirent *entry;
  char ls_html[8192];
  memset(ls_html, 0, 8192);
  dir = opendir(path);

  // parent directory
  strcat(ls_html, "<a href=\"../\">Parent Directory</a>\n");

  // children
  do {
    if ((entry = readdir(dir)) != NULL)
    {
      char curr_link[strlen(entry->d_name) + 3];
      strcpy(curr_link, "./");
      strcat(curr_link, entry->d_name);

      strcat(ls_html, "<a href=\"");
      strcat(ls_html, curr_link);
      strcat(ls_html, "\">");
      strcat(ls_html, entry->d_name);
      strcat(ls_html, "</a>\n");
    }
  } while (entry != NULL);

  closedir(dir);

  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", "text/html");
  char content_size[strlen(ls_html)];
  sprintf(content_size, "%d", (int) strlen(ls_html));
  http_send_header(fd, "Content-Length", content_size);
  http_end_headers(fd);
  http_send_string(fd, ls_html);
}

/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target. HTTP requests from the client (fd) should be sent to the
 * proxy target, and HTTP responses from the proxy target should be sent to
 * the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */
void handle_proxy_request(int fd) {

  /*
  * The code below does a DNS lookup of server_proxy_hostname and 
  * opens a connection to it. Please do not modify.
  */

  struct sockaddr_in target_address;
  memset(&target_address, 0, sizeof(target_address));
  target_address.sin_family = AF_INET;
  target_address.sin_port = htons(server_proxy_port);

  struct hostent *target_dns_entry = gethostbyname2(server_proxy_hostname, AF_INET);

  int client_socket_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (client_socket_fd == -1) {
    fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
    exit(errno);
  }

  if (target_dns_entry == NULL) {
    fprintf(stderr, "Cannot find host: %s\n", server_proxy_hostname);
    exit(ENXIO);
  }

  char *dns_address = target_dns_entry->h_addr_list[0];

  memcpy(&target_address.sin_addr, dns_address, sizeof(target_address.sin_addr));
  int connection_status = connect(client_socket_fd, (struct sockaddr*) &target_address,
      sizeof(target_address));

  if (connection_status < 0) {
    /* Dummy request parsing, just to be compliant. */
    http_request_parse(fd);

    http_start_response(fd, 502);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    http_send_string(fd, "<center><h1>502 Bad Gateway</h1><hr></center>");
    return;
  }

  struct proxy_party p2c;
  p2c.fd_to = client_socket_fd;
  p2c.fd_from = fd;
  struct proxy_party c2p;
  c2p.fd_to = fd;
  c2p.fd_from = client_socket_fd;

  pthread_t p2c_thread;
  pthread_t c2p_thread;
  pthread_create(&p2c_thread, NULL, (void *) &serve_proxy, (void *) &p2c);
  pthread_create(&c2p_thread, NULL, (void *) &serve_proxy, (void *) &c2p);
  pthread_join(p2c_thread, NULL);
  pthread_join(c2p_thread, NULL);
}

void serve_proxy(void *aux) {
  struct proxy_party *pp = (struct proxy_party *) aux;
  char buffer[8192];
  while (read(pp->fd_from, buffer, sizeof(buffer)) > 0) {
    http_send_data(pp->fd_to, buffer, strlen(buffer));
    memset(buffer, 0, sizeof(buffer));
  }
}

void init_thread_pool(int num_threads, void (*request_handler)(int)) {
  wq_init(&work_queue);
  for (int i = 0; i < num_threads; i++) {
    pthread_t thread;
    pthread_create(&thread, NULL, (void *) &serve_request, request_handler);
  }
}

void serve_request(void (*request_handler)(int)) {
  while (1) {
    // pop from wq
    int client_fd = wq_pop(&work_queue);

    // serve
    request_handler(client_fd);

    // close client_socket_number
    close(client_fd);
  }
}

/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *socket_number, void (*request_handler)(int)) {

  struct sockaddr_in server_address, client_address;
  size_t client_address_length = sizeof(client_address);
  int client_socket_number;

  *socket_number = socket(PF_INET, SOCK_STREAM, 0);
  if (*socket_number == -1) {
    perror("Failed to create a new socket");
    exit(errno);
  }

  int socket_option = 1;
  if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
        sizeof(socket_option)) == -1) {
    perror("Failed to set socket options");
    exit(errno);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(server_port);

  if (bind(*socket_number, (struct sockaddr *) &server_address,
        sizeof(server_address)) == -1) {
    perror("Failed to bind on socket");
    exit(errno);
  }

  if (listen(*socket_number, 1024) == -1) {
    perror("Failed to listen on socket");
    exit(errno);
  }

  printf("Listening on port %d...\n", server_port);

  init_thread_pool(num_threads, request_handler);

  while (1) {
    client_socket_number = accept(*socket_number,
        (struct sockaddr *) &client_address,
        (socklen_t *) &client_address_length);
    if (client_socket_number < 0) {
      perror("Error accepting socket");
      continue;
    }

    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);

    wq_push(&work_queue, client_socket_number);

    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);
  }

  shutdown(*socket_number, SHUT_RDWR);
  close(*socket_number);
}

int server_fd;
void signal_callback_handler(int signum) {
  printf("Caught signal %d: %s\n", signum, strsignal(signum));
  printf("Closing socket %d\n", server_fd);
  if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
  exit(0);
}

char *USAGE =
  "Usage: ./httpserver --files www_directory/ --port 8000 [--num-threads 5]\n"
  "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000 [--num-threads 5]\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  signal(SIGINT, signal_callback_handler);

  /* Default settings */
  server_port = 8000;
  void (*request_handler)(int) = NULL;

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp("--files", argv[i]) == 0) {
      request_handler = handle_files_request;
      free(server_files_directory);
      server_files_directory = argv[++i];
      if (!server_files_directory) {
        fprintf(stderr, "Expected argument after --files\n");
        exit_with_usage();
      }
    } else if (strcmp("--proxy", argv[i]) == 0) {
      request_handler = handle_proxy_request;

      char *proxy_target = argv[++i];
      if (!proxy_target) {
        fprintf(stderr, "Expected argument after --proxy\n");
        exit_with_usage();
      }

      char *colon_pointer = strchr(proxy_target, ':');
      if (colon_pointer != NULL) {
        *colon_pointer = '\0';
        server_proxy_hostname = proxy_target;
        server_proxy_port = atoi(colon_pointer + 1);
      } else {
        server_proxy_hostname = proxy_target;
        server_proxy_port = 80;
      }
    } else if (strcmp("--port", argv[i]) == 0) {
      char *server_port_string = argv[++i];
      if (!server_port_string) {
        fprintf(stderr, "Expected argument after --port\n");
        exit_with_usage();
      }
      server_port = atoi(server_port_string);
    } else if (strcmp("--num-threads", argv[i]) == 0) {
      char *num_threads_str = argv[++i];
      if (!num_threads_str || (num_threads = atoi(num_threads_str)) < 1) {
        fprintf(stderr, "Expected positive integer after --num-threads\n");
        exit_with_usage();
      }
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  if (server_files_directory == NULL && server_proxy_hostname == NULL) {
    fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                    "                      \"--proxy [HOSTNAME:PORT]\"\n");
    exit_with_usage();
  }

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
