#include <hiredis/hiredis.h>#include <boost/shared_ptr.hpp>#include <string>void connectRedisServer(redisContext **c, const std::string& host, const int port);bool authenticate(redisContext *c, const std::string& password);typedef boost::shared_ptr<redisReply> redisReplyPtr;void freeRedisReply(redisReply *reply);int checkRedisReply(const redisReplyPtr& reply);