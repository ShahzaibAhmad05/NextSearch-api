// src/components/SearchResults.tsx
import React from "react";
import type { SearchResult } from "../types";

type Props = {
  results: SearchResult[];
};

export default function SearchResults({ results }: Props) {
  if (!results.length) {
    return <div className="mt-3 text-secondary">No results.</div>;
  }

  return (
    <div className="mt-3 d-grid gap-3">
      {results.map((r, idx) => (
        <div
          key={r.docId}
          className="card card-hover shadow-sm fade-in-up"
          style={{ animationDelay: `${idx * 30}ms` }}
        >
          <div className="card-body">
            <div className="d-flex align-items-start justify-content-between gap-3">
              <div className="flex-grow-1">
                <div className="fw-semibold fs-6 line-clamp-2">
                  {r.url ? (
                    <a
                      className="clean-link"
                      href={r.url}
                      target="_blank"
                      rel="noreferrer"
                    >
                      {r.title || "(untitled)"}
                    </a>
                  ) : (
                    <span>{r.title || "(untitled)"}</span>
                  )}
                </div>

                <br />

                {/* View Article button */}
                {r.url && (
                  <div className="mt-2">
                    <a
                      href={r.url}
                      target="_blank"
                      rel="noreferrer"
                      className="btn btn-sm btn-outline-primary"
                    >
                      View article →
                    </a>
                  </div>
                )}
              </div>

              <div className="text-end">
                {r.url ? (
                  <div
                    className="small text-secondary mt-2 truncate"
                    style={{ maxWidth: 220 }}
                  >
                    {safeHostname(r.url)}
                  </div>
                ) : null}
              </div>
            </div>
          </div>
        </div>
      ))}
    </div>
  );
}

function safeHostname(url?: string) {
  if (!url) return "";
  try {
    return new URL(url).hostname.replace(/^www\./, "");
  } catch {
    return url;
  }
}
