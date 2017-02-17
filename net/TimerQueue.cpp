#include <net/TimerQueue.h>

#include <net/IOEventLoop.h>
#include <sys/timerfd.h>
#include <support/Log.h>

using namespace agilNet::net;
using namespace boost;
using namespace std;


TimerQueue::TimerQueue(IOEventLoop* eventLoop)
    :loop(eventLoop),
    timerFd(createTimeFd()),
    event(new IOEvent(loop,timerFd))
{

    loop->addEvent(event);
    event->enableReading(true);
    event->setReadFunc(bind(&TimerQueue::timerHandle,this));
}


TimerQueue::~TimerQueue()
{
    ::close(timerFd);
}


void TimerQueue::runOniceAfter(function<void ()> handle,int interval)
{
    MutexGuard lock(mutex);
    addOniceTimer(handle,interval);
}

void TimerQueue::addOniceTimer(function<void ()> handle,uint32_t interval)
{
    shared_ptr<Timer> timer(new Timer(interval,handle));
    oniceTimers.insert(pair<uint64_t,shared_ptr<Timer> >(timer->getTimeOutMSecond(),timer));
    if(needResetTimer(oniceTimers,timer) && needResetTimer(everytimers,timer))
    {
        resetTimer(timer);
    }
}


void TimerQueue::runEveryInterval(boost::function<void ()> handle,int interval)
{
    MutexGuard lock(mutex);
    addEveryTimer(handle,interval);
}

void TimerQueue::addEveryTimer(boost::function<void ()> handle,uint32_t interval)
{
    shared_ptr<Timer> timer(new Timer(interval,handle));
    everytimers.insert(pair<uint64_t,shared_ptr<Timer> >(timer->getTimeOutMSecond(),timer));
    if(needResetTimer(oniceTimers,timer) && needResetTimer(everytimers,timer))
    {
        resetTimer(timer);
    }
}

bool TimerQueue::needResetTimer(multimap<uint64_t,shared_ptr<Timer> > times , shared_ptr<Timer> timer)
{
    if(times.empty())
        return true;

    multimap<uint64_t,shared_ptr<Timer> >::iterator it = times.begin();
    //�������ʱ��С��������ʱ��ʱ�䣬����Ҫ���ö�ʱ����
    if(it->first < timer->getTimeOutMSecond())
    {
        return false;
    }
    //�����ֵ�Ѵ��ڣ�����Ҫ���ö�ʱ��
    if(times.count( timer->getTimeOutMSecond())>1)
    {
        return false;
    }
    return true;
}

void TimerQueue::timerHandle()
{
    MutexGuard lock(mutex);
    readTimerfd();
    multimap<uint64_t,shared_ptr<Timer> >::iterator it;

    //map�ǻ���ƽ����ʵ�֣�����ֻ���������ǰ������ʱ�����˳����ɡ�
    for(it=oniceTimers.begin();it!=oniceTimers.end();it++)
    {
        if(it->first > Timer::getNowTimeMSecond())
        {
            break;
        }
        it->second->timerHandle();
        oniceTimers.erase(it);
    }


    for(it=everytimers.begin();it!=everytimers.end();it++)
    {
        if(it->first > Timer::getNowTimeMSecond())
        {
            break;
        }
        it->second->timerHandle();
        shared_ptr<Timer> timer = it->second;
        timer->update();
        everytimers.insert(pair<uint64_t,shared_ptr<Timer> >(timer->getTimeOutMSecond(),timer));
        everytimers.erase(it);
    }
    resetTimer();
}

void TimerQueue::resetTimer(shared_ptr<Timer> timer)
{
    struct itimerspec newValue;
    struct itimerspec oldValue;
    bzero(&newValue, sizeof newValue);
    bzero(&oldValue, sizeof oldValue);
    newValue.it_value = timer->getTimeInterval();
    int ret = ::timerfd_settime(timerFd, 0, &newValue, &oldValue);
    if (ret<0)
    {
        LogOutput(error) << "timerfd_settime() error";
    }
}


void TimerQueue::resetTimer()
{
    if(oniceTimers.empty())
    {
        if(!everytimers.empty())
        {
            multimap<uint64_t,shared_ptr<Timer> >::iterator it;
            it = everytimers.begin();
            resetTimer(it->second);
        }
    }
    else
    {
        if(everytimers.empty())
        {
            multimap<uint64_t,shared_ptr<Timer> >::iterator it;
            it = oniceTimers.begin();
            resetTimer(it->second);
        }
        else
        {
            multimap<uint64_t,shared_ptr<Timer> >::iterator it1;
            multimap<uint64_t,shared_ptr<Timer> >::iterator it2;
            it1 = everytimers.begin();
            it2 = oniceTimers.begin();
            if(it1->second->getTimeOutMSecond() < it2->second->getTimeOutMSecond())
            {
                resetTimer(it1->second);
            }
            else
            {
                resetTimer(it2->second);
            }
        }
    }
}

int TimerQueue::createTimeFd()
{
  int fd = ::timerfd_create(CLOCK_MONOTONIC,TFD_NONBLOCK | TFD_CLOEXEC);
  if (fd < 0)
  {
    LogOutput(error) << "failed to create time fd";
  }
  return fd;
}

void TimerQueue::readTimerfd()
{
    uint64_t cnt;
    int n = ::read(timerFd, &cnt, sizeof cnt);
    if (n != sizeof cnt)
    {
        LogOutput(error) << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
    }
}
