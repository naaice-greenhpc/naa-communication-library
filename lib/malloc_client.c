#include "malloc_client.h"

#include <curl/curl.h>
#include <endian.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct alloc_array {
  uint32_t *data;
  size_t size;
};

// curl read callback feeding the POST body from a struct alloc_array
size_t read_post_data(char *ptr, size_t size, size_t nitems, void *indata) {
  struct alloc_array *array = (struct alloc_array *)indata;
  size_t buff_size = size * nitems;
  size_t copy_size = array->size;

  if (array->size <= 0) return 0;

  if (buff_size < array->size) copy_size = buff_size;

  memcpy(ptr, array->data, copy_size);

  array->data += copy_size;
  array->size -= copy_size;

  return copy_size;
}

// curl write callback copying the response data into outdata
size_t write_post_data(char *ptr, size_t size, size_t nitems, void *outdata) {
  size_t copy_size = size * nitems;

  if (copy_size > CURL_MAX_WRITE_SIZE) copy_size = CURL_MAX_WRITE_SIZE;

  memcpy(outdata, ptr, copy_size);

  return copy_size;
}

// debug helper to test the connection to the remote memory management server
int rmms_say_hi(const char *server, int port) {
  CURL *curl;
  CURLcode ret_code;

  curl = curl_easy_init();

  char url_alloc_fmt[] = "http://%s/remote_malloc/hi";
  char url_alloc[MAX_REQUEST_LEN];

  int url_len = snprintf(url_alloc, MAX_REQUEST_LEN, url_alloc_fmt, server);
  if (url_len == MAX_REQUEST_LEN) return -1;

  curl_easy_setopt(curl, CURLOPT_URL, url_alloc);
  curl_easy_setopt(curl, CURLOPT_PORT, port);
  curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);

  ret_code = curl_easy_perform(curl);

  curl_easy_cleanup(curl);

  return ret_code;
}

int rmms_type_alloc(uint32_t *sizes, uint64_t *addresses, size_t region_count, const char *server,
                    int port, const char *fpga_mnemonic, const char *mem_type) {
  CURL *curl;
  CURLcode ret_code;

  char body_data[CURL_MAX_WRITE_SIZE];
  char url_alloc_fmt[] = "http://%s/remote_malloc/alloc/%s%s";
  char url_alloc[MAX_REQUEST_LEN];
  char mem_type_string[MAX_RMMS_MEM_TYPE_LEN] = "";

  int mem_type_len = 0;

  curl = curl_easy_init();

  uint32_t *alloc_list = calloc(region_count, sizeof(uint32_t));
  if (alloc_list == NULL) return -1;

  for (size_t i = 0; i < region_count; i++) {
    alloc_list[i] = htobe32(sizes[i]);
  }

  struct alloc_array post_data;
  post_data.data = alloc_list;
  post_data.size = region_count * sizeof(uint32_t);

  if (!curl) {
    free(alloc_list);
    return -1;
  }

  if (mem_type != NULL) {
    mem_type_len = snprintf(mem_type_string, MAX_RMMS_MEM_TYPE_LEN, "/%s", mem_type);
  }
  int url_len =
      snprintf(url_alloc, MAX_REQUEST_LEN, url_alloc_fmt, server, fpga_mnemonic, mem_type_string);
  // bail out if the request url is too large
  if (url_len == MAX_REQUEST_LEN || mem_type_len == MAX_REQUEST_LEN) {
    free(alloc_list);
    curl_easy_cleanup(curl);
    return -1;
  }

  // url, port and POST method
  ret_code = curl_easy_setopt(curl, CURLOPT_URL, url_alloc);
  ret_code |= curl_easy_setopt(curl, CURLOPT_PORT, port);
  ret_code |= curl_easy_setopt(curl, CURLOPT_POST, 1L);

  // Content-Length is region_count * sizeof(uint32_t)
  ret_code |= curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, region_count * sizeof(uint32_t));

  // read callback supplying the request body
  ret_code |= curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_post_data);
  ret_code |= curl_easy_setopt(curl, CURLOPT_READDATA, (void *)(&post_data));

  // write callback receiving the response
  ret_code |= curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_post_data);
  ret_code |= curl_easy_setopt(curl, CURLOPT_WRITEDATA, body_data);

  // bail out if setup failed
  if (ret_code != CURLE_OK) {
    free(alloc_list);
    curl_easy_cleanup(curl);
    return -1;
  }

  ret_code = curl_easy_perform(curl);

  free(alloc_list);

  // bail out if the request failed
  if (ret_code != CURLE_OK) {
    curl_easy_cleanup(curl);
    return -1;
  }

  curl_off_t content_length;
  long response_code;

  // read the response code and Content-Length
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
  curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length);

  if (response_code != 200l) {
    curl_easy_cleanup(curl);
    return -1;
  }

  if ((content_length - sizeof(uint32_t)) % sizeof(uint64_t) != 0) {
    curl_easy_cleanup(curl);
    return -1;
  }

  // user id
  int user_id = *(int *)body_data;
  user_id = be32toh(user_id);

  // allocated addresses
  int tmp_count = 0;
  for (int i = sizeof(uint32_t); i < (content_length); i += sizeof(void *)) {
    uint64_t addr_be = *(uint64_t *)(body_data + i);

    addresses[tmp_count] = be64toh(addr_be);
    tmp_count++;
  }

  curl_easy_cleanup(curl);

  return user_id;
}

