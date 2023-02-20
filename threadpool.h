#pragma once

struct Threadpool {
    std::deque<std::function<void()>> Workitems;
    std::vector<std::thread> Threads;
    std::condition_variable Cv;
    std::mutex Mutex;
    bool WorkerContinue;

    Threadpool() = delete;
    Threadpool(size_t ThreadCount = 1) : WorkerContinue(true)
    {
        Threads.reserve(ThreadCount);
        for (auto i = 0u; i < ThreadCount; ++i) {
            Threads.emplace_back(std::thread(&Threadpool::ThreadpoolWorker, this));
        }
    }
    Threadpool(const Threadpool&) = delete;
    Threadpool& operator= (const Threadpool&) = delete;
    Threadpool(Threadpool&&) = default;
    ~Threadpool()
    {
        {
            std::unique_lock<std::mutex> Lock(Mutex);
            WorkerContinue = false;
            Lock.unlock();
            Cv.notify_all();
        }
        for (auto& Thread : Threads) {
            Thread.join();
        }
    }

    void ThreadpoolWorker()
    {
        while (WorkerContinue) {
            std::unique_lock<std::mutex> Lock(Mutex);
            Cv.wait(Lock, [this]{return Workitems.size() > 0 || !WorkerContinue;});
            if (!WorkerContinue) {
                break;
            }
            auto Workitem = std::move(Workitems.front());
            Workitems.pop_front();
            Lock.unlock();
            Workitem();
        }
    }

    template <typename Callable, typename... Args>
    void Enqueue(Callable&& Fn, Args&&... Parms)
    {
        std::unique_lock<std::mutex> Lock(Mutex);
        Workitems.emplace_back(std::bind(Fn, std::forward<Args>(Parms)...));
        Lock.unlock();
        Cv.notify_one();
    }
};
