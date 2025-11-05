# cdbshorts

A small collection of short programs that use [cdbdirect](https://github.com/vondele/cdbdirect) to perform analysis on chess positions or [cdb](https://chessdb.cn/queryc_en/) in general.

## contents

### puzzles

Generate puzzles from the cdb database, which are unique best moves that change game outcome (from loss to draw, draw to win), and also happen to be quiet (no caputres, or checks).

### unseen

Check positions in the cdb local copy for positions where an unseen move to an existing cdb position would change the evaluation of the position 'significantly'.

### fakeleaves

Check positions in cdb local copy with one scored move, out of multiple legal that connect to a scored position.

### books

construct opening books, with properties on the eval as well as the PV

## usage

### building

See [cdbdirect](https://github.com/vondele/cdbdirect?tab=readme-ov-file#building) as it is a prerequisite for building this project.

```bash
make -j all  TERARKDBROOT=/foo/bar1/ CDBDIRECTROOT=/foo/bar2/ CHESS_PATH=/foo/bar3/
```

or

```bash
make -j puzzles TERARKDBROOT=/foo/bar1/ CDBDIRECTROOT=/foo/bar2/ CHESS_PATH=/foo/bar3/

```

### executing

currently the shorts have no config options, except in the source (e.g. max_entries), just executing the binaries without arguments will do.
