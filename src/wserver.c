#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include "request.h"
#include "io_helper.h"

// Max buffer size
#define MAXBUF (8192)

char default_root[] = ".";

// Put function for adding item to buffer
void put(int);
// Get function for removing item from buffer
int get();
// Consumer thread function
void* consumer(void* arg);
// Lock wrapper function
void Pthread_mutex_lock(pthread_mutex_t*);
// Unlock wrapper function
void Pthread_mutex_unlock(pthread_mutex_t*);

// Initialize a default buffer size
int default_buffer_size;
// The shared resource (an int array of size MAX)
int * buffer;
// Initialize a default thread value
int default_threads;
// Keeps track of items in buffer
// When we add an item in put() we increment by 1
// When we remove an item in get() we decrement by 1.
int fill_ptr = 0;
int use_ptr = 0;
int count = 0;

// Lock
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; // lock
// Condition variable for signaling empty buffer
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;
// Condition variable for signaling buffer is not empty
pthread_cond_t fill = PTHREAD_COND_INITIALIZER;

int main(int argc, char *argv[]) {
    int c;
    char *root_dir = default_root;
    // Default port
    int port = 10000;
    // Default buffer size
    default_buffer_size = MAXBUF;
    default_threads = 10;

    while ((c = getopt(argc, argv, "d:p:t:b:")) != -1)
	     switch (c) {
	        case 'd':
        	    root_dir = optarg;
        	    break;
        	case 'p':
        	    port = atoi(optarg);
        	    break;
          case 't':
              default_threads = atoi(optarg);
              if (default_threads < 1) {
                exit(EXIT_FAILURE);
              }
              break;
          case 'b':
              default_buffer_size = atoi(optarg);
              if (default_buffer_size < 1) {
                exit(EXIT_FAILURE);
              }
              break;
        	default:
        	    fprintf(stderr, "usage: wserver [-d basedir] [-p port] [-t threads] [-b buffersize]\n");
        	    exit(1);
  	}
    // Initialize buffer and thread pool using user gathered values
    // or defaults if not specified
    buffer = malloc(default_buffer_size * sizeof(int));
    pthread_t threads[default_threads];

    // Initialize worker/consumer threads to handle connections
    for(int i = 0; i < default_threads; i++) {
      pthread_create(&threads[i], NULL, consumer, (void*)&default_buffer_size);
    }

    // Run out of this directory
    chdir_or_die(root_dir);
    // Initiate port listening
    int listen_fd = open_listen_fd_or_die(port);
    while (1) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        int *conn_fd = accept_or_die(listen_fd, (sockaddr_t *) &client_addr, (socklen_t *) &client_len);
        printf("Adding connection to buffer.\n");
        // Put method handles the lock, waiting, unlock, and signaling.
        put(conn_fd);
    }
    return 0;
}

// Wrapper around pthread_mutex_lock that checks if it fails
void Pthread_mutex_lock(pthread_mutex_t* m) {
  int rc = pthread_mutex_lock(m);
  if(rc != 0) {
    fprintf(stderr, "pthread_mutex_lock failed with return value %d\n", rc);
    exit(EXIT_FAILURE);
  }
}

// Wrapper around Pthread_mutex_unlock that checks if it fails
void Pthread_mutex_unlock(pthread_mutex_t* m) {
  int rc = pthread_mutex_unlock(m);
  if (rc != 0) {
    fprintf(stderr, "pthread_mutex_unlock failed with return value %d\n", rc);
    exit(EXIT_FAILURE);
  }
}

// Add an item to the shared resource (the array buffer)
void put(int value) {
  // Lock before enqueueing
  Pthread_mutex_lock(&mutex);
  // If the buffer is full, wait
  if (count == default_buffer_size) {
    printf("==>> Buffer is full, waiting...\n");
    pthread_cond_wait(&empty, &mutex);
  }
  // Add request to buffer
  buffer[fill_ptr] = value;
  fill_ptr = (fill_ptr + 1) % default_buffer_size;
  count++;
  printf("Number of items in the buffer: %d\n", count);
  // Signal that the buffer is no longer empty.
  pthread_cond_signal(&fill);
  // Unlock so other threads can utilize cpu.
  Pthread_mutex_unlock(&mutex);
}

// Remove a request from the shared resource (the array buffer)
int get() {
  // Get request from buffer
  int tmp = buffer[use_ptr];
  use_ptr = (use_ptr + 1) % default_buffer_size;
  count--;
  printf("Number of items in the buffer: %d\n", count);
  // Return the request from buffer
  return tmp;
}

// The consumer thread: removes items from the buffer and processes them
void* consumer(void* arg) {
// Thread runs indefinitely
  while(1) {
    // Lock before accessing buffer
    Pthread_mutex_lock(&mutex);
    // While the buffer is full, wait
    while(count == 0) {
      printf("==>> Buffer is empty. Waiting for data to arrive.\n");
      pthread_cond_wait(&fill, &mutex);
    }
    // Retrieve job from the buffer using get function.
    int *tmp = get();
    // Signal that the buffer might no longer be full
    pthread_cond_signal(&empty);
    // Unlock
    Pthread_mutex_unlock(&mutex);
    // If connection exists
    if(tmp != NULL) {
      // Handle the connection
      request_handle(tmp);
    }
    // close the connection
    close_or_die(tmp);

    printf("Processing item %d\n", tmp);
  }
  return NULL;
}
