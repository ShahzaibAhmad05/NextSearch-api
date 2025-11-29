#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <cctype>

using namespace std;
using DocID = uint32_t;
using TermID = uint32_t;

struct TermOcc {
    TermID tid;
    vector<uint32_t> pos;
};

unordered_map<string, TermID> term_to_id;
vector<vector<TermOcc>> forward_index;

// --- CSV & Tokenizer ---
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
        }
        else if (c == ',' && !inq) {
            cols.push_back(cur);
            cur.clear();
        }
        else cur.push_back(c);
    }
    cols.push_back(cur);
    return cols;
}

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
    string tok;
    vector<string> out;
    while (ss >> tok) out.push_back(tok);
    return out;
}

// ------------------------------------

int main() {
    // Load lexicon
    {
        ifstream lx("lexicon.txt");
        if (!lx.is_open()) {
            cerr << "lexicon.txt not found\n";
            return 1;
        }
        string term;
        TermID tid;
        uint32_t df;
        while (lx >> term >> tid >> df)
            term_to_id[term] = tid;
    }

    ifstream fin("metadata.csv");
    if (!fin.is_open()) {
        cerr << "metadata.csv not found\n";
        return 1;
    }

    string header;
    getline(fin, header);
    auto head = parse_csv(header);

    int title_col = -1, authors_col = -1, abs_col = -1;

    for (size_t i = 0; i < head.size(); i++) {
        string h = head[i];
        for (char &c : h) c = tolower(c);
        if (h == "title") title_col = i;
        if (h == "authors") authors_col = i;
        if (h == "abstract") abs_col = i;
    }

    if (title_col == -1 || abs_col == -1) {
        cerr << "title/abstract column missing\n";
        return 1;
    }

    string line;
    DocID doc_id = 1;
    DocID max_id = 0;

    while (getline(fin, line)) {
        auto cols = parse_csv(line);
        if (cols.size() <= abs_col) continue;

        string title    = cols[title_col];
        string authors  = (authors_col != -1 ? cols[authors_col] : "");
        string abstract = cols[abs_col];

        string text = title + " " + authors + " " + abstract;
        auto tokens = tokenize(text);

        unordered_map<TermID, vector<uint32_t>> mp;

        for (uint32_t i = 0; i < tokens.size(); i++) {
            auto it = term_to_id.find(tokens[i]);
            if (it != term_to_id.end())
                mp[it->second].push_back(i);
        }

        if (doc_id >= forward_index.size())
            forward_index.resize(doc_id + 1);

        vector<TermOcc> v;
        v.reserve(mp.size());
        for (auto &p : mp) v.push_back({ p.first, p.second });

        forward_index[doc_id] = v;
        max_id = doc_id;
        doc_id++;
    }

    // Write forward_index.txt
    ofstream fout("forward_index.txt");
    for (DocID d = 1; d <= max_id; d++) {
        auto &terms = forward_index[d];
        if (terms.empty()) continue;

        fout << d << " " << terms.size() << " ";
        for (size_t i = 0; i < terms.size(); i++) {
            fout << terms[i].tid << ":";
            for (size_t j = 0; j < terms[i].pos.size(); j++) {
                fout << terms[i].pos[j];
                if (j + 1 < terms[i].pos.size()) fout << ",";
            }
            if (i + 1 < terms.size()) fout << ";";
        }
        fout << "\n";
    }

    return 0;
}
