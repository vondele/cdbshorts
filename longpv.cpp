#include "cdbdirect.h"
#include <cmath>
#include <cstdint>
#include <iostream>

#include "external/chess.hpp"
using namespace chess;

int pv_explore(Board &board, const int margin, int budget, size_t &query_count,
               const std::uintptr_t handle) {

  int pv_len = 0;

  if (budget < 0)
    return pv_len;

  auto res = board.isGameOver();
  if (res.second != GameResult::NONE)
    return pv_len;

  query_count++;
  auto r = cdbdirect_get(handle, board.getFen(false));

  if (r.size() < 2)
    return pv_len;

  auto &best_move = r[0];

  for (auto &m : r) {

    if (m.first == "a0a0")
      break;

    if (m.first != best_move.first && m.second + margin < best_move.second)
      break;

    Move move = uci::uciToMove(board, m.first);
    board.makeMove<true>(move);
    pv_len = std::max(
        pv_len, 1 + pv_explore(board, margin, budget, query_count, handle));
    board.unmakeMove(move);

    budget--;
  }

  return pv_len;
}

int main() {
  std::uintptr_t handle = cdbdirect_initialize(CHESSDB_PATH);

  std::uint64_t db_size = cdbdirect_size(handle);
  std::cout << "DB count: " << db_size << std::endl;

  std::string fen;
  fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";   // startpos
  fen = "rnbqkbnr/pppppppp/8/8/6P1/8/PPPPPP1P/RNBQKBNR b KQkq -"; // 1. g4
  fen =
      "r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq -"; // ruy
                                                                       // lopez
  fen = "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq -";      // 1. d4

  Board board(fen);

  std::cout << "Looking at fen: " << fen << std::endl;

  for (int budget = 1; budget < 10; budget += 2) {
    for (int margin = -1; margin <= 3; margin += 1) {
      size_t query_count = 0;
      int len = pv_explore(board, margin, budget, query_count, handle);
      std::cout << "cp margin from bestmove " << margin << " search budget "
                << budget << " found PV len: " << len << " using "
                << query_count << " queries." << std::endl;
    }
    std::cout << std::endl;
  }

  cdbdirect_finalize(handle);
  return 0;
}
