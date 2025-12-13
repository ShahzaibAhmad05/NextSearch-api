// src/api.ts
import type { SearchResponse, SearchResult } from "./types";

const API_BASE_URL = "http://localhost:8000"; // <-- change to your backend

export async function searchNextSearch(
  query: string,
  page: number = 1,
  pageSize: number = 10
): Promise<SearchResponse> {
  if (!query.trim()) {
    return {
      results: [],
      total: 0,
      page,
      pageSize,
    };
  }

  try {
    const url = new URL("/api/search", API_BASE_URL);
    url.searchParams.set("q", query);
    url.searchParams.set("page", String(page));
    url.searchParams.set("pageSize", String(pageSize));

    const res = await fetch(url.toString());
    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`);
    }

    // Adjust this shape if your backend returns something different
    const data = (await res.json()) as SearchResponse;
    return data;
  } catch (err) {
    console.error("Search API error, falling back to mock data:", err);

    // Simple mock so UI is usable even without backend
    const mock: SearchResult[] = [
      {
        cord_uid: "mock-1",
        title: "Sample COVID-19 Article for Development",
        abstract:
          "This is a mock abstract to demonstrate how results will look in the NextSearch UI.",
        authors: "Doe J, Smith A",
        publish_time: "2020-03-15",
        journal: "Journal of Mock Results",
        doi: "10.0000/mock.doi",
        url: "https://example.org/mock-article",
        source_x: "CORD-19",
      },
    ];

    return {
      results: mock,
      total: mock.length,
      page: 1,
      pageSize: mock.length,
    };
  }
}
