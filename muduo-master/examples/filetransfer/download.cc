#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/TcpServer.h"

#include <stdio.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

const char* g_file = NULL;

// FIXME: use FileUtil::readFile()
//该版本是将文件一次性读入内存中，如果文件1个G，那么将消耗1G的内存
string readFile(const char* filename)
{
  string content;
  FILE* fp = ::fopen(filename, "rb");
  if (fp)
  {
    // inefficient!!!
    const int kBufSize = 1024*1024;
    char iobuf[kBufSize];
    ::setbuffer(fp, iobuf, sizeof iobuf);//设置文件缓冲区
                                         //系统内核本身有一个缓存，这里设置的缓存是用户空间的缓存
                                         //这样可以减少系统调用次数
    char buf[kBufSize];
    size_t nread = 0;
    while ( (nread = ::fread(buf, 1, sizeof buf, fp)) > 0)
    {
      content.append(buf, nread);
    }
    ::fclose(fp);
  }
  return content;
}

void onHighWaterMark(const TcpConnectionPtr& conn, size_t len)
{
  LOG_INFO << "HighWaterMark " << len;
}

void onConnection(const TcpConnectionPtr& conn)
{
  LOG_INFO << "FileServer - " << conn->peerAddress().toIpPort() << " -> "
           << conn->localAddress().toIpPort() << " is "
           << (conn->connected() ? "UP" : "DOWN");
  if (conn->connected())
  {
    LOG_INFO << "FileServer - Sending file " << g_file
             << " to " << conn->peerAddress().toIpPort();
    conn->setHighWaterMarkCallback(onHighWaterMark, 64*1024);
    string fileContent = readFile(g_file);
    conn->send(fileContent);//send函数非阻塞的，不用关心数据什么时候送达
    conn->shutdown(); //关闭写的一半；马上就调用shutdown函数是没有问题的
                      //如果连接中没有正在发生写事件，那么shutdown函数直接关闭写的一半
                      //如果写事件正在发生，shutdown函数只是把连接状态改成disconnectting
                      //在handlewrite函数中，当buffer读完后，会判断，如果状态是disconnectting
                      //就调用shutdownInLoop函数
                      //所以，在send之后马上调用shutdown，不用担心数据没发完
    LOG_INFO << "FileServer - done";
  }
}

int main(int argc, char* argv[])
{
  LOG_INFO << "pid = " << getpid();
  if (argc > 1)
  {
    g_file = argv[1];

    EventLoop loop;
    InetAddress listenAddr(2021);
    TcpServer server(&loop, listenAddr, "FileServer");
    server.setConnectionCallback(onConnection);
    server.start();
    loop.loop();
  }
  else
  {
    fprintf(stderr, "Usage: %s file_for_downloading\n", argv[0]);
  }
}

