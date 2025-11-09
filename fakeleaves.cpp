#include "cdbdirect.h"
#include "cdbshorts.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

#include "external/chess.hpp"
using namespace chess;

int main() {
  std::uintptr_t handle = cdbdirect_initialize(CHESSDB_PATH);

  std::uint64_t db_size = cdbdirect_size(handle);
  std::cout << "DB count: " << db_size << std::endl;

  // setup of a function that will be called for each entry in the db,
  // multithreaded
  size_t max_entries = std::min(10'000'000UL, db_size);
  std::atomic<size_t> count_total(0);
  constexpr int num_buckets = 256;
  std::array<std::atomic<size_t>, num_buckets> count_fake{};
  auto start = std::chrono::steady_clock::now();

  auto evaluate_entry =
      [&](const std::string &fen,
          const std::vector<std::pair<std::string, int>> &scored) {
        // count entries
        size_t peek = count_total.fetch_add(1, std::memory_order_relaxed);
        // status update
        if (peek % 10000 == 0 && peek > 0) {
          auto end = std::chrono::steady_clock::now();
          auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
              end - start);
          std::cout << "Counted               " << peek << " of " << max_entries
                    << " entries so far..."
                    << "\n";
          std::cout << "  Time (s):           " << elapsed.count() / 1000.0
                    << "\n";
          std::cout << "  nps:                " << peek * 1000 / elapsed.count()
                    << "\n";
          std::cout << "  ETA (s):            "
                    << (max_entries - peek) * elapsed.count() / (peek * 1000)
                    << "\n";
          std::cout << std::endl;
        }

        if (scored.size() !=
            2) // only retain positions with exactly 1 scored move
          return peek < max_entries;

        Board board(fen);
        Movelist moves;
        movegen::legalmoves(moves, board);

        if (moves.size() <= 1)
          return peek < max_entries;

        Move move = uci::uciToMove(board, scored[0].first);
        board.makeMove<true>(move);
        auto r = cdbdirect_wrapper(handle, board);
        board.unmakeMove(move);

        if (r.size() > 1)
          count_fake[std::clamp(scored[1].second, 0, num_buckets - 1)]++;

        return peek < max_entries; // continue iteration as long as true
      };

  // evaluate all entries in the db, using multiple threads
  const size_t num_threads = std::thread::hardware_concurrency();
  cdbdirect_apply(handle, num_threads, evaluate_entry);

  std::cout << "Fake leaf distribution after " << max_entries
            << " entries written to fake_leaves_distribution.xy\n";
  std::ofstream fake_leaves_file("fake_leaves_distribution.xy");
  size_t total_fake = 0;
  for (size_t i = 0; i < num_buckets; ++i) {
    fake_leaves_file << i << " " << count_fake[i] << "\n";
    total_fake += count_fake[i].load();
  }
  fake_leaves_file.close();
  std::cout << "Total fake leaves: " << total_fake << "\n";
  std::cout << "Percentage: " << 100 * double(total_fake) / max_entries
            << std::endl;

  cdbdirect_finalize(handle);
  return 0;
}
