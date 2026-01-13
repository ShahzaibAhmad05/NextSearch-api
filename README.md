# NextSearch - A Scalable Search Engine

An efficient search engine implementation in **C++**, capable of generating a **Lexicon**, **Forward Index**, and **Inverted Index** from a CSV dataset, along with a Frontend built in React with Typescript.

This project demonstrates core indexing concepts used in modern search engines.

---

## **Project Structure**

````
NextSearch
├─ AddDocument.cpp
├─ api_add_document.cpp
├─ api_add_document.hpp
├─ api_autocomplete.cpp
├─ api_autocomplete.hpp
├─ api_engine.cpp
├─ api_engine.hpp
├─ api_http.cpp
├─ api_http.hpp
├─ api_metadata.cpp
├─ api_metadata.hpp
├─ api_segment.cpp
├─ api_segment.hpp
├─ api_server.cpp
├─ api_types.hpp
├─ barrels.hpp
├─ CMakeLists.txt
├─ cordjson.hpp
├─ Dockerfile
└─ ForwardIndex.cpp
````

---

## Usage

Get a subset of the Cord19 Dataset from [here](https://drive.google.com/file/d/13TQhCDnkPcsjJbZFkgEHuaV50UPCIZKb/view?usp=drive_link).

- Place the dataset in a known directory, for example, D:/cord_19/

- Build backend using cmake:

````
cmake -S . -B build
cmake --build build
````

- Run `forwardindex.exe` to create forward index.

- Run `lexicon.exe` to create inverted index, and lexicon.

- Run `api_server.exe` to run the backend API locally.
