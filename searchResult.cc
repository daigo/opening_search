#include "searchResult.h"
#include "redis.h"
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <glog/logging.h>

const std::string
SearchResult::toString() const
{
  std::ostringstream out;
  out << ":depth "      << depth <<
         " :score "     << score <<
         " :consumed "  << consumed_seconds <<
         " :pv "        << pv <<
         " :timestamp " << timestamp
         << std::endl;
  return out.str();
}

const std::string compactBoardToString(const osl::record::CompactBoard& cb)
{
  std::ostringstream ss;
  ss << cb;
  return ss.str();
}

int querySearchResult(redisContext *c, SearchResult& sr)
{
  const std::string key = compactBoardToString(sr.board);
  redisReplyPtr reply((redisReply*)redisCommand(c, "HGETALL %b", key.c_str(), key.size()),
                      freeRedisReply);
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
    const std::string field(r->str);
    if ("depth" == field) {
      const redisReply *r = reply->element[i++];
      assert(r->type == REDIS_REPLY_STRING);
      sr.depth = boost::lexical_cast<int>(r->str);
    } else if ("score" == field) {
      const redisReply *r = reply->element[i++];
      assert(r->type == REDIS_REPLY_STRING);
      sr.score = boost::lexical_cast<int>(r->str);
    } else if ("consumed" == field) {
      const redisReply *r = reply->element[i++];
      assert(r->type == REDIS_REPLY_STRING);
      sr.consumed_seconds = boost::lexical_cast<int>(r->str);
    } else if ("pv" == field) {
      const redisReply *r = reply->element[i++];
      assert(r->type == REDIS_REPLY_STRING);
      sr.pv.assign(r->str);
    } else if ("timestamp" == field) {
      const redisReply *r = reply->element[i++];
      assert(r->type == REDIS_REPLY_STRING);
      sr.timestamp = boost::lexical_cast<int>(r->str);
    } else {
      LOG(WARNING) << "unknown field found: " << field;
    }
  }

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

    if (reply->elements == 0) {
      LOG(ERROR) << "No existing board found.";
      continue;
    }

    for(size_t i=0; i<reply->elements; /*empty*/) {
      const redisReply *r = reply->element[i++];
      assert(r->type == REDIS_REPLY_STRING);
      const std::string field(r->str);
      if ("depth" == field) {
        const redisReply *r = reply->element[i++];
        assert(r->type == REDIS_REPLY_STRING);
        sr.depth = boost::lexical_cast<int>(r->str);
      } else if ("score" == field) {
        const redisReply *r = reply->element[i++];
        assert(r->type == REDIS_REPLY_STRING);
        sr.score = boost::lexical_cast<int>(r->str);
      } else if ("consumed" == field) {
        const redisReply *r = reply->element[i++];
        assert(r->type == REDIS_REPLY_STRING);
        sr.consumed_seconds = boost::lexical_cast<int>(r->str);
      } else if ("pv" == field) {
        const redisReply *r = reply->element[i++];
        assert(r->type == REDIS_REPLY_STRING);
        sr.pv.assign(r->str);
      } else if ("timestamp" == field) {
        const redisReply *r = reply->element[i++];
        assert(r->type == REDIS_REPLY_STRING);
        sr.timestamp = boost::lexical_cast<int>(r->str);
      } else {
        LOG(WARNING) << "unknown field found: " << field;
      }
    }
  }
  
  return 0;
}

