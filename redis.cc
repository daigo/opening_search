#include "redis.h"
#include <glog/logging.h>
#include <iostream>
#include <cassert>

void connectRedisServer(redisContext **context, const std::string& host, const int port) {
  const struct timeval timeout = { 1, 500000 }; // 1.5 seconds
  redisContext *c = redisConnectWithTimeout(host.c_str(), port, timeout);
  if (c->err) {
    std::cerr << "Connection error: " << c->errstr << std::endl;
  } else {
    *context = c;
  }
}

void freeRedisReply(redisReply *reply) {
  freeReplyObject(reply);
}

int checkRedisReply(const redisReplyPtr& reply) {
  assert(reply);
  if (reply->type == REDIS_REPLY_ERROR) {
    LOG(ERROR) << reply->str;
    return 1;
  }
  return 0;
}

