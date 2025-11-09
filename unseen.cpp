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
  std::atomic<size_t> count_unseen(0);
  std::mutex io_mutex;
  std::ofstream file_fens("unseen.epd");
  auto start = std::chrono::steady_clock::now();

  auto evaluate_entry =
      [&](const std::string &fen,
          const std::vector<std::pair<std::string, int>> &scored) {
        // count entries
        size_t peek = count_total.fetch_add(1, std::memory_order_relaxed);

        Board board(fen);
        Movelist moves;
        movegen::legalmoves(moves, board);
        for (const auto &move : moves) {
          auto uci_move = uci::moveToUci(move);
          auto it = std::find_if(
              scored.begin(), scored.end(),
              [&uci_move](const auto &p) { return p.first == uci_move; });
          if (it != scored.end())
            continue;
          board.makeMove<true>(move);
          auto r = cdbdirect_wrapper(handle, board);
          board.unmakeMove(move);
          if (r.size() > 1 && -r[0].second > scored[0].second + 5) {
            count_unseen.fetch_add(1, std::memory_order_relaxed);
            const std::lock_guard<std::mutex> lock(io_mutex);
            // write to file
            file_fens << fen << " " << uci_move << " " << -r[0].second << " "
                      << scored[0].first << " " << scored[0].second << "\n";
            break;
          }
        }

        // status update
        if (peek % 10000 == 0 && peek > 0) {
          auto end = std::chrono::steady_clock::now();
          auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
              end - start);
          std::cout << "Counted               " << peek << " of " << max_entries
                    << " entries so far..."
                    << "\n";
          std::cout << "  eval changes:       " << count_unseen.load() << "\n";
          std::cout << "  Time (s):           " << elapsed.count() / 1000.0
                    << "\n";
          std::cout << "  eps:                "
                    << count_unseen.load() * 1000 / elapsed.count() << "\n";
          std::cout << "  nps:                " << peek * 1000 / elapsed.count()
                    << "\n";
          std::cout << "  ETA (s):            "
                    << (max_entries - peek) * elapsed.count() / (peek * 1000)
                    << "\n";
          std::cout << std::endl;
        }
        return peek < max_entries; // continue iteration as long as true
      };

  // evaluate all entries in the db, using multiple threads
  const size_t num_threads = std::thread::hardware_concurrency();
  cdbdirect_apply(handle, num_threads, evaluate_entry);

  file_fens.close();

  cdbdirect_finalize(handle);
  return 0;
}
