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

    const fixedNav = document.querySelector(".navbar.fixed-top") as HTMLElement | null;
    const fixedNavH = fixedNav?.getBoundingClientRect().height ?? 0;

    // Add this class to your sticky wrapper in App.tsx: "search-sticky"
    const stickySearch = document.querySelector(".search-sticky") as HTMLElement | null;
    const stickySearchH = stickySearch?.getBoundingClientRect().height ?? 0;

    const headerOffset = 5 * fixedNavH + stickySearchH + 12; // small breathing room
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

  const goTo = (p: number) => setPage(Math.min(Math.max(1, p), totalPages));

  if (!results.length) {
    return <div className="mt-3 text-secondary">No results.</div>;
  }

  return (
    <div className="mt-3">
      {/* Anchor: we scroll to here */}
      <div ref={topRef} />

      <div className="d-grid gap-3">
        {pageResults.map((r, idx) => {
          const domain = r.url ? safeHostname(r.url) : null;

          return (
            <div
              key={r.docId}
              className="card card-hover shadow-sm fade-in-up"
              style={{ animationDelay: `${idx * 30}ms` }}
            >
              <div className="card-body">
                <div className="d-flex align-items-start justify-content-between gap-3">
                  <div className="flex-grow-1">
                    {/* 1) Title */}
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

                    {/* 3) Author, publish date */}
                    <div className="small text-secondary mt-1">
                      {formatByline(r)}
                    </div>

                    {/* View at {domain} */}
                    {r.url && domain && (
                      <div className="mt-2">
                        <a
                          href={r.url}
                          target="_blank"
                          rel="noreferrer"
                          className="btn btn-sm btn-view-at"
                        >
                          View at {domain} →
                        </a>
                      </div>
                    )}
                  </div>
                </div>
              </div>
            </div>
          );
        })}
      </div>

      {/* Pagination (improved styling) */}
      {totalPages > 1 && (
        <div className="mt-4">
          <div className="d-flex flex-column align-items-center gap-2">
            <nav aria-label="Search results pages" className="w-100">
              <ul
                className="pagination justify-content-center flex-wrap mb-0"
                style={{ gap: 6 }}
              >
                <li className={`page-item ${safePage === 1 ? "disabled" : ""}`}>
                  <button
                    className="page-link rounded-pill border-0"
                    type="button"
                    onClick={() => goTo(1)}
                    aria-label="First page"
                  >
                    «
                  </button>
                </li>

                <li className={`page-item ${safePage === 1 ? "disabled" : ""}`}>
                  <button
                    className="page-link rounded-pill border-0"
                    type="button"
                    onClick={() => goTo(safePage - 1)}
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
                      className="page-link rounded-pill border-0"
                      type="button"
                      onClick={() => goTo(it)}
                      aria-current={it === safePage ? "page" : undefined}
                      style={{
                        minWidth: 40,
                        textAlign: "center",
                        border: it === safePage ? "none" : undefined,
                      }}
                    >
                      {it}
                    </button>
                  </li>
                ))}

                <li
                  className={`page-item ${safePage === totalPages ? "disabled" : ""}`}
                >
                  <button
                    className="page-link rounded-pill border-0"
                    type="button"
                    onClick={() => goTo(safePage + 1)}
                    aria-label="Next page"
                  >
                    ›
                  </button>
                </li>

                <li
                  className={`page-item ${safePage === totalPages ? "disabled" : ""}`}
                >
                  <button
                    className="page-link rounded-pill border-0"
                    type="button"
                    onClick={() => goTo(totalPages)}
                    aria-label="Last page"
                  >
                    »
                  </button>
                </li>
              </ul>
            </nav>

            <div className="small text-secondary">
              Page <span className="fw-semibold">{safePage}</span> of{" "}
              <span className="fw-semibold">{totalPages}</span>
            </div>
          </div>
        </div>
      )}

      <br />
    </div>
  );
}

/**
 * Formats "author name, publish date".
 * Supports either `author` or `authors`.
 */
function formatByline(r: SearchResult) {
  const anyR = r as unknown as {
    author?: unknown;
    authors?: unknown;
    publish_time?: unknown;
  };

  const authorRaw = anyR.author ?? anyR.authors;
  const author =
    authorRaw != null && String(authorRaw).trim()
      ? String(authorRaw).trim()
      : "—";

  const dateRaw = anyR.publish_time;
  const date =
    dateRaw != null && String(dateRaw).trim()
      ? String(dateRaw).trim()
      : "—";

  return `${author}, ${date}`;
}

function safeHostname(url?: string) {
  if (!url) return "";
  try {
    return new URL(url).hostname.replace(/^www\./, "");
  } catch {
    return url;
  }
}
