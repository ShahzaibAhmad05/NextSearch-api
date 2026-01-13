
# NextSearch---A Scalable Search Engine 

**NextSearch** is a scalable **C++-based Search Engine** project inspired by modern search architectures.  
It demonstrates how real search engines work internally from document ingestion to indexing and querying.

This project is suitable for:
- Software Engineering projects
- Search engine fundamentals
- Systems & backend development
- Research & large-dataset indexing
- Data Structure & Algorithms Project


## Table of Contents
- Overview
- Features
- Tech Stack
- Project Architecture
- Project Structure
- Installation
- Build Instructions
- How to Run
- API Modules
- Docker Support
- Dataset Usage
- Common Errors
- Future Improvements
- Contributing
- License
- Author

## Overview

NextSearch is a **search engine core written in C++** that focuses on:

- Efficient document indexing
- Forward & inverted index construction
- Lexicon generation
- Modular API-based search operations
- Scalability for large datasets (e.g. research papers)

The project mimics the **backend logic** of real-world search engines like Google or Bing at a system level.

## Features

✔ Forward Index generation  
✔ Inverted Index & Lexicon  
✔ Fast document lookup  
✔ Modular API components  
✔ Designed for large datasets  
✔ Docker support  
✔ Clean & extendable C++ codebase  


## Tech Stack

- **Language:** C++ (Modern C++)
- **Build System:** CMake
- **Containerization:** Docker
- **Version Control:** Git & GitHub
- **OS Support:** Windows, Linux


## Project Architecture

```

Documents
↓
AddDocument
↓
Forward Index
↓
Inverted Index
↓
Lexicon
↓
API Server
↓
Search Queries

```


## Project Structure

```

NextSearch/
│
├── .github/                 # GitHub workflows
├── helper_scripts/          # Helper & automation scripts
│
├── AddDocument.cpp          # Document ingestion
├── ForwardIndex.cpp         # Forward index creation
│
├── api_add_document.cpp
├── api_add_document.hpp
├── api_autocomplete.cpp
├── api_autocomplete.hpp
├── api_metadata.cpp
├── api_metadata.hpp
├── api_server.cpp
├── api_server.hpp
│
├── CMakeLists.txt           # Build configuration
├── Dockerfile               # Docker support
├── LICENSE
└── README.md

````


## Installation

### Install Prerequisites

Make sure you have:

- C++ Compiler (g++ / clang++ / MSVC)
- CMake
- Git

Check versions:
```bash
g++ --version
cmake --version
git --version
```

## Clone the Repository

```bash
git clone https://github.com/ShahzaibAhmad05/NextSearch.git
cd NextSearch
```

## Build Instructions (CMake)

### Configure Build

```bash
cmake -S . -B build
```

### Compile

```bash
cmake --build build
```

Compiled binaries will appear inside the `build/` directory.


## How to Run

After building:

```bash
cd build
```

Run indexing tools or API server depending on your build output:

```bash
./ForwardIndex
./AddDocument
./api_server
```

> ⚠ Executable names may vary depending on your OS and CMake configuration.


## API Modules

The project includes multiple API components:

* `api_add_document` → Add new documents
* `api_autocomplete` → Query suggestions
* `api_metadata` → Document metadata access
* `api_server` → Main search server

These APIs are designed to be connected with a frontend later.


## Docker Support

### Build Docker Image

```bash
docker build -t nextsearch .
```

### Run Container

```bash
docker run --rm -p 8080:8080 nextsearch
```


## Dataset Usage

This project is designed to work with **large text datasets**, such as:

* Research papers
* News articles
* Document corpora (e.g. CORD-19)

Place your dataset in a directory and provide its path to the indexing modules.


## Common Errors & Fixes

### `fatal: not a git repository`

✔ Make sure you are inside the cloned folder:

```bash
cd NextSearch
```


### CMake build errors

✔ Delete old build folder and rebuild:

```bash
rm -rf build
cmake -S . -B build
cmake --build build
```


## Contributing

Contributions are welcome!

1. Fork the repository
2. Create a feature branch
3. Commit your changes
4. Open a Pull Request


## License

This project is licensed under the terms of the **LICENSE** file included in this repository.


## Authors

**Shahzaib Ahmad , Muhammad Ali & Shehroz Shoukat as DSA Project in 3rd Sem.**
GitHub: [https://github.com/ShahzaibAhmad05](https://github.com/ShahzaibAhmad05)

 If you found this project useful, **give it a star on GitHub!**


