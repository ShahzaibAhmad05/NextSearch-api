# NextSearch Search What's Next with NextSearch

An efficient search engine implementation in **C++**, capable of generating a **Lexicon**, **Forward Index**, and **Inverted Index** from a CSV dataset.  
This project demonstrates core indexing concepts used in modern search engines.

---

## **Project Structure**

````
NextSearch
├─ backend/
│  ├─ ForwardIndex.cpp
│  ├─ AddDocument.cpp
│  ├─ InvertedIndex.cpp
│  └─ lexicon.cpp
├─ sampleFiles/
│  ├─ forward_index.txt
│  ├─ lexicon.txt
│  ├─ inverted_index.txt
│  └─ metadata.csv
├─ LICENSE
└─ README.md
````

## **Features**

* **Tokenization & Normalization**
  * Converts text to lowercase  
  * Removes punctuation  
  * Splits into valid terms

* **Lexicon Generation**
  * Builds a vocabulary of unique terms  
  * Assigns each term a unique **TermID**  
  * Stores document frequency  
  * Output: `lexicon.txt`

* **Forward Index**
  * Maps **document → terms + their positions**  
  * Reads metadata (title, authors, abstract)  
  * Stores positional information  
  * Output: `forward_index.txt`

* **Inverted Index**
  * Maps **term → documents containing that term + positions**  
  * Sorts posting lists by document ID  
  * Output: `inverted_index.txt`

---

## **Usage**

### 1. Compile the Code

Use `g++` (or any C++ compiler):

```bash
g++ lexicon.cpp -o lexicon
g++ forward_index.cpp -o forward_index
g++ inverted_index.cpp -o inverted_index
