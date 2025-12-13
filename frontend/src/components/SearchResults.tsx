// src/components/SearchResults.tsx
import React from "react";
import type { SearchResult } from "../types";

interface SearchResultsProps {
  results: SearchResult[];
  total: number;
  page: number;
  pageSize: number;
  onPageChange: (page: number) => void;
  query: string;
}

const formatAuthors = (authors?: string | null) => {
  if (!authors) return "Unknown authors";
  const parts = authors.split(";").map((a) => a.trim()).filter(Boolean);
  if (parts.length === 0) return "Unknown authors";
  if (parts.length === 1) return parts[0];
  if (parts.length === 2) return `${parts[0]} & ${parts[1]}`;
  return `${parts[0]} et al.`;
};

const formatDate = (d?: string | null) => (d ? d : "Unknown date");

const SearchResults: React.FC<SearchResultsProps> = ({
  results,
  total,
  page,
  pageSize,
  onPageChange,
  query,
}) => {
  const start = total === 0 ? 0 : (page - 1) * pageSize + 1;
  const end = Math.min(page * pageSize, total);
  const totalPages = pageSize ? Math.ceil(total / pageSize) : 1;

  if (!query.trim()) {
    return (
      <div className="results-empty">
        <p>Type a query above to search.</p>
      </div>
    );
  }

  if (query.trim() && results.length === 0) {
    return (
      <div className="results-empty">
        <p>No results found for “{query}”. Try different keywords.</p>
      </div>
    );
  }

  return (
    <div className="results-container">
      <div className="results-summary">
        <span>
          Showing <strong>{start}</strong>–<strong>{end}</strong> of{" "}
          <strong>{total}</strong> results for “{query}”
        </span>
        {totalPages > 1 && (
          <span className="results-pages">
            Page <strong>{page}</strong> of <strong>{totalPages}</strong>
          </span>
        )}
      </div>

      <ul className="results-list">
        {results.map((res) => (
          <li key={res.cord_uid} className="result-card">
            <div className="result-header">
              {res.url ? (
                <a
                  href={res.url}
                  target="_blank"
                  rel="noreferrer"
                  className="result-title-link"
                >
                  {res.title || "Untitled article"}
                </a>
              ) : (
                <h3 className="result-title">
                  {res.title || "Untitled article"}
                </h3>
              )}
              <div className="result-meta-line">
                <span>{formatAuthors(res.authors)}</span>
                <span>•</span>
                <span>{res.journal || "Unknown journal"}</span>
                <span>•</span>
                <span>{formatDate(res.publish_time)}</span>
              </div>
            </div>

            {res.abstract && (
              <p className="result-abstract">
                {res.abstract.length > 300
                  ? res.abstract.slice(0, 300) + "..."
                  : res.abstract}
              </p>
            )}

            <div className="result-footer">
              {res.doi && (
                <a
                  href={`https://doi.org/${res.doi}`}
                  target="_blank"
                  rel="noreferrer"
                  className="result-chip"
                >
                  DOI: {res.doi}
                </a>
              )}
              {res.pmcid && (
                <span className="result-chip">PMCID: {res.pmcid}</span>
              )}
              {res.source_x && (
                <span className="result-chip">Source: {res.source_x}</span>
              )}
              {res.license && (
                <span className="result-chip">License: {res.license}</span>
              )}
            </div>
          </li>
        ))}
      </ul>

      {totalPages > 1 && (
        <div className="results-pagination">
          <button
            onClick={() => onPageChange(page - 1)}
            disabled={page <= 1}
          >
            Previous
          </button>
          <span>
            Page {page} of {totalPages}
          </span>
          <button
            onClick={() => onPageChange(page + 1)}
            disabled={page >= totalPages}
          >
            Next
          </button>
        </div>
      )}
    </div>
  );
};

export default SearchResults;
