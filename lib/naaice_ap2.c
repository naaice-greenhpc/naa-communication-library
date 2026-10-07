/* Dependencies **************************************************************/

#include "naaice_ap2.h"

#include "ulog.h"

/* Internal Typedefs *********************************************************/

// One available NAA, usually derived from an environment variable set by the
// RMS or user.
typedef struct {
  // Network properties
  char *address;
  uint16_t port;
  // NAA properties (from configuration)
  naa_function_code_t fn_code;
  unsigned int param_count;
  // Handle by which the NAA is used.
  naa_handle *handle;
} naa_job_spec_item_t;

/* Constants *****************************************************************/

// TODO(#6): HHI prefers as few MRs as possible.
#define MAX_PARAMS 32

// All timeouts given in ms.
#define TIMEOUT_INVOKE 100
#define TIMEOUT_TEST 100
#define TIMEOUT_WAIT 100

// Environment variable names

// Specification of the NAA functions to be used (see parse_naa_job_spec for
// the format).
#define NAA_JOB_SPEC_ENV_VAR_NAME "NAA_SPEC"
#define NAA_JOB_SPEC_CHUNK_SEP ","

// Local IP to use as the communication source. Sometimes needed for correct
// communication, e.g. with multiple ports/HCAs per host.
#define NAA_LOCAL_IP_ENV_VAR_NAME "NAA_LOCAL_IP"

/* Internal data *************************************************************/

static size_t num_naa_job_spec = 0;
static naa_job_spec_item_t *naa_job_spec = NULL;
// TODO(#2): memory leak here.

/* Helper Functions **********************************************************/

static bool parse_naa_job_spec_chunk(char *chunk) {
  naa_job_spec_item_t *spec = &(naa_job_spec[num_naa_job_spec]);
  char *token;
  size_t idx;

  for (idx = 0; (token = strsep(&chunk, ":")) != NULL; idx++) {
    switch (idx) {
      case 0:
        spec->address = strdup(token);
        break;
      case 1:
        if (strlen(token) > 0) {
          spec->port = (uint16_t)strtol(token, NULL, 10);
        } else {
          spec->port = SERVER_CONNECTION_PORT;
        }
        break;
      case 2:
        spec->fn_code = strtol(token, NULL, 10);
        break;
      case 3:
        spec->param_count = strtol(token, NULL, 10);
        break;
    }
  }

  return (idx == 4);
}

// Parse the NAA spec from the NAA_SPEC env var (set by RMS/Slurm, or manually
// for testing). Format: <address>:<port>:<fn_code>:<param_count>[,..]
static void parse_naa_job_spec(void) {
  static bool parsed_env = false;

  if (parsed_env) {
    return;
  }

  parsed_env = true;
  char *naa_spec_env = getenv(NAA_JOB_SPEC_ENV_VAR_NAME);
  if (naa_spec_env == NULL) {
    ulog_error("Environment variable " NAA_JOB_SPEC_ENV_VAR_NAME " not set! No NAAs useable");
    return;
  }

  char *spec = strdup(naa_spec_env);
  if (spec == NULL) {
    ulog_warn("Unable to get NAA job spec provided via " NAA_JOB_SPEC_ENV_VAR_NAME
              " environment variable. No NAAs useable");
    return;
  }

  // One item per NAA spec.
  num_naa_job_spec = 1;
  for (const char *c = spec; *c; c++) {
    if (*c == ',') {
      num_naa_job_spec++;
    }
  }

  naa_job_spec = calloc(num_naa_job_spec, sizeof(*naa_job_spec));
  if (!naa_job_spec) {
    free(spec);
    ulog_error("Memory allocation for job specification failed");
    return;
  }

  char *tokenized_spec = spec, *chunk;
  num_naa_job_spec = 0;  // recount: number of successfully parsed specs
  while ((chunk = strsep(&tokenized_spec, NAA_JOB_SPEC_CHUNK_SEP)) != NULL) {
    if (parse_naa_job_spec_chunk(chunk)) {
      num_naa_job_spec++;
    }
  }

  free(spec);

  if (num_naa_job_spec == 0) {
    ulog_error("No valid NAA job specs provided. No NAAs will be useable");
  }
}

static naa_job_spec_item_t *get_naa_for_function_code(const naa_function_code_t fn_code) {
  parse_naa_job_spec();

  // Find a matching, unused item in the NAA job spec.
  for (size_t i = 0; i < num_naa_job_spec; i++) {
    if (naa_job_spec[i].handle == NULL && naa_job_spec[i].fn_code == fn_code) {
      ulog_info("using remote address %s (fn_code %d), port %d", naa_job_spec[i].address, fn_code,
                naa_job_spec[i].port);
      return naa_job_spec + i;
    }
  }

  return NULL;
}

/* Public Function Implementations *******************************************/

int naa_create(const naa_function_code_t function_code, naa_param_t *input_params,
               unsigned int input_amount, naa_param_t *output_params, unsigned int output_amount,
               naa_handle *handle) {
  // Track which output regions are also used as input regions.
  bool output_as_input[output_amount];
  for (unsigned int i = 0; i < output_amount; i++) {
    output_as_input[i] = false;
  }

  // Clear the naa_handle.
  memset(handle, 0, sizeof(naa_handle));

  // Setup the NAA to be used.
  naa_job_spec_item_t *naa = get_naa_for_function_code(function_code);
  if (naa == NULL) {
    ulog_error("No NAA for function code %d available.", function_code);
    return -1;
  }

  if (naa->param_count != input_amount) {
    ulog_error(
        "Mismatch between number of arguments for NAA at %s (fn_code "
        "%d). %d configured, %d provided by application",
        naa->address, naa->fn_code, naa->param_count, input_amount);
    return -1;
  }
  naa->handle = handle;

  // Convert the params into the representation the API layer expects
  // (i.e. without the naa_param_t type).
  size_t params_amount = input_amount + output_amount;
  for (unsigned int i = 0; i < output_amount; i++) {
    for (unsigned int j = 0; j < input_amount; j++) {
      if ((char *)output_params[i].addr == (char *)input_params[j].addr) {
        // Same region for input and output: avoid reallocating and
        // reregistering it.
        params_amount--;
        output_as_input[i] = true;
        break;
      }
    }
  }

  char *param_addrs[params_amount];
  size_t param_sizes[params_amount];
  for (unsigned int i = 0; i < input_amount; i++) {
    param_addrs[i] = (char *)input_params[i].addr;
    param_sizes[i] = input_params[i].size;
  }

  unsigned idx = 0;
  for (unsigned int i = 0; i < output_amount; i++) {
    if (output_as_input[i] == false) {
      param_addrs[idx + input_amount] = (char *)output_params[i].addr;
      param_sizes[idx + input_amount] = output_params[i].size;
      idx++;
    }
  }

  // Initialize the communication context.
  if (naaice_init_communication_context(
          &(handle->comm_ctx), 0, param_sizes, param_addrs, params_amount, 0, 0, function_code,
          getenv(NAA_LOCAL_IP_ENV_VAR_NAME), naa->address, naa->port)) {
    return -1;
  }

  // Set the immediate value sent later as part of the data transfer.
  // FIXME(#2): possible leak; could use a uint32_t and let naaice_set_immediate mask it.
  uint8_t *imm_bytes = (uint8_t *)calloc(3, sizeof(uint8_t));
  if (naaice_set_immediate(handle->comm_ctx, imm_bytes)) {
    return -1;
  }

  // Setup the connection to the NAA.
  if (naaice_setup_connection(handle->comm_ctx)) {
    return -1;
  }

  // Match each input param's address to a region in the comm context and mark
  // it as input (and single-send if requested); error if not found.
  for (unsigned int i = 0; i < input_amount; i++) {
    bool param_exists = false;
    for (int j = 0; j < handle->comm_ctx->no_local_mrs; j++) {
      if (input_params[i].addr == (void *)handle->comm_ctx->mr_local_data[j].addr) {
        param_exists = true;
        if (input_params[i].single_send == true) {
          if (naaice_set_singlesend_mr(handle->comm_ctx, j)) {
            ulog_error(
                "Error on setting constant memory regions (single send "
                "regions).\n");
            return -1;
          };
        }
        if (naaice_set_input_mr(handle->comm_ctx, j)) {
          return -1;
        }
        break;
      }
    }

    if (!param_exists) {
      ulog_error(
          "Requested input parameter which was not previously passed to "
          "naaice_init_communication_context.\n");
      return -1;
    }
  }

  for (unsigned int i = 0; i < output_amount; i++) {
    bool param_exists = false;
    for (int j = 0; j < handle->comm_ctx->no_local_mrs; j++) {
      if (output_params[i].addr == (void *)handle->comm_ctx->mr_local_data[j].addr) {
        param_exists = true;
        if (output_params[i].single_send == true) {
          if (naaice_set_singlesend_mr(handle->comm_ctx, j)) {
            ulog_error(
                "Error on setting constant memory regions (single send "
                "regions).\n");
            return -1;
          }
        }

        if (naaice_set_output_mr(handle->comm_ctx, j)) {
          return -1;
        }

        break;
      }
    }
    if (!param_exists) {
      ulog_error(
          "Requested output parameter which was not previously passed "
          "to naaice_init_communication_context.\n");
      return -1;
    }
  }

  // Register the memory regions.
  if (naaice_register_mrs(handle->comm_ctx)) {
    return -1;
  }

  // Memory region setup protocol. Done once here.
  if (naaice_do_mrsp(handle->comm_ctx)) {
    return -1;
  }

  return 0;
}

// Obsolete now that sending only specific regions is implemented?
// TODO(#6): use input_params to select transferred data; unify input/output types?
int naa_invoke(naa_handle *handle) {
  // Initialize data transfer to the NAA.
  if (naaice_init_data_transfer(handle->comm_ctx)) {
    return -1;
  }

  return 0;
}

int naa_test(naa_handle *handle, bool *flag, naa_status *status) {
  // Poll for NAA completion; -1 means an error occurred.
  if (naaice_poll_cq_nonblocking(handle->comm_ctx)) {
    return -1;
  }

  // Completion flag.
  if (handle->comm_ctx->state >= NAAICE_FINISHED) {
    *flag = true;
  } else {
    *flag = false;
  }

  // Update the status struct.
  status->state = handle->comm_ctx->state;
  status->user_immediate = handle->comm_ctx->response_user_immediate;
  status->bytes_received = handle->comm_ctx->bytes_received;

  return 0;
}

int naa_wait(naa_handle *handle, naa_status *status) {
  if (naaice_poll_cq_blocking(handle->comm_ctx)) {
    return -1;
  }

  status->state = handle->comm_ctx->state;
  status->user_immediate = handle->comm_ctx->response_user_immediate;
  status->bytes_received = handle->comm_ctx->bytes_received;

  return 0;
}

int naa_finalize(naa_handle *handle) {
  // Disconnect and clean up.
  return naaice_disconnect_and_cleanup(handle->comm_ctx);

  // TODO(#2): clean up handle memory.
}
