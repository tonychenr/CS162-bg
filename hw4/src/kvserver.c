#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include "kvconstants.h"
#include "kvstore.h"
#include "kvmessage.h"
#include "kvserver.h"
#include "index.h"
#include "tpclog.h"
#include "socket_server.h"

/* Initializes a kvserver. Will return 0 if successful, or a negative error
 * code if not. DIRNAME is the directory which should be used to store entries
 * for this server.  HOSTNAME and PORT indicate where SERVER will be
 * made available for requests. */
int kvserver_init(kvserver_t *server, char *dirname, unsigned int num_sets,
    unsigned int elem_per_set, unsigned int max_threads, const char *hostname,
    int port) {
  int ret;
  ret = kvstore_init(&server->store, dirname);
  if (ret < 0) return ret;
  ret = tpclog_init(&server->log, dirname);
  if (ret < 0) return ret;
  server->hostname = malloc(strlen(hostname) + 1);
  if (!server->hostname) fatal_malloc();
  strcpy(server->hostname, hostname);
  server->port = port;
  server->max_threads = max_threads;
  server->handle = kvserver_handle;

  server->state = TPC_INIT;

  /* Rebuild TPC state. */
  kvserver_rebuild_state(server);
  return 0;
}

/* Sends a message to register SERVER with a TPCLeader over a socket located at
 * SOCKFD which has previously been connected. Does not close the socket when
 * done. Returns -1 if an error was encountered.
 */
int kvserver_register_leader(kvserver_t *server, int sockfd) {
  kvrequest_t register_req;
  memset(&register_req, 0, sizeof(kvrequest_t));

  register_req.type = REGISTER;
  register_req.key = server->hostname;
  char port_string[6];
  sprintf(port_string, "%d", server->port);
  register_req.val = port_string;

  kvrequest_send(&register_req, sockfd);

  kvresponse_t *res = kvresponse_recieve(sockfd);
  int ret = (res && res->type == SUCCESS);
  kvresponse_free(res);
  return ret;
}

/* Attempts to get KEY from SERVER. Returns 0 if successful, else a negative
 * error code.  If successful, VALUE will point to a string which should later
 * be free()d.  */
int kvserver_get(kvserver_t *server, char *key, char **value) {
  int ret;
  if (strlen(key) > MAX_KEYLEN)
    return ERRKEYLEN;
  ret = kvstore_get(&server->store, key, value);
  return ret;
}

/* Checks if the given KEY, VALUE pair can be inserted into this server's
 * store. Returns 0 if it can, else a negative error code. */
int kvserver_put_check(kvserver_t *server, char *key, char *value) {
  int check;
  if (strlen(key) > MAX_KEYLEN || strlen(key) == 0)
    return ERRKEYLEN;
  if (strlen(value) > MAX_VALLEN)
    return ERRVALLEN;
  if ((check = kvstore_put_check(&server->store, key, value)) < 0)
    return check;
  return 0;
}

/* Inserts the given KEY, VALUE pair into this server's store
 * Returns 0 if successful, else a negative error code. */
int kvserver_put(kvserver_t *server, char *key, char *value) {
  int ret;
  if ((ret = kvserver_put_check(server, key, value)) < 0)
    return ret;
  ret = kvstore_put(&server->store, key, value);
  return ret;
}

/* Checks if the given KEY can be deleted from this server's store.
 * Returns 0 if it can, else a negative error code. */
int kvserver_del_check(kvserver_t *server, char *key) {
  int check;
  if (strlen(key) > MAX_KEYLEN || strlen(key) == 0)
    return ERRKEYLEN;
  if ((check = kvstore_del_check(&server->store, key)) < 0)
    return check;
  return 0;
}

/* Removes the given KEY from this server's store. Returns
 * 0 if successful, else a negative error code. */
int kvserver_del(kvserver_t *server, char *key) {
  int ret;
  if ((ret = kvserver_del_check(server, key)) < 0)
    return ret;
  ret = kvstore_del(&server->store, key);
  return ret;
}

/* Handles an incoming kvrequest REQ, and populates RES as a response.  REQ and
 * RES both must point to valid kvrequest_t and kvrespont_t structs,
 * respectively. Assumes that the request should be handled as a TPC
 * message. This should also log enough information in the server's TPC log to
 * be able to recreate the current state of the server upon recovering from
 * failure. See the spec for details on logic and error messages.
 */
