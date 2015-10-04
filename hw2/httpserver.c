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
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

#include "libhttp.h"

#define LIBHTTP_REQUEST_MAX_SIZE 8192

/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;

void send_file_request (int fd, char *filename) {
  struct stat buf;
  stat(filename, &buf);
  char *contentType = http_get_mime_type(filename);
  FILE *fp = fopen(filename, "r");
  char *data = (char *) malloc(buf.st_size + 1);
  int length = fread(data, 1, buf.st_size, fp);
  char slength[10];
  sprintf(slength, "%d", length);
  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", contentType);
  http_send_header(fd, "Content-Length", slength);
  http_end_headers(fd);
  http_send_data(fd, data, length);

  free(data);
}

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

  /* YOUR CODE HERE (Feel free to delete/modify the existing code below) */
  struct http_request *request = http_request_parse(fd);
  struct stat buf;
  char *path = (char *) malloc(strlen(server_files_directory) + strlen(request->path) + 1);
  strcpy(path, server_files_directory);
  strcat(path, request->path);
  stat(path, &buf);
  if (S_ISREG(buf.st_mode)) {
    send_file_request (fd, path);
  } else if (S_ISDIR(buf.st_mode)) {
    char *index;
    if (path[strlen(path) - 1] == '/') {
      index = "index.html";
    } else {
      index = "/index.html";
    }
    char *index_path = (char *) malloc(strlen(path) + strlen(index) + 1);
    strcpy(index_path, path);
    strcat(index_path, index);
    struct stat ibuf;
    stat(index_path, &ibuf);
    if (S_ISREG(ibuf.st_mode)) {
      send_file_request(fd, index_path);
    } else {
      DIR *dir = opendir(path);
      struct dirent *dp;
      int index = 0;
      char *new;
      dp = readdir(dir);
      int nextlen = sizeof("<a href=\"") + strlen(dp->d_name) + sizeof("\">") + sizeof(dp->d_name) + sizeof("</a>");
      char *parent = "<a href=\"../\">Parent directory</a>";
      char *html = (char *) malloc(strlen(parent) + nextlen + 1);
      strcpy(html, parent);
      int size = strlen(parent) + nextlen;
      while (dp != NULL) {
        nextlen = sizeof("<a href=\"") + strlen(dp->d_name) + sizeof("\">") + sizeof(dp->d_name) + sizeof("</a>");
        index = index + nextlen;
        if (size < index) {
          new = (char *) malloc(strlen(html) + nextlen + 1);
          strcpy(new, html);
          free(html);
          html = new;
          size = strlen(html) + nextlen;
        }
        strcat(html, "<a href=\"");
        strcat(html, dp->d_name);
        strcat(html, "\">");
        strcat(html, dp->d_name);
        strcat(html, "</a>");
        dp = readdir(dir);
      }

      http_start_response(fd, 200);
      http_send_header(fd, "Content-type", "text/html");
      http_end_headers(fd);
      http_send_string(fd, html);
      closedir(dir);
      free(index_path);
      free(html);
    }
  } else {
    http_start_response(fd, 404);
    http_send_header(fd, "Content-type", "text/html");
    http_end_headers(fd);
  }
  free(path);
  close(fd);
}

int read_write_request(int fd1, int fd2) {
  char *read_buffer = malloc(LIBHTTP_REQUEST_MAX_SIZE + 1);
  int bytes_read;
  int bytes_sent;
  bytes_read = read(fd1, read_buffer, LIBHTTP_REQUEST_MAX_SIZE);
  read_buffer[bytes_read] = '\0'; /* Always null-terminate. */
  if (bytes_read == -1) {
    free(read_buffer);
    close(fd2);
    return 1;
  }
  bytes_sent = write(fd2, read_buffer, bytes_read);
  if (bytes_sent == -1) {
    free(read_buffer);
    close(fd2);
    return 1;
  }
  free(read_buffer);
  return 0;
}

void coordinate_proxy_requests(int fd1, int fd2) {
  fd_set readfds;
  int rc;
  int maxfd = (fd1 > fd2) ? fd1 : fd2;
  while (1) {
    FD_ZERO(&readfds);
    FD_SET(fd1, &readfds);
    FD_SET(fd2, &readfds);
    rc = select(maxfd, &readfds, NULL, NULL, NULL);
    if (FD_ISSET(fd1, &readfds)) {
      rc = read_write_request(fd1, fd2);
      if (rc > 0) {
        break;
      }
    }
    if (FD_ISSET(fd2, &readfds)) {
      rc = read_write_request(fd2, fd1);
      if (rc > 0) {
        break;
      }
    }
  }
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

  /* YOUR CODE HERE */
  struct hostent *host = gethostbyname(server_proxy_hostname);
  int upstream = socket(host->h_addrtype, SOCK_STREAM, 0);
  if (upstream == -1) {
    perror("Failed to create a new socket");
    exit(errno);
  }
  struct sockaddr_in saddr;
  size_t saddr_length = sizeof(saddr);
  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_port = htons(server_proxy_port);
  saddr.sin_addr.s_addr = ((struct in_addr*)(host->h_addr))->s_addr;
  saddr.sin_family = host->h_addrtype;
  int conn = connect(upstream, (struct sockaddr *) &saddr, saddr_length);
  if (conn == -1) {
    perror("Failed to connect to socket");
    exit(errno);
  }

  coordinate_proxy_requests(fd, upstream);
  close(upstream);
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
  pid_t pid;

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

    pid = fork();
    if (pid > 0) {
      close(client_socket_number);
    } else if (pid == 0) {
      // Un-register signal handler (only parent should have it)
      signal(SIGINT, SIG_DFL);
      close(*socket_number);
      request_handler(client_socket_number);
      close(client_socket_number);
      exit(EXIT_SUCCESS);
    } else {
      perror("Failed to fork child");
      exit(errno);
    }
  }

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
  "Usage: ./httpserver --files www_directory/ --port 8000\n"
  "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  signal(SIGINT, signal_callback_handler);

  /* Default settings */
  server_port = 8000;
  server_files_directory = malloc(1024);
  getcwd(server_files_directory, 1024);
  server_proxy_hostname = "inst.eecs.berkeley.edu";
  server_proxy_port = 80;

  void (*request_handler)(int) = handle_files_request;

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
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
