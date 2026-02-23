# NAAICE Communication Libraries

[![License](https://img.shields.io/badge/license-BSD--3--Clause-02B36C)](LICENSE)
[![Project](https://img.shields.io/badge/Project-greenhpc.eu-02B36C)](https://greenhpc.eu)
[![DOI](https://zenodo.org/badge/1142599439.svg)](https://doi.org/10.5281/zenodo.18403259)


## Overview

This library provides a low-level and middleware implementation for RDMA-based communication with Network-Attached Accelerators (NAAs). It serves as a reference implementation for the NAAICE project, enabling efficient data transfer between hosts and FPGA-based or software-emulated accelerators.

### Key Features

- RDMA-based data transfer using RDMA WRITE operations
- Dynamic memory region exchange protocol (MRSP)
  - Protocol details were discussed in NAAICE work meetings in Nov 2022/Jan 2023. Memory regions are exhanged using a single RDMA SEND operation (per direction) with a dynamic message. This message can hold a variable number of announced memory regions and a request for size of memory to allocate memory regions.
- Support for multiple input/output memory regions
- Software NAA implementation for testing without FPGA hardware
- Application-level interface (Middleware) for simplified integration

## Table of Contents

- [NAAICE Communication Libraries](#naaice-communication-libraries)
  - [Overview](#overview)
    - [Key Features](#key-features)
  - [Table of Contents](#table-of-contents)
  - [Build Instructions](#build-instructions)
    - [Prerequisites](#prerequisites)
    - [Configuration](#configuration)
    - [Compilation](#compilation)
  - [Examples](#examples)
    - [Low-Level API (AP1)](#low-level-api-ap1)
    - [Application-Level API (Middleware)](#application-level-api-middleware)
  - [Documentation](#documentation)
    - [Building Documentation](#building-documentation)
    - [API Reference](#api-reference)
  - [Known Limitations](#known-limitations)
    - [Fixed Issues](#fixed-issues)
  - [Authors](#authors)
  - [References](#references)
  - [License](#license)


## Build Instructions

### Prerequisites

- CMake >= 3.15
- C compiler with C17 support
- RDMA libraries: `libibverbs`, `librdmacm`
- (Optional) Doxygen and Sphinx for documentation

### Configuration

To build the examples run the following commands:

```bash
cmake -S . -B build [OPTIONS]
```

**Build Options:**

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | `Debug`, `Release` | `Release` | Build configuration |
| `LOG_LEVEL` | `ULOG_LEVEL_INFO`, `ULOG_LEVEL_DEBUG`, `ULOG_LEVEL_WARN`, `ULOG_LEVEL_ERROR` | `ULOG_LEVEL_INFO` | Logging level |
| `LOG_VERBOSE` | `ON`, `OFF` | `OFF` | Enables showing the full file path in log messages| 
| `USE_EMA` | `ON`, `OFF` | `OFF` | Enable EMA measurements |
| `NUMBER_CONNECTIONS` | 1...n | Core number | Maximal allowed number of parallel server connections 

### Compilation/Installation

To build the examples, run the following command: 

```bash
cmake --build build
```

For your own projects, the APIs can be integrated via CMake using the FetchContent mechanism. A dedicated CMake target is provided for each of the three components:

- NAAICE Low-Level API (naaice::low_level)
- NAAICE Middleware API (naaice::middleware)
- Software-NAA (naaice::swnaa).

## Examples

### Low-Level API (AP1)

Direct control over RDMA connections and memory regions.

**Server (NAA side):**
   - Waits for connections
   - Allocates memory regions
   - Processes data (increments by 1)
   - Sends results back

**Update**: The server now also supports multiple connections. User-specific worker logic must be implemented in the `kernels/swnaa_kernels.c` file, and encoding is performed using the function code. 

```bash
./build/examples/naaice_server
```

**Client (Host side):**

  - Initiates connection to NAA
  - Sends integer arrays
  - Receives processed data (incremented values)
  - Validates results

```bash
./build/examples/naaice_client <local_ip> <server_ip> <num_regions> "<region_sizes>"
```

Example:
```bash
# Node 1 (10.3.10.42)
./build/examples/naaice_server

# Node 2 (10.3.10.41)
./build/examples/naaice_client 10.3.10.41 10.3.10.42 3 "1024 1024 1024"
```

### Application-Level API (Middleware)

Simplified interface using environment variables for NAA discovery.

**Environment Variables:**

- `NAA_SPEC`: Specification of available NAAs
  - Format: `<address>:<port>:<fn_code>:<fn_num_args>[,...]`
  - Example: `10.3.10.42:12345:1:3`
  
- `NAA_LOCAL_IP` (optional): Local interface IP/hostname

**Example:**
```bash
NAA_SPEC=10.3.10.42:12345:1:4 ./build/examples/naaice_client_ap2 4 "1024 1024 1024 1024"
```


## Documentation

Full API documentation is available at: https://naaice-greenhpc.github.io/naa-communication-library/

### Building Documentation

```bash
cd docs
doxygen
make html
```

Documentation will be generated in `docs/build/`.

### API Reference

- **Low-Level API**: See `docs/source/api/low_level.rst`
- **Middleware API**: See `docs/source/api/middleware.rst`
- **Software NAA**: See `docs/source/api/swnaa.rst`

## Known Limitations


### Fixed Issues

✅ Work request posting limit (increased from 10 to configurable value)  
✅ Error handling and messaging between communication partners  
✅ Multiple output memory region handling  
✅ Receive queue overflow with many output regions  


## Authors

- Steffen Christgau (christgau@zib.de)
- Dylan Everingham (everingham@zib.de)
- Florian Mikolajczak (florian.mikolajczak@uni-potsdam.de)
- Niklas Schelten (niklas.schelten@hhi.fraunhofer.de)
- Hannes Signer (signer@uni-potsdam.de)
- Fritjof Steinert (fritjof.steinert@hhi.fraunhofer.de)

## References

- NAAICE Project Website: https://www.greenhpc.eu

## License

BSD-3-Clause


## Funding

The development of the NAAICE Communication Libraries is funded by the BMFTR Germany in the context of the [NAAICE](https://greenhpc.eu) Project ([GreenHPC](https://gauss-allianz.de/en/project/call/Richtlinie%20zur%20F%C3%B6rderung%20von%20Verbundprojekten%20auf%20dem%20Gebiet%20des%20energieeffizienten%20High-%E2%80%8BPerformance%20Computings%20%28GreenHPC%29) grant)


<img src="docs/source/_static/BMFTR-sponsored.jpg" alt="BMFTR" title="BMFTR" width="50%">


---

**Last Updated**: January 2026