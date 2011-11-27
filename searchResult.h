#ifndef _GPS_SEARCH_RESULT_H
#define _GPS_SEARCH_RESULT_H

#include "osl/record/compactBoard.h"
#include <vector>
#include <string>

struct SearchResult {
  osl::record::CompactBoard board;
  int depth;
  int score;            // evaluation value
  int consumed_seconds; // actual seconds consumed by thinking.
  time_t timestamp;     // current time stamp as seconds from Epoch.
  std::string pv;

  SearchResult(const osl::record::CompactBoard& _board)
    : board(_board),
      depth(0), score(0), consumed_seconds(0), timestamp(time(NULL))
  {}

  const std::string toString() const;
};

struct redisContext;

const std::string compactBoardToString(const osl::record::CompactBoard& cb);

int querySearchResult(redisContext *c, SearchResult& sr);

/**
 * Pipelined version.
 */
int querySearchResult(redisContext *c, std::vector<SearchResult>& results);

#endif /* _GPS_SEARCH_RESULT_H */
// ;;; Local Variables:
// ;;; mode:c++
// ;;; c-basic-offset:2
// ;;; End:
