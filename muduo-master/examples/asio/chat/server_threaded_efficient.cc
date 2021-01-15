#include "examples/asio/chat/codec.h"

#include "muduo/base/Logging.h"
#include "muduo/base/Mutex.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/TcpServer.h"

#include <set>
#include <stdio.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

class ChatServer : noncopyable
{
 public:
  ChatServer(EventLoop* loop,
             const InetAddress& listenAddr)
  : server_(loop, listenAddr, "ChatServer"),
    codec_(std::bind(&ChatServer::onStringMessage, this, _1, _2, _3)),
    connections_(new ConnectionList)
  {
    server_.setConnectionCallback(
        std::bind(&ChatServer::onConnection, this, _1));
    server_.setMessageCallback(
        std::bind(&LengthHeaderCodec::onMessage, &codec_, _1, _2, _3));
  }

  void setThreadNum(int numThreads)
  {
    server_.setThreadNum(numThreads);
  }

  void start()
  {
    server_.start();
  }

 private:
  void onConnection(const TcpConnectionPtr& conn)
  {
    LOG_INFO << conn->peerAddress().toIpPort() << " -> "
        << conn->localAddress().toIpPort() << " is "
        << (conn->connected() ? "UP" : "DOWN");

    MutexLockGuard lock(mutex_); //锁时间比较短
    if (!connections_.unique())//说明引用计数大于1
    {
      //new ConnectionList(*connections_) 这段代码拷贝了一份ConnectionList
      connections_.reset(new ConnectionList(*connections_));
      //reset会将原来的connection_"释放"，再新建一个引用计数为1的connections_，并且原来的connections_引用计数会减1
    }
    assert(connections_.unique());

    //在复本上修改，不会影响读者，所以读者在遍历列表的时候，不需要mutex保护
    if (conn->connected())
    {
      connections_->insert(conn);
    }
    else
    {
      connections_->erase(conn);
    }
  }

  typedef std::set<TcpConnectionPtr> ConnectionList;
  typedef std::shared_ptr<ConnectionList> ConnectionListPtr;

  void onStringMessage(const TcpConnectionPtr&,
                       const string& message,
                       Timestamp)
  {
    //应用计数加1，mutex保护的临界区大大缩短
    ConnectionListPtr connections = getConnectionList();//仅在这里使用了锁
    
    //这里不用担心写者对连接列表的修改，因为写者是在connections_的副本上修改的
    for (ConnectionList::iterator it = connections->begin();
        it != connections->end();
        ++it)
    {
      codec_.send(get_pointer(*it), message);//这种方式仍然有个缺点，因为是在同一个线程完成转发
    }                                        //第一个客户端收到消息和最后一个客户端收到消息的延迟比较高   

    //由于该函数可以跟OnConnection并发执行的，在OnConnection中可能将connections_的引用计数减1
    //所以在退出该函数之前可能已经减1了

    //当connections这个栈上的变量销毁的时候，引用计数减1
  }

  ConnectionListPtr getConnectionList()
  {
    MutexLockGuard lock(mutex_);
    return connections_;
  }

  TcpServer server_;
  LengthHeaderCodec codec_;
  MutexLock mutex_;
  ConnectionListPtr connections_ GUARDED_BY(mutex_);
};

int main(int argc, char* argv[])
{
  LOG_INFO << "pid = " << getpid();
  if (argc > 1)
  {
    EventLoop loop;
    uint16_t port = static_cast<uint16_t>(atoi(argv[1]));
    InetAddress serverAddr(port);
    ChatServer server(&loop, serverAddr);
    if (argc > 2)
    {
      server.setThreadNum(atoi(argv[2]));
    }
    server.start();
    loop.loop();
  }
  else
  {
    printf("Usage: %s port [thread_num]\n", argv[0]);
  }
}

