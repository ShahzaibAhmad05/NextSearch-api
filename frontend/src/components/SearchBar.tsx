// src/components/SearchBar.tsx
import React from "react";
import type { FormEvent } from "react";


interface SearchBarProps {
  query: string;
  setQuery: (value: string) => void;
  onSubmit: () => void;
  isLoading?: boolean;
}

const SearchBar: React.FC<SearchBarProps> = ({
  query,
  setQuery,
  onSubmit,
  isLoading = false,
}) => {
  const handleSubmit = (e: FormEvent) => {
    e.preventDefault();
    onSubmit();
  };

  return (
    <form className="search-bar" onSubmit={handleSubmit}>
      <div className="search-input-wrapper">
        <input
          className="search-input"
          type="text"
          value={query}
          onChange={(e) => setQuery(e.target.value)}
          placeholder="type to search..."
          autoFocus
        />
        <button
          className="search-button"
          type="submit"
          disabled={!query.trim() || isLoading}
        >
          {isLoading ? "Searching..." : "Search"}
        </button>
      </div>
    </form>
  );
};

export default SearchBar;
