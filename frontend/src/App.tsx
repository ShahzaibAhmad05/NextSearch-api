// src/App.tsx
import React, { useState, useEffect } from "react";
import "./App.css";
import SearchBar from "./components/SearchBar";
import SearchResults from "./components/SearchResults";
import { searchNextSearch } from "./api";
import type { SearchResult } from "./types";

const PAGE_SIZE = 10;

const App: React.FC = () => {
  const [query, setQuery] = useState("");
  const [results, setResults] = useState<SearchResult[]>([]);
  const [total, setTotal] = useState(0);
  const [page, setPage] = useState(1);
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const performSearch = async (nextPage: number = 1) => {
    if (!query.trim()) {
      setResults([]);
      setTotal(0);
      setPage(1);
      setError(null);
      return;
    }

    setIsLoading(true);
    setError(null);

    try {
      const data = await searchNextSearch(query, nextPage, PAGE_SIZE);
      setResults(data.results);
      setTotal(data.total);
      setPage(data.page);
    } catch (err) {
      console.error(err);
      setError("Something went wrong while searching. Please try again.");
    } finally {
      setIsLoading(false);
    }
  };

  // When page changes via pagination
  const handlePageChange = (nextPage: number) => {
    performSearch(nextPage);
  };

  // Optional: re-run search when query changes & page is not 1
  useEffect(() => {
    if (page !== 1) {
      setPage(1);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [query]);

  return (
    <div className="app-root">
      <div className="app-shell">
        <header className="app-header">
          <div className="brand">
            <span className="brand-logo">N</span>
            <span className="brand-name">NextSearch</span>
          </div>
        </header>

        <main className="app-main">
          <SearchBar
            query={query}
            setQuery={setQuery}
            onSubmit={() => performSearch(1)}
            isLoading={isLoading}
          />

          {error && <div className="error-banner">{error}</div>}

          <SearchResults
            results={results}
            total={total}
            page={page}
            pageSize={PAGE_SIZE}
            onPageChange={handlePageChange}
            query={query}
          />
        </main>
      </div>

      <footer className="app-footer">
        <span>copyright NextSearch</span>
      </footer>
    </div>
  );
};

export default App;
