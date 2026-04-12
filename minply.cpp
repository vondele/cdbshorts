#include <chrono>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <queue>
#include <set>
#include <unistd.h>

#include "cdbshorts.hpp"
#include "external/chess.hpp"
#include "external/parallel_hashmap/btree.h"
#include "gameprogress.hpp"

// get memory in MB
std::pair<size_t, size_t> get_memory() {
  size_t tSize = 0, resident = 0, share = 0;
  std::ifstream buffer("/proc/self/statm");
  buffer >> tSize >> resident;
  buffer.close();

  long page_size = sysconf(_SC_PAGE_SIZE);
  return std::make_pair(tSize * page_size / (1024 * 1024),
                        resident * page_size / (1024 * 1024));
};

int main(int argc, char **argv) {
  // DB
  std::uintptr_t handle = cdbdirect_initialize(CHESSDB_PATH);
  std::uint64_t db_size = cdbdirect_size(handle);
  std::cout << "DB count: " << db_size << std::endl;

  // fen
  std::string start_fen;
  start_fen =
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -"; // Startpos
  uint64_t start_depth = 0;
  if (argc != 1 && argc != 3) {
    std::cout << "Wrong count of arguments! Expect either none or fen with "
                 "starting depth"
              << std::endl;
    return 1;
  }
  if (argc == 3) {
    start_fen = argv[1];
    start_depth = std::stoull(argv[2]);
  }
  Board start_board(start_fen, true);
  std::cout << "Counting subtree and minply distribution for fen: " << start_fen
            << std::endl;
  PackedBoard start_packed_board = Board::Compact::encode(start_board);
  auto db_lookup = cdbdirect_wrapper(handle, start_board);
  if (db_lookup.size() < 2) {
    std::cout << "Starting fen is not in cdb" << std::endl;
    return 1;
  }

  // search containers
  std::set<PackedBoard> visited;
  std::set<PackedBoard> added_by_dijkstra;
  std::priority_queue<std::pair<int, PackedBoard>,
                      std::vector<std::pair<int, PackedBoard>>, std::greater<>>
      pq_progress;

  // instead std::map<PackedBoard, int, EarlierBoard> use a more memory
  // efficient btree_map from parallel_hashmap
  phmap::btree_map<PackedBoard, int, EarlierBoard> to_visit;

  // initialize
  to_visit.insert({start_packed_board, start_depth});
  Board current_progress_group = start_board;

  // output
  uint64_t total_num_positions = 0;
  uint64_t improved_cdb_minply = 0;
  uint64_t group_cnt = 0;
  std::vector<uint64_t> num_pos_depth;

  auto start_time = std::chrono::high_resolution_clock::now();

  // iterate over every progress group
  while (!to_visit.empty()) {

    // representative element of current group
    current_progress_group = Board::Compact::decode(to_visit.begin()->first);
    // add all positions of the next progress group in to_visit to pq;
    while (!to_visit.empty()) {
      auto [next_position_packed, next_depth] = *to_visit.begin();
      Board next_position = Board::Compact::decode(next_position_packed);
      if (game_progress(current_progress_group, next_position) !=
          ProgressOutcome::kSame) {
        break;
      }

      pq_progress.push({next_depth, next_position_packed});
      to_visit.erase(to_visit.begin());
    }

    // go over all positions of the current progress group
    // using a variant of a dijkstra algorithm
    while (!pq_progress.empty()) {
      // this will pop the postion of lowest minply
      auto [cur_depth, cur_position_packed] = pq_progress.top();
      pq_progress.pop();
      Board cur_position = Board::Compact::decode(cur_position_packed);

      // already visited with less or equal depth
      if (visited.contains(cur_position_packed)) {
        continue;
      }

      // the minply of this position is now guaranteed
      visited.insert(cur_position_packed);

      while (num_pos_depth.size() <= cur_depth) {
        num_pos_depth.push_back(0);
      }
      num_pos_depth[cur_depth]++;

      // go over all next moves
      auto db_lookup = cdbdirect_wrapper(handle, cur_position);
      for (auto &cur_move : db_lookup) {
        if (cur_move.first == "a0a0") {
          if (cur_depth < cur_move.second || cur_move.second == -1) {
            // TODO: the cur_depth could be used to update the cdb
            improved_cdb_minply++;
          }
          break;
        }

        Move move = cdbuci_to_move(cur_position, cur_move.first);
        cur_position.makeMove<true>(move);
        PackedBoard packed_next_board = Board::Compact::encode(cur_position);

        // already visited or already added by Dijkstra
        if (visited.contains(packed_next_board) ||
            added_by_dijkstra.contains(packed_next_board)) {
          cur_position.unmakeMove(move);
          continue;
        }

        // check if next position is in cdb to save memory
        int dist_to_root =
            cdbdirect_wrapper(handle, cur_position).back().second;
        if (dist_to_root < -1) { // convention for not in cdb
          cur_position.unmakeMove(move);
          continue;
        }

        // handle next position depending on its game progress
        if (game_progress(cur_position, current_progress_group) ==
            ProgressOutcome::kLater) {
          // this position comes later in the game, insert with the current
          // depth unless the existing depth is better already
          auto [to_visit_it, inserted] =
              to_visit.try_emplace(packed_next_board, cur_depth + 1);
          if (!inserted && to_visit_it->second > cur_depth + 1) {
            to_visit_it->second = cur_depth + 1;
          }
        } else {
          // this position belongs to the current group, insert in the
          // priority_queue to avoid accessive duplication store the pq
          // insertion into a map so that it is only added once note that
          // checking if a position is already in a pq is not fast so a second
          // set is needed
          pq_progress.push({cur_depth + 1, packed_next_board});
          added_by_dijkstra.insert(packed_next_board);
        }
        cur_position.unmakeMove(move);
      }
    }

    total_num_positions += visited.size();
    group_cnt++;

    std::cout << "Progress group counter " << std::format("{:10}", group_cnt)
              << " subtree position count "
              << std::format("{:10}", total_num_positions)
              << " current group size " << std::format("{:10}", visited.size())
              << " to_visit size " << std::format("{:10}", to_visit.size())
              << " improved minply "
              << std::format("{:10}", improved_cdb_minply) << "\n";
    if (group_cnt % 100 == 0) {
      auto end_time = std::chrono::high_resolution_clock::now();
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time)
                    .count();
      std::cout << "Resident memory: " << get_memory().second << " MB\n";
      std::cout << "time: " << std::format("{:10}", ms) << " ms     nps: "
                << std::format("{:10}", total_num_positions * 1000 / ms)
                << "\n";
      std::cout << "Minply distribution:\n";
      for (int i = 0; i < num_pos_depth.size(); i++) {
        std::cout << i << ": " << num_pos_depth[i] << "\n";
      }
      std::cout << "\n";
    }

    // release the memory of visited
    visited.clear();
    added_by_dijkstra.clear();
  }

  std::cout << "Finished! Total number of positions: " << total_num_positions
            << "\n";
  std::cout << "Minply distribution:\n";
  for (int i = 0; i < num_pos_depth.size(); i++) {
    std::cout << i << ": " << num_pos_depth[i] << "\n";
  }
  std::cout << "\n";
}
