#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

using namespace std;
using DocID = uint32_t;
using TermID = uint32_t;

struct Posting {
    DocID doc_id;
    vector<uint32_t> pos;
};

unordered_map<TermID, vector<Posting>> inv;

static inline void ltrim(string& s) {
    while (!s.empty() && (s[0]==' ' || s[0]=='\t' || s[0]=='\r'))
        s.erase(s.begin());
}

int main() {
    ifstream fin("forward_index.txt");
    if (!fin.is_open()) {
        cerr << "forward_index.txt not found\n";
        return 1;
    }

    string line;
    while (getline(fin, line)) {
        if (line.empty()) continue;
        stringstream ss(line);

        DocID d;
        size_t nt;
        ss >> d >> nt;

        string rest;
        getline(ss, rest);
        ltrim(rest);
        if (rest.empty()) continue;

        stringstream rs(rest);
        string block;

        while (getline(rs, block, ';')) {
            ltrim(block);
            if (block.empty()) continue;

            size_t pos_colon = block.find(':');
            if (pos_colon == string::npos) continue;

            TermID tid = stoi(block.substr(0, pos_colon));
            string pos_str = block.substr(pos_colon + 1);

            vector<uint32_t> positions;
            stringstream ps(pos_str);
            string t;
            while (getline(ps, t, ',')) {
                ltrim(t);
                if (!t.empty())
                    positions.push_back(stoul(t));
            }

            if (positions.empty()) continue;

            inv[tid].push_back({ d, positions });
        }
    }

    for (auto &kv : inv) {
        auto &plist = kv.second;
        sort(plist.begin(), plist.end(),
             [](auto &a, auto &b) { return a.doc_id < b.doc_id; });
    }

    ofstream fout("inverted_index.txt");
    for (auto &kv : inv) {
        TermID tid = kv.first;
        auto &plist = kv.second;

        fout << tid << " " << plist.size() << " ";
        for (size_t i = 0; i < plist.size(); i++) {
            fout << plist[i].doc_id << ":";
            for (size_t j = 0; j < plist[i].pos.size(); j++) {
                fout << plist[i].pos[j];
                if (j + 1 < plist[i].pos.size()) fout << ",";
            }
            if (i + 1 < plist.size()) fout << ";";
        }
        fout << "\n";
    }

    return 0;
}
