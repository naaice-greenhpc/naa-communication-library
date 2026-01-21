# NAAICE Communication Libraries

[![License](https://img.shields.io/badge/license-BSD--3--Clause-02B36C)](LICENSE)

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
    - [Installation/Usage](#installationusage)
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

- CMake >= 3.10
- C compiler with C11 support
- RDMA libraries: `libibverbs`, `librdmacm`
- (Optional) Doxygen and Sphinx for documentation

### Configuration

```bash
cmake -S . -B build [OPTIONS]
```

**Build Options:**

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | `Debug`, `Release` | `Release` | Build configuration |
| `LOG_LEVEL` | `LOG_INFO`, `LOG_DEBUG`, `LOG_WARN`, `LOG_ERROR` | `LOG_INFO` | Logging verbosity |
| `USE_EMA` | `ON`, `OFF` | `OFF` | Enable EMA measurements |

### Compilation

```bash
cmake --build build
```

### Installation/Usage

```bash
cmake --install build --prefix /path/to/install
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
NAA_SPEC=10.3.10.42:12345:1:3 ./build/src/naaice_client_ap2 4 "1024 1024 1024 1024"
```


## Documentation

Full API documentation is available at: https://naaice.git-pages.gfz-potsdam.de/naa-communication-prototype/

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

- Multiple concurrent connections from different clients to the same `naaice_swnaa` instance are not yet supported

### Fixed Issues

✅ Work request posting limit (increased from 10 to configurable value)  
✅ Error handling and messaging between communication partners  
✅ Multiple output memory region handling  
✅ Receive queue overflow with many output regions  


## Authors

- Florian Mikolajczak (florian.mikolajczak@uni-potsdam.de)
- Dylan Everingham (everingham@zib.de)
- Niklas Schelten (niklas.schelten@hhi.fraunhofer.de)
- Steffen Christgau (christgau@zib.de)
- Hannes Signer (signer@uni-potsdam.de)

## References

- NAAICE Project Website: https://www.greenhpc.eu

## License

BSD-3-Clause

---

**Last Updated**: January 2026