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
      {results.map((r) => (
        <div key={r.docId} className="card shadow-sm">
          <div className="card-body">
            <div className="d-flex justify-content-between gap-3">
              <div className="fw-bold">
                {r.url ? (
                  <a className="text-decoration-none" href={r.url} target="_blank" rel="noreferrer">
                    {r.title || "(untitled)"}
                  </a>
                ) : (
                  <span>{r.title || "(untitled)"}</span>
                )}
              </div>
              <div className="text-secondary small">score: {r.score.toFixed(4)}</div>
            </div>

            <div className="mt-2 small text-secondary">
              docId: {r.docId} • segment: <code>{r.segment}</code> • cord_uid:{" "}
              <code>{r.cord_uid}</code>
            </div>

            <div className="mt-2 small">
              json_relpath: <code>{r.json_relpath}</code>
            </div>
          </div>
        </div>
      ))}
    </div>
  );
}
