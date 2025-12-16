// src/App.tsx
import React, { useState } from "react";
import SearchBar from "./components/SearchBar";
import SearchResults from "./components/SearchResults";
import { search as apiSearch } from "./api";
import type { SearchResult } from "./types";
import { addDocument as apiAddDocument } from "./api";

export default function App() {
  const [query, setQuery] = useState("");
  const [k, setK] = useState(25);
  const [loading, setLoading] = useState(false);
  const [results, setResults] = useState<SearchResult[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [backendSearchMs, setBackendSearchMs] = useState<number | null>(null);
  const [backendTotalMs, setBackendTotalMs] = useState<number | null>(null);

  async function onSubmit() {
    setError(null);
    setLoading(true);
    try {
      const data = await apiSearch(query, k);
      setResults(data.results);

      setBackendSearchMs(data.search_time_ms ?? null);
      setBackendTotalMs(data.total_time_ms ?? null);
    } catch (e: any) {
      setError(e?.message ?? String(e));
      setResults([]);
    } finally {
      setLoading(false);
    }
  }

  return (
    <div className="bg-light min-vh-100 py-4">
      <div className="container" style={{ maxWidth: 980 }}>
        <header className="mb-4">
          <h1 className="h3 fw-bold mb-1">NextSearch</h1>
          <div className="text-secondary">A Scalable Search Engine</div>
        </header>

        <button
          className="btn btn-outline-dark mb-4"
          onClick={async () => {
            const cord_root = prompt("CORD_ROOT (absolute path on server)?") ?? "";
            const json_relpath = prompt("JSON_REL_PATH (relative path)?") ?? "";
            const cord_uid = prompt("CORD_UID?") ?? "";
            const title = prompt("TITLE?") ?? "";
            if (!cord_root || !json_relpath || !cord_uid || !title) return;

            try {
              const out = await apiAddDocument({ cord_root, json_relpath, cord_uid, title });
              alert(
                `Added: ${out.segment} (reloaded=${out.reloaded}) time=${out.total_time_ms?.toFixed?.(2)}ms`
              );
            } catch (e: any) {
              alert(e?.message ?? String(e));
            }
          }}
        >
          Add Document
        </button>

        <SearchBar
          query={query}
          k={k}
          loading={loading}
          onChangeQuery={setQuery}
          onChangeK={setK}
          onSubmit={onSubmit}
        />

        {(backendSearchMs != null || backendTotalMs != null) && (
          <div className="mt-2 small text-secondary">
            {backendSearchMs != null && <>Search: {backendSearchMs.toFixed(2)} ms</>}
            {backendTotalMs != null && <> • Backend total: {backendTotalMs.toFixed(2)} ms</>}
          </div>
        )}

        {error ? (
          <div className="alert alert-danger mt-3 mb-0">
            <div className="fw-semibold">{error}</div>
            <div className="mt-2 small">
              Make sure backend is running: <code>./api_server &lt;INDEX_DIR&gt; 8080</code>
            </div>
          </div>
        ) : null}

        <SearchResults results={results} />
      </div>
    </div>
  );
}
