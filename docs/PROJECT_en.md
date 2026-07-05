# Computer Architecture and Organization 2 — Project Assignment

> School of Electrical Engineering, University of Belgrade
> Department of Computer Engineering and Information Theory

The goal of the project is to implement a hypervisor using the **Kernel-based Virtual Machine (KVM) API**. The hypervisor must be implemented in **C/C++**. The project is divided into three parts (versions): **A**, **B**, and **C**. Students are advised to read the entire project in full before starting the implementation.

## Project structure

```
project/
├── guest/
│   ├── inc/
│   │   ├── descriptors.h
│   │   ├── interrupts.h
│   │   └── io.h
│   ├── src/
│   │   ├── interrupts.c
│   │   └── main.c
│   ├── guest.ld
│   └── Makefile
└── host/
    ├── inc/
    │   └── vm.h
    ├── src/
    │   ├── main.c
    │   └── vm.c
    └── Makefile
```

The project consists of two directories. The first contains all the code corresponding to the guest system (`guest`), and the second to the host (`host`). Both directories contain sub-directories for headers (`inc`) and source (`src`) files. The code currently present in the project is from the `KVM_Zadatak_4` (KVM Assignment 4) task available on the course website. The `Makefile` is written so that an arbitrary number of source files can be added to the `inc` and `src` sub-directories. The `make` tool will create a `build` directory with the compiled code — specifically the hypervisor executable (`host`) and the compiled guest code (`guest`) that must be run inside a virtual machine.

- The hypervisor executable is `hypervisor` (`host/build/hypervisor`).
- The compiled guest code is `guest.img` (`guest/build/guest.img`).

---

## A. [15 points] Basic hypervisor features

Provide the following basic hypervisor features:

- The guest's physical memory size is **2MB, 4MB, or 8MB**. The chosen size is passed as a hypervisor command-line parameter via the `-m` or `--memory` option.
- The virtual machine (VM) runs in **64-bit mode** (*long mode*).
- The page size is **4KB or 2MB**. The chosen size is passed as a hypervisor command-line parameter via the `-p` or `--page` option.
- A VM with only a **single virtual processor**.
- Support **serial output and input on IO port `0xE9`**. The size of data that can be written to / read from the port is 1 byte.
- Support only VMs that finish execution with the **`hlt`** instruction.
- Load and run one or more guests passed as a hypervisor command-line parameter via the `-g` or `--guest` option. The number of VMs to launch is given as an array of files representing the guest's executable code. One VM must be launched per file.
- For each VM, launch one **POSIX thread** (use `pthread.h` in the hypervisor/host code) that ensures successful execution of the guest code.
- If a VM crashes or an unexpected VM exit occurs, execution of that VM (i.e. that thread) must be terminated, and the error code that abruptly stopped the VM's execution must be printed to standard output.

Example invocation:

```sh
./hypervisor --memory 4 --page 2 --guest guest1.img guest2.img
```

---

## B. [15 points] File support

Extend version A of the hypervisor by adding file-handling functionality. Ensure that the guest can open, close, read, write, and seek within a file. The signatures of these functions are:

```c
int open(const char *path, int flags);
int close(int fd);
int read(int fd, char *buf, int count);
int write(int fd, const char *buf, int count);
int lseek(int fd, const int offset, int off_flag);
```

Function details:

**Function: `open`**
- Parameters:
  - `path` — the file name.
  - `flags` — indicators for how the file is opened.
- Return value: A unique file descriptor for the opened file, or `-1` if an error occurred.

**Function: `close`**
- Parameters:
  - `fd` — the unique file descriptor.
- Return value: `0` if the operation succeeded, or `-1` if an error occurred.

**Function: `read`**
- Parameters:
  - `fd` — the unique descriptor of the file being read.
  - `buf` — pointer to the buffer filled with the file's contents.
  - `count` — the buffer size.
- Return value: The number of bytes read, or `-1` if an error occurred.

**Function: `write`**
- Parameters:
  - `fd` — the unique descriptor of the file being written to.
  - `buf` — pointer to the buffer from which data is written.
  - `count` — the buffer size.
- Return value: The number of bytes written, or `-1` if an error occurred.

**Function: `lseek`**
- Parameters:
  - `fd` — the unique descriptor of the file being written to.
  - `offset` — the offset from the beginning of the file where the user wants to position.
  - `off_flag` — the offset indicator.
- Return value: The offset relative to the beginning of the file.

File-open indicators:

| Indicator  | Value | Meaning                                        |
|------------|-------|------------------------------------------------|
| `O_RD`     | 1     | File is opened for reading only.               |
| `O_WR`     | 2     | File is opened for writing only.               |
| `O_RDWR`   | 4     | File is opened for reading and writing.        |
| `O_CREATE` | 8     | File is created if it does not exist.          |

`lseek` indicators:

| Indicator  | Value | Meaning                                                                                         |
|------------|-------|-------------------------------------------------------------------------------------------------|
| `SEEK_SET` | 1     | The cursor in the file is moved to the `offset` value.                                          |
| `SEEK_END` | 2     | The cursor in the file is moved to the end of the file. The `offset` value is ignored for this indicator. |

Requirements:

- Implement the listed functions together with the file-open indicators. Implement the functions via **IN/OUT instructions** using **IO port `0x0278`**.
- The offset is set to **0** when the file is opened.
- File names may consist of lowercase and uppercase alphabet letters, digits 0 to 9, and a dot. The file name must start with a letter. If an attempt is made to create a file with a name that does not satisfy this condition, return an error to the guest.
- Local and shared files are stored on the host system.
- **File sharing between VMs.** Files shared between VMs are passed to the hypervisor as an array of values via the command-line parameter `-f` or `--file`. The hypervisor is given an array of paths to shared files. Files specified via this option are shared between virtual machines. If a VM attempts to write to these shared files, the hypervisor must create a **local copy of the file for that VM (copy-on-write)** and continue working with that local copy. The copy is made on the first write to the file, i.e. on the first call to the file-write function.

Ensure that a VM can work with shared files just as it does with local (non-shared) files. The hypervisor must ensure correct file handling — i.e. it must make sure that each guest accesses only the files that guest created or has read access to.

Example invocation:

```sh
./hypervisor -m 4 -p 2 -g guest1.img guest2.img --file a.txt b.txt
```

---

## C. [15 points] Interrupt support

Extend version B of the hypervisor by adding interrupt support. You must enable communication between multiple virtual machines using interrupts and hypervisor support for injecting interrupts into the guest. Communication between virtual machines happens through a **shared buffer** controlled by the hypervisor. A guest can read from or write to the shared buffer.

Ensure that a guest has two possible roles (operating modes):

- **Reading** — reading from the shared buffer controlled by the hypervisor. Use port `0x510` for reading.
- **Writing** — writing to the shared buffer controlled by the hypervisor. Use port `0x510` for writing.

The hypervisor first injects an interrupt request with **vector 32** into the guest and thereby assigns the operating mode (with vector 32). On the first handling of this interrupt, the guest receives the operating mode via IO port `0x510` (**0 – reading, 1 – writing**). The hypervisor assigns the writing mode to only one virtual machine, while it assigns the reading mode to all the others.

Within the interrupt service routine for vector 32, ensure:

- On the first handling of the interrupt, the VM's **operating mode must be saved**.
- On each subsequent handling, a VM with the **reading** role reads from the shared buffer via port `0x510`. After reading, the VM sends the number of bytes read to the hypervisor via port `0x520` as an indicator that the read operation is complete. If not all bytes were read, VM execution must be stopped.
- On each subsequent handling, a VM with the **writing** role writes to the shared buffer via port `0x510`. After writing, the VM reads the amount of bytes written from the hypervisor via port `0x520`.

Writing to the shared buffer is done by first sending the number of bytes (an unsigned 32-bit-wide variable) that the VM wants to write to the buffer, followed by the array of bytes to be written. When reading, a VM with the reading role first receives from the hypervisor the number of bytes it will read, followed by the array of bytes from the shared buffer. Build a system in which the VM with the writing role continues writing to the shared buffer only after all other VMs have finished reading. Define the shared buffer size via the `BUFFER_SIZE` macro. If the number of bytes written to the shared buffer is greater than the value of `BUFFER_SIZE`, the hypervisor ignores the excess sent bytes.

Test this phase by running the hypervisor with two or more virtual machines, where one virtual machine reads from a local or shared file and writes to the shared buffer, and the other virtual machines, via interrupts, read from the shared buffer and write the read value into a local file.

---

## Option validation

In every phase of the project, ensure that if the hypervisor is started with incorrect values for the hypervisor options (e.g. the value `3` for the `--memory` option, since the allowed values are 2, 4, or 8), the user must be notified of the error and execution must be stopped.

---

## Defense and testing

At the defense, the student is expected to:

- Be able to answer questions about the implementation details of the project.
- Successfully perform a modification for the project phase being defended.
- Show a few (2 or 3) examples that test the functionality of each project phase.
