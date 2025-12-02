// src/types.ts
export interface SearchResult {
  cord_uid: string;
  sha?: string | null;
  source_x?: string | null;
  title: string;
  doi?: string | null;
  pmcid?: string | null;
  pubmed_id?: string | null;
  license?: string | null;
  abstract?: string | null;
  publish_time?: string | null;
  authors?: string | null;
  journal?: string | null;
  mag_id?: string | null;
  who_covidence_id?: string | null;
  arxiv_id?: string | null;
  pdf_json_files?: string | null;
  pmc_json_files?: string | null;
  url?: string | null;
  s2_id?: string | null;
}

export interface SearchResponse {
  results: SearchResult[];
  total: number;
  page: number;
  pageSize: number;
}
