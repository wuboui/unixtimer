#include "TimeWheel.h"
#include <iostream>
#include "thirdparty/glog/logging.h"
#include "wbl/logger.h"
#include "global.h"
#include <chrono>
#include <thread>
#include <list>

TimeWheel::TimeWheel()
{
    memset(&_time_pos, 0, sizeof(_time_pos));

}


TimeWheel::~TimeWheel()
{
}
int TimeWheel::InitTimerWheel(const int step, const int max_min)
{
    if (1000 % step != 0)
    {
        lerror << "step is not property, should be devided by 1000" ;
        return -1;
    }
    int msNeedCount = 1000 / step;
    int sNeedCount = 60;
    int minNeedCount = max_min;

    _pCallbackList = new std::list<EventInfo>[msNeedCount + sNeedCount + minNeedCount];
    _step_ms = step;

    _lowCount = msNeedCount;
    _midCount = sNeedCount;
    _highCount = minNeedCount;

    std::thread th([&] {
        this->DoLoop();
        });

    th.detach();
    return 0;
}
int TimeWheel::AddTimer(int interval, std::function<void(void)>& call_back)
{
    if (interval < _step_ms || interval % _step_ms != 0 || interval >= _step_ms * _lowCount * _midCount * _highCount)
    {
        lerror << "time interval is invalid" ;
        return -1;
    }


    EventInfo einfo = { 0 };
    einfo.interval = interval;
    einfo.call_back = call_back;
    einfo.time_pos.ms_pos = _time_pos.ms_pos;
    einfo.time_pos.s_pos = _time_pos.s_pos;
    einfo.time_pos.min_pos = _time_pos.min_pos;
    einfo.timer_id = -1;
    InsertTimer(einfo.interval, einfo);


    //linfo << "insert timer success time_id: " << einfo.timer_id ;
    return einfo.timer_id;
}
int TimeWheel::DeleteTimer(int time_id)
{
    std::unique_lock<std::mutex> lock(_mutex);
    int i = 0;
    int nCount = _lowCount + _midCount + _highCount;
    for (i = 0; i < nCount; i++)
    {
        std::list<EventInfo>& leinfo = _pCallbackList[i];
        for (auto item = leinfo.begin(); item != leinfo.end(); item++)
        {
            if (item->timer_id == time_id)
            {
                item = leinfo.erase(item);
                return 0;
            }
        }
    }

    if (i == nCount)
    {
        lerror << "timer " << time_id << " not found";
        return -1;
    }

    return 0;
}
int TimeWheel::DoLoop()
{
    //linfo << "........starting loop........";
    //static int nCount = 0;
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(_step_ms));
        //linfo << ".........this is " << ++nCount << "  loop.........";
        std::unique_lock<std::mutex> lock(_mutex);
        auto callbacklist = _pCallbackList;

        TimePos pos = { 0 };
        TimePos last_pos = _time_pos;
        GetNextTrigerPos(_step_ms, pos);
        _time_pos = pos;
        lock.unlock();

        int pos_call;
        pos_call = 0;

        if (pos.min_pos != last_pos.min_pos)
        {
            pos_call = pos.min_pos + _midCount + _lowCount;
        }
        else if (pos.s_pos != last_pos.s_pos)
        {
            pos_call = pos.s_pos + _lowCount;
        }
        else if (pos.ms_pos != last_pos.ms_pos)
        {
            pos_call = pos.ms_pos;
        }
        else
        {
            lerror << "error time not change" ;
            return -1;
        }

        auto& leinfo = callbacklist[pos_call];
        DealTimeWheeling(leinfo);
        lock.lock();
        reInsertTimeWheel(_pCallbackList[pos_call]);
        _pCallbackList[pos_call].clear();
        lock.unlock();
    }
    return 0;
}
int TimeWheel::GenerateTimerID()
{
    int x = rand() % 0xffffffff;
    int cur_time = time(nullptr);
    return x | cur_time | _timer_count;
}

