# NAAICE_MWE
This library serves as a minimal working example for the RDMA transfer to the FPGAs.

It provides two examples:
- `/src/naaice_client.c`: initiates a connection to the given IP, sends data of the given length to the given remote address (arrays of integer defined in the source code), waits for data (sent data incremented by 1) and checks the data for errors.
- `/src/naaice_server.c`: waits for a remote to connect. Allocates memory regions analogously to the client. Waits for data, increments it by 1 and sends it back. 

Data transfer is done by RDMA WRITE operations. The memory regions for data transfer are exchanged beforehand using a new protocol. Protocol details were discussed in NAAICE work meetings in Nov 2022/Jan 2023. Memory regions are exhanged using a single RDMA SEND operation (per direction) with a dynamic message. This message can hold a variable number of announced memory regions and a request for size of memory to allocate memory regions.

Sample commands
1. Node 1 (IP `10.3.10.136`):\
   `src/naaice_server `
2. Node 2 (IP `10.3.10.135`):\
    `src/naaice_client 10.3.10.135 10.3.10.136 16474836480`

The port for the initial connection is fixed (but can be changed). All routines not taken from ibverbs or librdmacm libraries are named starting naaice_*. Testing for sending multiple memory regions can be sped up by changing the MAXIMUM_TRANSFER_LENGTH, a variable used to denote the maximum size of a single RDMA operation. By lowering the value, one can force the use of more and smaller memory regions.



### Bugs

### Fixed Bugs:
 - So far, posting write work requests at once fails with a number of work requests > 15. The reason for this is yet unknown. Reason was the number of maximum outstandingn write request during qp setup. This value was 10. Increasing this limit solved the problem
 - So far, there is no error handling regarding the connection. Error messages are not yet send, however each communication partner has internal error handling mechanisms. Error messages are now send as was discussed for AP1 in April/May 2023.