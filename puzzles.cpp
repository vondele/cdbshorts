#include "cdbdirect.h"
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

float cp_to_win(int cp) { return 1.0 / (1.0 + std::exp((100 - cp) / 22.0)); }

float cp_to_score(int cp) {
  float w = cp_to_win(cp);
  float l = cp_to_win(-cp);
  float d = 1.0 - w - l;
  return w + d * 0.5;
}

int main() {
  std::uintptr_t handle = cdbdirect_initialize(CHESSDB_PATH);

  std::uint64_t db_size = cdbdirect_size(handle);
  std::cout << "DB count: " << db_size << std::endl;

  // setup of a function that will be called for each entry in the db,
  // multithreaded
  size_t max_entries = std::min(10'000'000UL, db_size);
  std::atomic<size_t> count_total(0);
  std::atomic<size_t> count_puzzle(0);
  std::mutex io_mutex;
  std::ofstream file_fens("puzzles.epd");
  auto start = std::chrono::steady_clock::now();

  auto evaluate_entry = [&](const std::string &fen,
                            const std::vector<std::pair<std::string, int>>
                                &scored) {
    // count entries
    size_t peek = count_total.fetch_add(1, std::memory_order_relaxed);

    if (scored.size() >= 6) {
      auto diff = cp_to_score(scored[0].second) - cp_to_score(scored[1].second);
      if (diff > 0.4) {
        Board board(fen);
        if (!board.inCheck()) {
          Move m = uci::uciToMove(board, scored[0].first);
          if (peek % 10 == 0 && !board.isCapture(m) &&
              m.promotionType() != PieceType::NONE &&
              board.givesCheck(m) == CheckType::NO_CHECK) // non-forcing move
          {
            count_puzzle.fetch_add(1, std::memory_order_relaxed);
            const std::lock_guard<std::mutex> lock(io_mutex);
            // write to file
            file_fens << fen << " ; " << scored[0].first << "\n";
          }
        }
      }
    }

    // status update
    if (peek % 10000000 == 0 && peek > 0) {
      auto end = std::chrono::steady_clock::now();
      auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
      std::cout << "Counted               " << peek << " of " << max_entries
                << " entries so far..."
                << "\n";
      std::cout << "  Puzzles found:      " << count_puzzle.load() << "\n";
      std::cout << "  Time (s):           " << elapsed.count() / 1000.0 << "\n";
      std::cout << "  pps:                "
                << count_puzzle.load() * 1000 / elapsed.count() << "\n";
      std::cout << "  nps:                " << peek * 1000 / elapsed.count()
                << "\n";
      std::cout << "  ETA (s):            "
                << (max_entries - peek) * elapsed.count() / (peek * 1000)
                << "\n";
      /*
      std::cout << fen << "\n";
      for (auto &e : scored) {
        std::cout << e.first << " " << e.second << "\n";
      }
      */
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
