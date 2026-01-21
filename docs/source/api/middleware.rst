NAAICE Middleware API Documentation
============================

This document describes the API for the NAAICE Middleware which integrates a network-
attached accelerator (NAA) into HPC data centers using RoCEv2, allowing for IP-based
remote direct memory accesses (RDMA). In order for this to work, a middleware/com-
munication library was designed to achieve the following goals:

1. **Easy integration into HPC applications**
The communication details should be transparent to the user, since an application
developer should not deal with communication specifics, but rather concentrate
on the specification of the computation which should be offloaded to the NAA.
Communication details include for example a communication context containing
ibverbs-specific structures such as Infiniband queue pairs or details of memory
region handling.

2. **Fast adaption by the HPC community**
The middleware library should be easily understood and used by the HPC com-
munity. As such, the popular and well known Message Passing Interface (MPI)
standard will be taken as an inspiration to formulate an NAA middleware, which
will reproduce or reuse functionalities and structures of the MPI standards. We
assume that a middleware with analogies to MPI can be more easily adopted by
the HPC community.

3. **Ability for communication-computation-overlap (CCO)**
Instead of serialized communication and computation, the aim of the middleware
is to allow both to happen concurrently. For this, non-blocking communication
calls are necessary. Non-blocking communication functions return before the ac-
tual communication i.e. data-transfer is done. The host node can then continue
with some other computation while the NIC continues with the data transfer.

A PDF version of the middleware documentation can also be found `here <https://www.greenhpc.eu/assets/docs/middleware.pdf>`_.


Structs & Enums
---------------
..  doxygengroup:: StructsEnumsMiddleware
    :project: naa-communication-prototype
    :members:
    :content-only:

Functions
---------
..  doxygengroup:: PublicFunctionsMiddleware
    :project: naa-communication-prototype
    :content-only:


Example
-------
The following code shows the use of the middleware in a minimal example, in which two input regions are offloaded and the result is expected in a third output region. The example shows non-blocking waiting on the first invoke and blocking waiting on the second one.

.. code-block:: C
    :caption: Minimal NAAICE Middleware example

    //All data is gathered and sent to the NAA in one go.
    #include <naaice_ap2.h>
    #define FNCODE_VEC_ADD 0

    void *a, *b, *c;
    a = calloc(64, sizeof(double));
    b = calloc(64, sizeof(double));
    c = calloc(64, sizeof(double));

    // define input and output memory regions
    naa_param_t input_param[2] = {{a, 64 * sizeof(double)}, 	
    			      {b, 64 * sizeof(double)}};
    naa_param_t output_param[1] = {{c, 64 * sizeof(double)}};

    naa_handle handle;

    // establish connection
    naa_create(FNCODE_VEC_ADD, &input_params, 2, &output_params, 1, &handle) ;

    int flag = 0;
    naa_status status;

    // transfer data to NAA
    naa_invoke(&handle);

    // non-blocking check if results are received
    naa_test(&handle,&flag,&status)
    while (!flag) {
      do_other_work();
      naa_test(&handle,&flag,&status)
    }

    // process results
    ...

    // set inputs with new data
    set_inputs(a, 64, b, 64) ;

    naa_invoke(&handle);

    // blocked waiting for RPC to finish
    naa_wait (&handle,&status)
    process_results(c);

    // finalize connection
    naa_finalize(&handle);


Further examples of the middleware API can be found in `examples/naaice_client_ap2.c <https://git.gfz.de/naaice/naa-communication-prototype/-/blob/main/src/naaice_client_ap2.c?ref_type=heads>`_.