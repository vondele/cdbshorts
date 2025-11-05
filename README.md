# cdbshorts

A small collection of short programs that use [cdbdirect](https://github.com/vondele/cdbdirect) to perform analysis on chess positions or [cdb](https://chessdb.cn/queryc_en/) in general.

## contents

### puzzles

Generate puzzles from the cdb database, which are unique best moves that change game outcome (from loss to draw, draw to win), and also happen to be quiet (no caputres, or checks).

### unseen

Check positions in the cdb local copy for positions where an unseen move to an existing cdb position would change the evaluation of the position 'significantly'.
