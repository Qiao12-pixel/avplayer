//
// Created by Joe on 26-3-23.
//

#ifndef SYNCNOTIFIER_H
#define SYNCNOTIFIER_H
#include <condition_variable>
#include <mutex>

//同步通知
namespace av {
    class SyncNotifier {
    public:
        SyncNotifier() = default;
        ~SyncNotifier() = default;

        void Notify();
        bool Wait(int timeoutInMilliseconds = -1);
        void Reset();

    private:
        std::mutex m_mutex;
        std::condition_variable m_cond;
        std::atomic<bool> m_triggered{false};
        bool m_manualReset{false};
    };
}



#endif //SYNCNOTIFIER_H
