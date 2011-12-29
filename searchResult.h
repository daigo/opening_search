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
  osl::stl::vector<osl::Move> moves;

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


/**
 * Convert moves into a string of CSA format.
 * ex. -5142OU+5948OU-4232OU+4839OU
 */
const std::string movesToCsaString(const osl::stl::vector<osl::Move>& moves);

#endif /* _GPS_SEARCH_RESULT_H */
// ;;; Local Variables:
// ;;; mode:c++
// ;;; c-basic-offset:2
// ;;; End:
