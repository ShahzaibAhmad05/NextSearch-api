// src/App.tsx
import React, { useMemo, useState } from "react";
import SearchBar from "./components/SearchBar";
import SearchResults from "./components/SearchResults";
import AddDocumentModal from "./components/AddDocumentModal";
import { search as apiSearch } from "./api";
import type { SearchResult } from "./types";

type SortBy =
  | "Relevancy"
  | "Publish Date (Newest)"
  | "Publish Date (Oldest)";

function publishTimeToMs(iso?: string) {
  if (!iso) return NaN;
  const t = Date.parse(iso);
  return Number.isNaN(t) ? NaN : t;
}

export default function App() {
  const [query, setQuery] = useState("");
  const [k, setK] = useState(100);
  const [loading, setLoading] = useState(false);
  const [showAdvanced, setShowAdvanced] = useState(false);
  const [results, setResults] = useState<SearchResult[]>([]);
  const [error, setError] = useState<string | null>(null);

  const [backendTotalMs, setBackendTotalMs] = useState<number | null>(null);
  const [hasSearched, setHasSearched] = useState(false);

  const [showAddModal, setShowAddModal] = useState(false);

  const [sortBy, setSortBy] = useState<SortBy>("Relevancy");
  const [showSort, setShowSort] = useState(false);

  async function onSubmit() {
    if (!query.trim()) return;

    setError(null);
    setLoading(true);

    try {
      const data = await apiSearch(query, k);
      setResults(data.results);
      setHasSearched(true);
      setBackendTotalMs(data.total_time_ms ?? null);
    } catch (e: any) {
      setError(e?.message ?? String(e));
      setResults([]);
      setHasSearched(true);
      setBackendTotalMs(null);
    } finally {
      setLoading(false);
    }
  }

  const status = useMemo(() => {
    if (!hasSearched) return "";
    if (loading) return "Searching…";
    if (error) return "Error fetching results";
    if (results.length === 0) return "No results found";

    const parts: string[] = [
      `Found ${results.length} result${results.length === 1 ? "" : "s"}`,
    ];
    if (backendTotalMs != null)
      parts.push(`in ${backendTotalMs.toFixed(2)} ms`);
    return parts.join(" ");
  }, [hasSearched, loading, error, results.length, backendTotalMs]);

  const sortedResults = useMemo(() => {
    const copy = [...results];

    if (sortBy !== "Relevancy") {
      copy.sort((a, b) => {
        const ta = publishTimeToMs(a.publish_time);
        const tb = publishTimeToMs(b.publish_time);

        const aBad = Number.isNaN(ta);
        const bBad = Number.isNaN(tb);
        if (aBad && bBad) return 0;
        if (aBad) return 1;
        if (bBad) return -1;

        return sortBy === "Publish Date (Newest)" ? tb - ta : ta - tb;
      });
    }

    return copy;
  }, [results, sortBy]);

  return (
    <div className="bg-light min-vh-100">
      {/* Top bar */}
      <nav className="navbar navbar-expand-lg bg-white border-bottom fixed-top">
        <div className="container" style={{ maxWidth: 980 }}>
          <a className="navbar-brand fw-bold" href="#">
          </a>
          <div className="d-flex gap-2">
            <button
              className="btn btn-outline-dark"
              onClick={() => setShowAddModal(true)}
            >
              Add Document
            </button>
          </div>
        </div>
      </nav>

      {/* ======================
          PRE-SEARCH (CENTERED)
          ====================== */}
      {!hasSearched && (
        <div
          className="d-flex align-items-center justify-content-center"
          style={{ minHeight: "100vh" }}
        >
          <div className="container" style={{ maxWidth: 1000 }}>
            <div className="text-center mb-4">
              <h1 className="display-4 fw-bold mb-2">NextSearch</h1>
              <div className="text-secondary">
                through 60k+ Cord19 research papers
              </div>
            </div>

            <div className="card-body">
              <SearchBar
                query={query}
                k={k}
                loading={loading}
                onChangeQuery={setQuery}
                onChangeK={setK}
                onSubmit={onSubmit}
              />
            </div>
          </div>
        </div>
      )}

      {/* ======================
          POST-SEARCH (NORMAL)
          ====================== */}
      {hasSearched && (
        <div className="pt-5">
          <div className="container pt-4" style={{ maxWidth: 980 }}>
            {/* Hero */}
            <div className="py-4">
              <h1 className="display-5 fw-bold mb-2">NextSearch</h1>
              <div className="text-secondary">
                through 60k+ Cord19 research papers
              </div>
            </div>

            {/* Search area */}
            <div className="bg-light py-3">
              <div className="card shadow-sm">
                <div className="card-body">
                  <SearchBar
                    query={query}
                    k={k}
                    loading={loading}
                    onChangeQuery={setQuery}
                    onChangeK={setK}
                    onSubmit={onSubmit}
                  />

                  {status && (
                    <div className="mt-2 small text-secondary">{status}</div>
                  )}

                  <div className="mt-3 d-flex flex-wrap gap-2 align-items-center">
                    {/* Sort dropdown */}
                    <div className="position-relative">
                      <button
                        className="btn btn-sm btn-outline-secondary dropdown-toggle"
                        type="button"
                        onClick={() => setShowSort((v) => !v)}
                      >
                        Sort by {sortBy}
                      </button>

                      {showSort && (
                        <div
                          className="dropdown-menu show"
                          style={{ position: "absolute" }}
                        >
                          {(
                            [
                              "Relevancy",
                              "Publish Date (Newest)",
                              "Publish Date (Oldest)",
                            ] as SortBy[]
                          ).map((opt) => (
                            <button
                              key={opt}
                              className={`dropdown-item ${
                                opt === sortBy ? "active" : ""
                              }`}
                              onClick={() => {
                                setSortBy(opt);
                                setShowSort(false);
                              }}
                              type="button"
                            >
                              {opt}
                            </button>
                          ))}
                        </div>
                      )}
                    </div>
                    <button
                      type="button"
                      className="btn btn-sm btn-outline-secondary"
                      onClick={() => setShowAdvanced(true)}
                    >
                      Advanced
                    </button>
                  </div>

                  {error && (
                    <div className="alert alert-danger mt-3 mb-0">
                      <div className="fw-semibold">{error}</div>
                    </div>
                  )}
                </div>
              </div>
            </div>

            {/* Results */}
            <SearchResults results={sortedResults} />
          </div>
        </div>
      )}

      <AddDocumentModal
        show={showAddModal}
        onClose={() => setShowAddModal(false)}
      />
    </div>
  );
}
