#include <naaice.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

char conn_port[6];

int main(int argc, char *argv[]) {
  if (argc != 6) {
    fprintf(stderr, "Wrong number of arguments. use: ./naaice_client local-ip remote-ip number of regions 'region-sizes' requested-size\n Example: ./naaice_client 10.3.10.135 10.3.10.136 1 '1024' 1024 should work between bsnode5 and 6\n");
    exit(EXIT_FAILURE);
  };
  char *ptr; 
  //Check if user input includes at least and no more than 32 memory regions (limit on fpga)
  long int mr_num = strtol(argv[3], &ptr, 10);
  if(mr_num < 1 || mr_num > 32){
    fprintf(stderr, "Chosen number of arguments %ld is not supported. Exiting\n",mr_num);
    exit(EXIT_FAILURE);
  }
  //derive sizes of memory regions
  int i = 0;
  int mr_sizes[mr_num+1];
  char *token = strtok(argv[4], " ");
  mr_sizes[0] = atoi(token);

      // loop through the string to extract all other tokens
      while (i < mr_num) {
    i++;
    /*printf("i %d\n",i);
    if(i > mr_num){
      fprintf(stderr,"More memory region sizes given than number of memory regions. Ignoring exceeding size info");
      token = NULL;
      break;
    }*/
    token = strtok(NULL, " ");
    if(token == NULL){
      if(i < mr_num ){
        fprintf(stderr,"Higher number of memory regions requested than size information given. Exiting\n");
        exit(EXIT_FAILURE);
      }
    break;
    }
    mr_sizes[i] = atoi(token);
  }
  //Size of Meta-data region is fixed right now. Has a variable length highly dependent on the problem to offload
  //Metadata region is used to announce output address to FPGA and to report structure/size of parameters if not implicitly known
  mr_sizes[mr_num] = MR_META_DATA_SIZE;

  for (int j = 0; j < mr_num+1; j++) {
        printf("array pos %d, %d\n", j, mr_sizes[j]);
    }
    clock_gettime(CLOCK_MONOTONIC_RAW, &programm_start_time);

    //Basic Infiniband strucutres, channel and id
    struct rdma_event_channel *rdma_ev_channel;
    struct rdma_cm_id *rdma_comm_id;
    struct addrinfo *rem_addr, *loc_addr;
    struct rdma_cm_event *ev = NULL;
    rdma_ev_channel = rdma_create_event_channel();
    if (rdma_ev_channel == NULL) {
      perror("Failed to create rdma event channel");
      exit(EXIT_FAILURE);
    }
    // Third argument is context, null for now
    if (rdma_create_id(rdma_ev_channel, &rdma_comm_id, NULL, RDMA_PS_TCP) ==
        -1) {
      perror("Failed to create a communication id.");
      exit(EXIT_FAILURE);
    }
    // Allocate communication context structure, this holds all information necessary for the connection
    struct naaice_communication_context *comm_ctx =
        (struct naaice_communication_context *)calloc(1,
            sizeof(struct naaice_communication_context));
    if (comm_ctx == NULL) {
      fprintf(stderr,
              "Failed to allocate memory for communication context. Exiting.");
      return -1;
    }
    rdma_comm_id->context = comm_ctx;
    comm_ctx->id = rdma_comm_id;
    // for MRSP we do not advertise all regions: meta-data region is not
    // announced
    comm_ctx->no_local_mrs = mr_num+1;
    comm_ctx->no_advertised_mrs = mr_num;

    //TODO: Can we find out the amount of requested memory automatically?
    comm_ctx->requested_size = strtoll(argv[5],&ptr,10);
    if (comm_ctx->requested_size > (255) * MAX_TRANSFER_LENGTH) {
      fprintf(stderr, "Requested Transfer Size exceeds the maximum amount of "
                      "memory regions. Exiting\n");
      exit(EXIT_FAILURE);
    }
    errno = 0;
    if ((errno == ERANGE && (comm_ctx->requested_size == ULLONG_MAX)) ||
        (errno != 0 && comm_ctx->requested_size == 0)) {
      perror("Conversion of user-input for transfer length failed.");
      exit(EXIT_FAILURE);
    }
    //Allocate local memory for memory regions, in middleware-APi this is already done by user beforehand.
    struct naaice_mr_local *local;
    local = calloc(comm_ctx->no_local_mrs, sizeof(struct naaice_mr_local));
    if (local == NULL) {
      fprintf(stderr,
              "Failed to allocate memory for local memory region structure\n");
      return 1;
    }
    comm_ctx->mr_local_data = local;
    comm_ctx->rdma_write_finished = 0;
    comm_ctx->rdma_writes_done = 0;

    //Allocate regions by user-input. Number and size for each region was given
    //TODO: Error checking or a smarter way to do this
    for (int i = 0; i < comm_ctx->no_local_mrs; i++) {
      comm_ctx->mr_local_data[i].size = mr_sizes[i];
      posix_memalign((void **)&(comm_ctx->mr_local_data[i].addr),
                     sysconf(_SC_PAGESIZE), comm_ctx->mr_local_data[0].size);
      if (comm_ctx->mr_local_data[i].addr == NULL) {
        fprintf(stderr,
                "Failed to allocate memory for local memory region buffer\n");
        exit(EXIT_FAILURE);
      }
    }
    //TODO: Port for connection is given by handle structure in Middleware later.
    //Doesn't have to be fixed, but well known between host and FPGA
    snprintf(conn_port, 6, "%d", CONNECTION_PORT);
    if (getaddrinfo(argv[1], conn_port, NULL, &loc_addr)) {
      perror("Failed to get address info for local address. Exiting.");
      exit(EXIT_FAILURE);
    }
    if (getaddrinfo(argv[2], conn_port, NULL, &rem_addr)) {
      perror("Failed to get address info for remote address. Exiting.");
      exit(EXIT_FAILURE);
    }
    if (rdma_resolve_addr(rdma_comm_id, loc_addr->ai_addr, rem_addr->ai_addr,
                          TIMEOUT_IN_MS) == -1) {
      perror("Failed to resolve addresses.");
      exit(EXIT_FAILURE);
    }
    freeaddrinfo(loc_addr);
    freeaddrinfo(rem_addr);
    if (argv[3]) {
    }
    
  //Enter event loop until disconnect
  //TODO in Middleware/Application example we exit this event loop after data transfer and get back to it?
  while (rdma_get_cm_event(rdma_ev_channel, &ev) == 0) {
    struct rdma_cm_event ev_cp;
    memcpy(&ev_cp, ev, sizeof(*ev));
    rdma_ack_cm_event(ev);
    if (naaice_on_event_client(&ev_cp,comm_ctx)) {
      break;
    }
  }
  if (rdma_destroy_id(rdma_comm_id) == -1) {
    perror("Failed to destroy RDMA communication id.");
    exit(EXIT_FAILURE);
  }
  rdma_destroy_event_channel(rdma_ev_channel);
  printf("All cleaned up. Exiting\n");
  exit(EXIT_SUCCESS);
  }
