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
  size_t max_entries = std::min(100'000'000UL, db_size);
  std::atomic<size_t> count_total(0);
  auto start = std::chrono::steady_clock::now();
  std::mutex io_mutex;
  std::ofstream file_fens("book.epd");
  std::atomic<size_t> count_book(0);

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
          std::cout << "  Book entries found: " << count_book.load() << "\n";
          std::cout << "  Time (s):           " << elapsed.count() / 1000.0
                    << "\n";
          std::cout << "  nps:                " << peek * 1000 / elapsed.count()
                    << "\n";
          std::cout << "  ETA (s):            "
                    << (max_entries - peek) * elapsed.count() / (peek * 1000)
                    << "\n";
          std::cout << std::endl;
        }

        if (scored.size() <= 5) // not fully explored
          return peek < max_entries;

        if (scored.back().second < 0 ||
            scored.back().second >
                16) // min_ply should be defined and less than 16
          return peek < max_entries;

        // position is close to 100, i.e. 50% winning chance
        if (std::abs(scored[0].second) > 105 or std::abs(scored[0].second) < 95)
          return peek < max_entries;

        // collect properties of the PV
        Board board(fen);
        Move move = uci::uciToMove(board, scored[0].first);
        constexpr int pv_len = 10;
        std::array<Move, pv_len> pv;
        int ply = 0;
        int uncertain = 0;
        while (true) {
          pv[ply] = move;
          board.makeMove<true>(move);
          auto r = cdbdirect_wrapper(handle, board);
          if (r.size() < 2 || ply == pv_len - 1)
            break;
          if (r.size() >= 3 &&
              r[0].second - r[1].second <
                  10) // difference between the first two moves is small
            uncertain++;
          ply++;
          move = uci::uciToMove(board, r[0].first);
        }
        int max_ply = ply;
        while (ply >= 0) {
          board.unmakeMove(pv[ply]);
          ply--;
        }

        // Finally, if the pv is long enough and many uncertain moves, book it
        if (max_ply == pv_len - 1 && uncertain * 2 > pv_len) {
          count_book.fetch_add(1, std::memory_order_relaxed);
          const std::lock_guard<std::mutex> lock(io_mutex);
          file_fens << fen << "\n";
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
