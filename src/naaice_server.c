#include <naaice.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, __attribute__((unused)) char *argv[]) {
  // TODO maybe make port flexible
  if (argc != 1) {
    fprintf(stderr,
            "Server should be called with no arguments prot is hardcoded :) ");
    exit(EXIT_FAILURE);
  };

  // maybe not on the stack?!
  struct rdma_event_channel *rdma_ev_channel;
  struct rdma_cm_id *rdma_comm_id;
  struct sockaddr loc_addr;
  struct rdma_cm_event *ev = NULL;

  rdma_ev_channel = rdma_create_event_channel();
  if (rdma_ev_channel == NULL) {
    perror("Failed to create rdma event channel");
    exit(EXIT_FAILURE);
  }

  // Third argument is context, null for now
  if (rdma_create_id(rdma_ev_channel, &rdma_comm_id, NULL, RDMA_PS_TCP) == -1) {
    perror("Failed to create a communication id.");
    exit(EXIT_FAILURE);
  }
  //Allocate communication context earlier than before.
  struct naaice_communication_context *comm_ctx =
      (struct naaice_communication_context *)calloc(1,
          sizeof(struct naaice_communication_context));
  if (comm_ctx == NULL) {
    fprintf(stderr,
            "Failed to allocate memory for communication context. Exiting.");
    return -1;
  }

  // Bind address to communication ID and stamallocrt listening for connections
  rdma_comm_id->context = comm_ctx;
  //comm_ctx->id = rdma_comm_id;
  //Make port flexible?
  memset(&loc_addr, 0, sizeof(loc_addr));
  loc_addr.sa_family = AF_INET;
  ((struct sockaddr_in *)&loc_addr)->sin_port = htons(CONNECTION_PORT);

  if (rdma_bind_addr(rdma_comm_id, &loc_addr)) {
    perror("Binding Communication ID to local address failed. Exiting.\n");
  }

  if (rdma_listen(rdma_comm_id, 10)) { // backlog queue length 10
    perror("Listening on specified port failed.\n");
  }

  int port = ntohs(rdma_get_src_port(rdma_comm_id));
  printf("listening on port %d.\n", port);
  //Enter event loop.
  while (rdma_get_cm_event(rdma_ev_channel, &ev) == 0) {
    struct rdma_cm_event ev_cp;
    memcpy(&ev_cp, ev, sizeof(*ev));
    rdma_ack_cm_event(ev);
    if (naaice_on_event_server(&ev_cp,comm_ctx))
      break;
  }
  // clean_id:
  if (rdma_destroy_id(rdma_comm_id) == -1) {
    perror("Failed to destroy RDMA communication id.");
  }
  //printf("Destroyed rdma_comm_id successfully\n");
  rdma_destroy_event_channel(rdma_ev_channel);
  printf("All cleaned up. Exiting\n");
  exit(EXIT_SUCCESS);
}
