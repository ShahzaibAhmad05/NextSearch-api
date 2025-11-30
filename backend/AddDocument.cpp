#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdint>

using namespace std;

using DocID  = uint32_t;
using TermID = uint32_t;

// ---------------- CSV PARSER ----------------
vector<string> parse_csv(const string& line) {
    vector<string> cols;
    string cur;
    bool inq = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (c == '"') {
            if (inq && i + 1 < line.size() && line[i + 1] == '"') {
                cur.push_back('"');
                i++;
            } else inq = !inq;
        } else if (c == ',' && !inq) {
            cols.push_back(cur);
            cur.clear();
        } else cur.push_back(c);
    }
    cols.push_back(cur);
    return cols;
}

// ---------------- TOKENIZER ----------------
vector<string> tokenize(const string& s) {
    string cleaned;
    cleaned.reserve(s.size());
    for (char c : s) {
        if (isalpha((unsigned char)c) || isspace((unsigned char)c))
            cleaned.push_back(tolower((unsigned char)c));
        else
            cleaned.push_back(' ');
    }
    stringstream ss(cleaned);
    vector<string> out;
    string tok;
    while (ss >> tok) out.push_back(tok);
    return out;
}

struct LexiconEntry {
    TermID tid;
    uint32_t df;
};

struct Posting {
    DocID doc;
    vector<uint32_t> pos;
};

int main() {
    string new_line;
    cout << "Enter new metadata.csv line:\n";
    getline(cin, new_line);

    if (new_line.empty()) {
        cerr << "Empty input.\n";
        return 1;
    }

    // ---------------------------
    // LOAD EXISTING LEXICON
    // ---------------------------
    unordered_map<string, LexiconEntry> lexicon;
    TermID next_tid = 1;

    {
        ifstream fin("lexicon.txt");
        string term;
        TermID tid;
        uint32_t df;
        while (fin >> term >> tid >> df) {
            lexicon[term] = {tid, df};
            next_tid = max(next_tid, tid + 1);
        }
    }

    // ---------------------------
    // FIND MAX DOCID IN FORWARD INDEX
    // ---------------------------
    DocID last_docID = 0;
    {
        ifstream fin("forward_index.txt");
        string line;
        while (getline(fin, line)) {
            if (line.empty()) continue;
            stringstream ss(line);
            DocID d;
            size_t n;
            ss >> d >> n;
            last_docID = max(last_docID, d);
        }
    }
    DocID new_docID = last_docID + 1;

    // ---------------------------
    // PARSE NEW METADATA ROW
    // ---------------------------
    auto cols = parse_csv(new_line);
    if (cols.size() < 19) {
        cerr << "Line does not contain 19 columns.\n";
        return 1;
    }

    string title    = cols[3];
    string abstract = cols[8];
    string authors  = cols[10];

    string text = title + " " + authors + " " + abstract;

    auto tokens = tokenize(text);

    // --- collect occurrences ---
    unordered_map<TermID, vector<uint32_t>> positions;
    unordered_set<string> seen_terms;

    for (uint32_t i = 0; i < tokens.size(); i++) {
        const string& t = tokens[i];

        auto it = lexicon.find(t);
        if (it == lexicon.end()) {
            lexicon[t] = { next_tid++, 1 };
            positions[ lexicon[t].tid ].push_back(i);
            seen_terms.insert(t);
        } else {
            positions[it->second.tid].push_back(i);
            if (!seen_terms.count(t)) {
                it->second.df++;
                seen_terms.insert(t);
            }
        }
    }

    // ---------------------------
    // APPEND TO forward_index.txt
    // ---------------------------
    {
        ofstream fout("forward_index.txt", ios::app);
        fout << new_docID << " " << positions.size() << " ";
        size_t k = 0;

        for (auto &p : positions) {
            fout << p.first << ":";
            for (size_t j = 0; j < p.second.size(); j++) {
                fout << p.second[j];
                if (j + 1 < p.second.size()) fout << ",";
            }
            if (k + 1 < positions.size()) fout << ";";
            k++;
        }
        fout << "\n";
    }

    // ---------------------------
    // UPDATE INVERTED INDEX
    // ---------------------------
    unordered_map<TermID, vector<Posting>> inv;

    {
        ifstream fin("inverted_index.txt");
        string line;
        while (getline(fin, line)) {
            if (line.empty()) continue;
            stringstream ss(line);

            TermID tid;
            size_t n;
            ss >> tid >> n;

            string rest;
            getline(ss, rest);
            if (!rest.empty() && rest[0]==' ') rest.erase(0,1);

            stringstream rs(rest);
            string block;
            while (getline(rs, block, ';')) {
                if (block.empty()) continue;
                size_t c = block.find(':');
                if (c == string::npos) continue;

                DocID d = stoul(block.substr(0, c));
                string pos_str = block.substr(c + 1);

                vector<uint32_t> pos;
                stringstream ps(pos_str);
                string num;
                while (getline(ps, num, ',')) {
                    if (!num.empty())
                        pos.push_back(stoul(num));
                }
                inv[tid].push_back({d, pos});
            }
        }
    }

    // add new postings
    for (auto &p : positions) {
        TermID tid = p.first;
        inv[tid].push_back({ new_docID, p.second });
    }

    // sort postings
    for (auto &kv : inv)
        sort(kv.second.begin(), kv.second.end(),
             [](auto &a, auto &b){ return a.doc < b.doc; });

    // write back inverted index
    {
        ofstream fout("inverted_index.txt");
        for (auto &kv : inv) {
            fout << kv.first << " " << kv.second.size() << " ";
            for (size_t i = 0; i < kv.second.size(); i++) {
                fout << kv.second[i].doc << ":";
                for (size_t j = 0; j < kv.second[i].pos.size(); j++) {
                    fout << kv.second[i].pos[j];
                    if (j + 1 < kv.second[i].pos.size()) fout << ",";
                }
                if (i + 1 < kv.second.size()) fout << ";";
            }
            fout << "\n";
        }
    }

    // ---------------------------
    // WRITE UPDATED LEXICON
    // ---------------------------
    {
        ofstream fout("lexicon.txt");
        for (auto &kv : lexicon)
            fout << kv.first << " " << kv.second.tid << " " << kv.second.df << "\n";
    }

    cout << "Update complete. Added as DocID " << new_docID << "\n";
    return 0;
}
