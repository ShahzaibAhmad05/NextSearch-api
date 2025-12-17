// src/components/SearchResults.tsx
import React, { useEffect, useMemo, useRef, useState } from "react";
import type { SearchResult } from "../types";

type Props = {
  results: SearchResult[];
  /** Number of results per page (default: 10) */
  pageSize?: number;
};

export default function SearchResults({ results, pageSize = 10 }: Props) {
  const [page, setPage] = useState(1);
  const topRef = useRef<HTMLDivElement | null>(null);

  const totalPages = Math.max(1, Math.ceil(results.length / pageSize));
  const safePage = Math.min(Math.max(1, page), totalPages);

  // When changing pages, scroll to the top of results,
  // but keep fixed/sticky header visible (do not hide it).
  useEffect(() => {
    const el = topRef.current;
    if (!el) return;

    // Measure your fixed navbar height
    const fixedNav = document.querySelector(".navbar.fixed-top") as HTMLElement | null;
    const fixedNavH = fixedNav?.getBoundingClientRect().height ?? 0;

    // Measure your sticky search block height (add this class in App.tsx: "search-sticky")
    const stickySearch = document.querySelector(".search-sticky") as HTMLElement | null;
    const stickySearchH = stickySearch?.getBoundingClientRect().height ?? 0;

    const headerOffset = 5*(fixedNavH) + stickySearchH; // small gap
    const y = el.getBoundingClientRect().top + window.scrollY - headerOffset;

    window.scrollTo({ top: Math.max(0, y), behavior: "smooth" });
  }, [safePage]);

  // Reset to first page whenever the result set changes (e.g., new search or re-sort).
  useEffect(() => {
    setPage(1);
  }, [results]);

  const pageResults = useMemo(() => {
    const start = (safePage - 1) * pageSize;
    return results.slice(start, start + pageSize);
  }, [results, safePage, pageSize]);

  const pageItems = useMemo(
    () => Array.from({ length: totalPages }, (_, i) => i + 1),
    [totalPages]
  );

  if (!results.length) {
    return <div className="mt-3 text-secondary">No results.</div>;
  }

  return (
    <div className="mt-3">
      {/* Anchor: we scroll to here */}
      <div ref={topRef} />

      <div className="d-grid gap-3">
        {pageResults.map((r, idx) => (
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

                  <div className="small text-secondary mt-1">
                    Published: {r.publish_time ?? "—"}
                  </div>

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

      {totalPages > 1 && (
        <nav className="mt-4" aria-label="Search results pages">
          <ul className="pagination justify-content-center flex-wrap mb-0">
            <li className={`page-item ${safePage === 1 ? "disabled" : ""}`}>
              <button
                className="page-link"
                type="button"
                onClick={() => setPage((p) => Math.max(1, p - 1))}
                aria-label="Previous page"
              >
                ‹
              </button>
            </li>

            {pageItems.map((it) => (
              <li
                key={`page-${it}`}
                className={`page-item ${it === safePage ? "active" : ""}`}
              >
                <button
                  className="page-link"
                  type="button"
                  onClick={() => setPage(it)}
                  aria-current={it === safePage ? "page" : undefined}
                >
                  {it}
                </button>
              </li>
            ))}

            <li className={`page-item ${safePage === totalPages ? "disabled" : ""}`}>
              <button
                className="page-link"
                type="button"
                onClick={() => setPage((p) => Math.min(totalPages, p + 1))}
                aria-label="Next page"
              >
                ›
              </button>
            </li>
          </ul>

          <div className="text-center small text-secondary mt-2">
            Page {safePage} of {totalPages}
          </div>
        </nav>
      )}

      <br />
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
