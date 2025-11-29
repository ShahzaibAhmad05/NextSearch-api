#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cctype>

using namespace std;
using DocID = uint32_t;
using TermID = uint32_t;

struct LexiconEntry {
    TermID term_id;
    uint32_t doc_freq = 0;
};

unordered_map<string, LexiconEntry> lexicon;
TermID next_term_id = 1;

vector<string> tokenize(const string& s) {
    string cleaned;
    cleaned.reserve(s.size());
    for (char c : s) {
        if (isalpha((unsigned char)c) || isspace((unsigned char)c))
            cleaned.push_back((char)tolower((unsigned char)c));
        else
            cleaned.push_back(' ');
    }
    stringstream ss(cleaned);
    string tok;
    vector<string> out;
    while (ss >> tok) out.push_back(tok);
    return out;
}

// simple CSV parser with quotes support
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

void index_doc(DocID id, const string& text) {
    (void)id; // not used yet
    auto tokens = tokenize(text);
    unordered_set<string> seen;
    for (auto& t : tokens) {
        if (seen.count(t)) continue;
        auto it = lexicon.find(t);
        if (it == lexicon.end()) {
            lexicon[t] = { next_term_id++, 1 };
        } else {
            it->second.doc_freq++;
        }
        seen.insert(t);
    }
}

int main() {
    ifstream fin("metadata.csv");
    if (!fin.is_open()) {
        cerr << "metadata.csv not found\n";
        return 1;
    }

    string header;
    if (!getline(fin, header)) {
        cerr << "empty metadata.csv\n";
        return 1;
    }

    auto head = parse_csv(header);
    int title_col   = -1;
    int authors_col = -1;
    int abs_col     = -1;

    for (size_t i = 0; i < head.size(); i++) {
        string h = head[i];
        for (auto &ch : h) ch = (char)tolower((unsigned char)ch);
        if (h == "title")    title_col   = (int)i;
        if (h == "authors")  authors_col = (int)i;   // optional
        if (h == "abstract") abs_col     = (int)i;
    }

    if (title_col == -1 && abs_col == -1) {
        cerr << "no title or abstract column found\n";
        return 1;
    }

    DocID doc_id = 1;
    string line;
    while (getline(fin, line)) {
        if (line.empty()) continue;
        auto cols = parse_csv(line);

        int max_needed = max(title_col, abs_col);
        if (authors_col != -1) max_needed = max(max_needed, authors_col);
        if ((int)cols.size() <= max_needed) continue;

        string title    = (title_col   != -1 ? cols[title_col]   : "");
        string authors  = (authors_col != -1 ? cols[authors_col] : "");
        string abstract = (abs_col     != -1 ? cols[abs_col]     : "");

        if (title.empty() && abstract.empty()) {
            doc_id++;
            continue;
        }

        string text = title + " " + authors + " " + abstract;
        index_doc(doc_id, text);
        doc_id++;
    }

    ofstream fout("lexicon.txt");
    if (!fout.is_open()) {
        cerr << "cannot write lexicon.txt\n";
        return 1;
    }

    for (auto &p : lexicon)
        fout << p.first << " " << p.second.term_id << " " << p.second.doc_freq << "\n";

    return 0;
}