int TimeWheel::InsertTimer(int diff_ms, EventInfo& einfo)
{

    //linfo << "insert timer " << einfo.timer_id;
    TimePos time_pos = { 0 };
    std::unique_lock<std::mutex> lock(_mutex);

    if (einfo.timer_id == -1) {
        einfo.timer_id = GenerateTimerID();
    }
    _timer_count++;

    GetNextTrigerPos(diff_ms, time_pos);

    einfo.time_pos = time_pos;

    //linfo << "insert timer " << einfo.timer_id << " "  << diff_ms <<" "<< einfo.time_pos.min_pos << " " << einfo.time_pos.s_pos << " " << einfo.time_pos.ms_pos;
    if (time_pos.min_pos != _time_pos.min_pos)
        _pCallbackList[_lowCount + _midCount + time_pos.min_pos].push_back(einfo);
    else if (time_pos.s_pos != _time_pos.s_pos)
        _pCallbackList[_lowCount + time_pos.s_pos].push_back(einfo);
    else if (time_pos.ms_pos != _time_pos.ms_pos)
        _pCallbackList[time_pos.ms_pos].push_back(einfo);


    return 0;
}

int TimeWheel::insertTimerNoLock(int diff_ms, EventInfo& einfo)
{

    linfo << "insert timer " << einfo.timer_id;
    TimePos time_pos = { 0 };

    if (einfo.timer_id == -1) {
        einfo.timer_id = GenerateTimerID();
    }
    _timer_count++;

    GetNextTrigerPos(diff_ms, time_pos);

    einfo.time_pos = time_pos;

    //linfo << "insert timer " << einfo.timer_id << " " << diff_ms << " " << einfo.time_pos.min_pos << " " << einfo.time_pos.s_pos << " " << einfo.time_pos.ms_pos;
    if (time_pos.min_pos != _time_pos.min_pos)
        _pCallbackList[_lowCount + _midCount + time_pos.min_pos].push_back(einfo);
    else if (time_pos.s_pos != _time_pos.s_pos)
        _pCallbackList[_lowCount + time_pos.s_pos].push_back(einfo);
    else if (time_pos.ms_pos != _time_pos.ms_pos)
        _pCallbackList[time_pos.ms_pos].push_back(einfo);


    return 0;
}
int TimeWheel::GetNextTrigerPos(int interval, TimePos& time_pos)
{
    int cur_ms = GetMS(_time_pos);
    int future_ms = cur_ms + interval;

    time_pos.min_pos = (future_ms / 1000 / 60) % _highCount;
    time_pos.s_pos = (future_ms % (1000 * 60)) / 1000;
    time_pos.ms_pos = (future_ms % 1000) / _step_ms;

    return 0;
}

int TimeWheel::GetMS(const TimePos& time_pos)
{
    return _step_ms * time_pos.ms_pos + time_pos.s_pos * 1000 + time_pos.min_pos * 60 * 1000;
}

int TimeWheel::DealTimeWheeling(std::list<EventInfo> leinfo)
{
    for (auto item = leinfo.begin(); item != leinfo.end(); item++)
    {
        int cur_ms = GetMS(_time_pos);
        int last_ms = GetMS(item->time_pos);
        int diff_ms = (cur_ms - last_ms + (_highCount + 1) * 60 * 1000) % ((_highCount + 1) * 60 * 1000);
        if (diff_ms == 0)
        {
            item->call_back();
        }
       // InsertTimer(item->interval - diff_ms, *item);
    }
    return 0;
}

int TimeWheel::reInsertTimeWheel(std::list<EventInfo>& leinfo)
{
    for (auto item = leinfo.begin(); item != leinfo.end(); item++)
    {
        int cur_ms = GetMS(_time_pos);
        int last_ms = GetMS(item->time_pos);
        int diff_ms = (cur_ms - last_ms + (_highCount + 1) * 60 * 1000) % ((_highCount + 1) * 60 * 1000);
       
        insertTimerNoLock(item->interval - diff_ms, *item);
    }
    return 0;
}
