#include "searchResult.h"
#include "redis.h"
#include "osl/record/csa.h"
#include "osl/record/record.h"
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <glog/logging.h>
#include <iostream>
#include <sstream>

const std::string
SearchResult::toString() const
{
  std::ostringstream out;
  out << ":depth "      << depth <<
         " :score "     << score <<
         " :consumed "  << consumed_seconds <<
         " :pv "        << pv <<
         " :timestamp " << timestamp <<
         " :moves " << movesToCsaString(moves)
         << std::endl;
  return out.str();
}

const std::string compactBoardToString(const osl::record::CompactBoard& cb)
{
  std::ostringstream ss;
  ss << cb;
  return ss.str();
}


void readMoves(const std::string& binary, osl::stl::vector<osl::Move>& moves)
{
  std::stringstream ss(binary);
  const int size = binary.size() / 4 /* 4 bytes per move */;

  for (int i=0; i<size; ++i) {
    const int i = osl::record::readInt(ss);
    moves.push_back(osl::Move::makeDirect(i));
  }
}

int parseSearchResultReply(const redisReplyPtr reply, SearchResult& sr)
{
  if (checkRedisReply(reply))
    exit(1);
  assert(reply->type == REDIS_REPLY_ARRAY);

  if (reply->elements == 0) {
    LOG(ERROR) << "No existing board found.";
    return 1;
  }

  for(size_t i=0; i<reply->elements; /*empty*/) {
    const redisReply *r = reply->element[i++];
    assert(r->type == REDIS_REPLY_STRING);
    const std::string field(r->str, r->len);
    if ("depth" == field) {
      const redisReply *r = reply->element[i++];
      assert(r->type == REDIS_REPLY_STRING);
      const std::string str(r->str, r->len);
      sr.depth = boost::lexical_cast<int>(str);
    } else if ("score" == field) {
      const redisReply *r = reply->element[i++];
      assert(r->type == REDIS_REPLY_STRING);
      const std::string str(r->str, r->len);
      sr.score = boost::lexical_cast<int>(str);
    } else if ("consumed" == field) {
      const redisReply *r = reply->element[i++];
      assert(r->type == REDIS_REPLY_STRING);
      const std::string str(r->str, r->len);
      sr.consumed_seconds = boost::lexical_cast<int>(str);
    } else if ("pv" == field) {
      const redisReply *r = reply->element[i++];
      assert(r->type == REDIS_REPLY_STRING);
      sr.pv.assign(r->str, r->len);
    } else if ("timestamp" == field) {
      const redisReply *r = reply->element[i++];
      assert(r->type == REDIS_REPLY_STRING);
      const std::string str(r->str, r->len);
      sr.timestamp = boost::lexical_cast<int>(str);
    } else if ("moves" == field) {
      const redisReply *r = reply->element[i++];
      assert(r->type == REDIS_REPLY_STRING);
      const std::string str(r->str, r->len);
      readMoves(str, sr.moves);
    } else {
      LOG(WARNING) << "unknown field found: " << field;
    }
  }

  return 0;
}


int querySearchResult(redisContext *c, SearchResult& sr)
{
  const std::string key = compactBoardToString(sr.board);
  redisReplyPtr reply((redisReply*)redisCommand(c, "HGETALL %b", key.c_str(), key.size()),
                      freeRedisReply);
  parseSearchResultReply(reply, sr);

  return 0;
}

int querySearchResult(redisContext *c, std::vector<SearchResult>& results)
{
  BOOST_FOREACH(const SearchResult& sr, results) {
    const std::string key = compactBoardToString(sr.board);
    redisAppendCommand(c, "HGETALL %b", key.c_str(), key.size());
  }

  BOOST_FOREACH(SearchResult& sr, results) {
    void *r;
    redisGetReply(c, &r);
    redisReplyPtr reply((redisReply*)r, freeRedisReply);
    parseSearchResultReply(reply, sr);
  }
  
  return 0;
}


const std::string movesToCsaString(const osl::stl::vector<osl::Move>& moves)
{
  std::ostringstream out;
  BOOST_FOREACH(const osl::Move move, moves) {
    out << osl::record::csa::show(move);
  }
  return out.str();
}