int rmms_alloc(uint32_t *sizes, uint64_t *addresses, size_t region_count, const char *server,
               int port, const char *fpga_mnemonic) {
  return rmms_type_alloc(sizes, addresses, region_count, server, port, fpga_mnemonic, NULL);
}

int rmms_free(int user_id, uint64_t free_addr, const char *server, int port,
              const char *fpga_mnemonic) {
  CURL *curl;
  CURLcode ret_code;

  char url_free_fmt[] = "http://%s/remote_malloc/free/%s/%d/%lu";
  char url_free[MAX_REQUEST_LEN];

  int url_len =
      snprintf(url_free, MAX_REQUEST_LEN, url_free_fmt, server, fpga_mnemonic, user_id, free_addr);
  // bail out if the request url is too large
  if (url_len == MAX_REQUEST_LEN) return -1;

  curl = curl_easy_init();

  // url, port and DELETE method
  ret_code = curl_easy_setopt(curl, CURLOPT_URL, url_free);
  ret_code |= curl_easy_setopt(curl, CURLOPT_PORT, port);
  ret_code |= curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");

  if (ret_code != CURLE_OK) {
    curl_easy_cleanup(curl);
    return -1;
  }

  ret_code = curl_easy_perform(curl);

  // read the response code
  long response_code;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

  curl_easy_cleanup(curl);

  if (response_code != 200l) return -1;

  return 0;
}

int rmms_free_all(int user_id, const char *server, int port, const char *fpga_mnemonic) {
  CURL *curl;
  CURLcode ret_code;

  // only works if ONLY the body data is stored and not the header
  char url_alloc_fmt[] = "http://%s/remote_malloc/free/%s/%d";
  char url_alloc[MAX_REQUEST_LEN];

  int url_len = snprintf(url_alloc, MAX_REQUEST_LEN, url_alloc_fmt, server, fpga_mnemonic, user_id);
  // bail out if the request url is too large
  if (url_len == MAX_REQUEST_LEN) return -1;

  curl = curl_easy_init();

  // url, port and DELETE method
  ret_code = curl_easy_setopt(curl, CURLOPT_URL, url_alloc);
  ret_code |= curl_easy_setopt(curl, CURLOPT_PORT, port);
  ret_code |= curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");

  if (ret_code != CURLE_OK) {
    curl_easy_cleanup(curl);
    return -1;
  }

  ret_code = curl_easy_perform(curl);

  // read the response code
  long response_code;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

  curl_easy_cleanup(curl);

  if (response_code != 200l) return -1;

  return 0;
}
