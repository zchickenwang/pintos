#include <stdlib.h>
#include <stdio.h>
#include "wq.h"
#include "utlist.h"

/* Initializes a work queue WQ. */
void wq_init(wq_t *wq) {
  wq->size = 0;
  wq->head = NULL;
  pthread_mutex_init(&(wq->mutex), NULL);
  pthread_cond_init(&(wq->cond), NULL);
}

/* Remove an item from the WQ. This function should block until there
 * is at least one item on the queue. */
int wq_pop(wq_t *wq) {
  // gain lock to access list
  pthread_mutex_lock(&(wq->mutex));

  // if list empty, down cv
  while (wq->size == 0) {
    pthread_cond_wait(&(wq->cond), &(wq->mutex));
  }
  // else pop off and return 
  wq_item_t *wq_item = wq->head;
  int client_socket_fd = wq->head->client_socket_fd;
  wq->size--;
  DL_DELETE(wq->head, wq->head);
  free(wq_item);

  // release lock
  pthread_mutex_unlock(&(wq->mutex));
  return client_socket_fd;
}

/* Add ITEM to WQ. */
void wq_push(wq_t *wq, int client_socket_fd) {
  // gain lock to access list
  pthread_mutex_lock(&(wq->mutex));

  // add onto list
  wq_item_t *wq_item = calloc(1, sizeof(wq_item_t));
  wq_item->client_socket_fd = client_socket_fd;
  DL_APPEND(wq->head, wq_item);
  wq->size++;

  // up the cv
  pthread_cond_signal(&(wq->cond));

  // release lock
  pthread_mutex_unlock(&(wq->mutex));
}
