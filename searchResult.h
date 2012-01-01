#ifndef _GPS_SEARCH_RESULT_H
#define _GPS_SEARCH_RESULT_H

#include "osl/record/compactBoard.h"
#include <functional>
#include <vector>
#include <string>

typedef osl::stl::vector<osl::Move> moves_t;

struct SearchResult {
  osl::record::CompactBoard board;
  int depth;
  int score;            // evaluation value
  int consumed_seconds; // actual seconds consumed by thinking.
  time_t timestamp;     // current time stamp as seconds from Epoch.
  std::string pv;
  moves_t moves;

  explicit SearchResult(const osl::record::CompactBoard& _board)
    : board(_board),
      depth(0), score(0), consumed_seconds(0), timestamp(time(NULL))
  {}

  const std::string timeString() const;
  const std::string toString() const;
};

struct SearchResultCompare : public std::binary_function<SearchResult, SearchResult, bool> {
  bool operator()(const SearchResult& lhs, const SearchResult& rhs) const {
    return lhs.score < rhs.score;
  }
};

struct redisContext; // forward declaration

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
const std::string movesToCsaString(const moves_t& moves);

const std::string stateToString(const osl::SimpleState& state, const osl::Move last_move=osl::Move());

#endif /* _GPS_SEARCH_RESULT_H */
// ;;; Local Variables:
// ;;; mode:c++
// ;;; c-basic-offset:2
// ;;; End:
