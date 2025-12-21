# x86-32-System-Internals-Suite
A collection of low level system programs exploring x86-32 OS internals, including program loading, process execution, scheduling, and multithreading.

This repository brings together multiple system level projects developed to understand how modern operating systems interact with hardware beyond standard library abstractions.

---

## Contents

This suite currently consists of **five system programs**:

1. **Simple Loader**
2. **Simple Shell**
3. **Simple Scheduler**
4. **Smart Loader**
5. **Simple Multithreader**

Each project focuses on a different aspect of **program execution and OS design** on x86-32 systems.

---

## Project Descriptions

### 1️⃣ Simple Loader
A minimal user-space loader that:
- Parses ELF binaries
- Maps program segments into memory
- Transfers control to the entry point manually

**Concepts:**  
ELF format, memory mapping, program entry, virtual memory

---

### 2️⃣ Simple Shell
A basic UNIX-style shell implementation supporting:
- Command execution
- Argument parsing
- Process creation using 'fork' and 'exec'
- Waiting and process termination handling

**Concepts:**  
Processes, system calls, command parsing, user kernel interaction

---

### 3️⃣ Simple Scheduler
A simplified scheduler simulation that:
- Manages multiple processes/tasks
- Demonstrates round robin scheduling policy
- Tracks execution states

**Concepts:**  
CPU scheduling, context switching (logical), process states

---

### 4️⃣ Smart Loader
An enhanced version of the simple loader with:
- Better ELF validation
- Lazy segment loading
- Improved error handling and robustness

**Concepts:**  
Advanced ELF handling, loader optimizations, runtime analysis

---

### 5️⃣ Simple Multithreader
A basic multithreading implementation that:
- Creates and manages multiple threads
- Demonstrates synchronization issues
- Explores shared memory behavior

**Concepts:**  
Threads, concurrency, synchronization, race conditions

---