void kvserver_handle_tpc(kvserver_t *server, kvrequest_t *req, kvresponse_t *res) {
  /* TODO: Implement me! */
  if (req->type == GETREQ) {
    char *buf;
    int ret = kvserver_get(server, req->key, &buf);
    if (ret == 0) {
      res->type = GETRESP;
      alloc_msg(res->body, buf);
      free(buf);
    } else {
      res->type = ERROR;
      buf = GETMSG(ret);
      alloc_msg(res->body, buf);
    }
  } else if (req->type == PUTREQ || req->type == DELREQ) {
    if (server->state == TPC_READY) {
      res->type = ERROR;
      alloc_msg(res->body, ERRMSG_INVALID_REQUEST);
      return;
    }
    int ret;
    tpclog_log(&server->log, req->type, req->key, req->val);
    if (req->type == PUTREQ) {
      ret = kvserver_put_check(server, req->key, req->val);
    } else {
      ret = kvserver_del_check(server, req->key);
    }

    res->type = VOTE;
    server->state = TPC_READY;
    if (ret != 0) {
      alloc_msg(res->body, GETMSG(ret));
    } else {
      server->pending_key = malloc(MAX_KEYLEN + 1);
      server->pending_value = malloc(MAX_VALLEN + 1);
      server->pending_msg = req->type;
      strcpy(server->pending_key, req->key);
      if (req->val != NULL) {
        strcpy(server->pending_value, req->val);
      }
      alloc_msg(res->body, MSG_COMMIT)
    }
  } else if (req->type == COMMIT) {
    if (server->state == TPC_READY) {
      tpclog_log(&server->log, req->type, req->key, req->val);
      server->state = TPC_COMMIT;
      if (server->pending_msg == PUTREQ) {
        kvserver_put(server, server->pending_key, server->pending_value);
      } else {
        kvserver_del(server, server->pending_key);
      }
      tpclog_clear_log(&server->log);
      free(server->pending_key);
      free(server->pending_value);
      server->pending_key = NULL;
      server->pending_value = NULL;
    }
    res->type = ACK;
    server->state = TPC_INIT;
  } else if (req->type == ABORT) {
    if (server->state == TPC_READY) {
      tpclog_log(&server->log, req->type, req->key, req->val);
      server->state = TPC_ABORT;
      tpclog_clear_log(&server->log);
      free(server->pending_key);
      free(server->pending_value);
      server->pending_key = NULL;
      server->pending_value = NULL;
    }
    res->type = ACK;
    server->state = TPC_INIT;
  }
}

/* Generic entrypoint for this SERVER. Takes in a socket on SOCKFD, which
 * should already be connected to an incoming request. Processes the request
 * and sends back a response message.  This should call out to the appropriate
 * internal handler. */
void kvserver_handle(kvserver_t *server, int sockfd, void *extra) {
  (void)extra; // silence compiler
  kvrequest_t *req = kvrequest_recieve(sockfd);
  kvresponse_t *res = calloc(1, sizeof(kvresponse_t));
  if (!res) fatal_malloc();
  do {
    if (!req) {
      res->type = ERROR;
      alloc_msg(res->body, ERRMSG_INVALID_REQUEST);
    } else if (req->type == INDEX) {
      index_send(sockfd, 0);
      break;
    } else {
      kvserver_handle_tpc(server, req, res);
    }
    kvresponse_send(res, sockfd);
  } while (0);

  kvresponse_free(res);
  kvrequest_free(req);
}

/* Restore SERVER back to the state it should be in, according to the
 * associated LOG.  Must be called on an initialized  SERVER. Only restores the
 * state of the most recent TPC transaction, assuming that all previous actions
 * have been written to persistent storage. Should restore SERVER to its exact
 * state; e.g. if SERVER had written into its log that it received a PUTREQ but
 * no corresponding COMMIT/ABORT, after calling this function SERVER should
 * again be waiting for a COMMIT/ABORT.  This should also ensure that as soon
 * as a server logs a COMMIT, even if it crashes immediately after (before the
 * KVStore has a chance to write to disk), the COMMIT will be finished upon
 * rebuild.
 */
int kvserver_rebuild_state(kvserver_t *server) {
  /* TODO: Implement me! */
  tpclog_iterate_begin(&server->log);
  logentry_t *last_entry = NULL;
  logentry_t *update_entry = NULL;
  while (tpclog_iterate_has_next(&server->log)) {
    last_entry = tpclog_iterate_next(&server->log);
    if (last_entry->type == PUTREQ || last_entry->type == DELREQ)
      update_entry = last_entry;
  }
  if (last_entry == NULL) {
    return 0;
  }
  if (last_entry->type == PUTREQ || last_entry->type == DELREQ) {
    server->pending_key = malloc(MAX_KEYLEN + 1);
    server->pending_value = malloc(MAX_VALLEN + 1);
    server->pending_msg = last_entry->type;
    strcpy(server->pending_key, last_entry->data);
    strcpy(server->pending_value, last_entry->data + strlen(last_entry->data) + 1);
    server->state = TPC_READY;
  } else {
    if (last_entry->type == COMMIT) {
      char *key = update_entry->data;
      char *value = update_entry->data + strlen(last_entry->data) + 1;
      char *buf = malloc(MAX_VALLEN + 1);
      kvserver_get(server, key, &buf);
      if (strcmp(buf, value) != 0) {
        if (update_entry->type == PUTREQ) {
          kvserver_put(server, key, value);
        } else {
          kvserver_del(server, key);
        }
      }
      free(buf);
    }
    tpclog_clear_log(&server->log);
    server->state = TPC_INIT;
    server->pending_key = NULL;
    server->pending_value = NULL;
  }

  return -1;
}

/* Deletes all current entries in SERVER's store and removes the store
 * directory.  Also cleans the associated log. Note that you will be required
 * to reinitialize SERVER following this action. */
int kvserver_clean(kvserver_t *server) {
  return kvstore_clean(&server->store);
}
