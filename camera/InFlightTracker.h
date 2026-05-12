#ifndef IN_FLIGHT_TRACKER_H
#define IN_FLIGHT_TRACKER_H

#include <stdint.h>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace mocha {

struct InFlightRequest {
    uint32_t frameNumber;
    void* outputBuffer;
    bool markedError;

    InFlightRequest()
        : frameNumber(0), outputBuffer(nullptr), markedError(false) {}
};

class InFlightTracker {
public:
    InFlightTracker() {}

    void add(uint32_t frameNumber, void* outputBuffer) {
        std::lock_guard<std::mutex> lock(mMutex);
        InFlightRequest req;
        req.frameNumber = frameNumber;
        req.outputBuffer = outputBuffer;
        req.markedError = false;
        mContexts[frameNumber] = req;
    }

    bool remove(uint32_t frameNumber) {
        std::lock_guard<std::mutex> lock(mMutex);
        return mContexts.erase(frameNumber) > 0;
    }

    void markAllAsError() {
        std::lock_guard<std::mutex> lock(mMutex);
        for (auto& entry : mContexts) {
            entry.second.markedError = true;
        }
    }

    bool isError(uint32_t frameNumber) {
        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mContexts.find(frameNumber);
        if (it == mContexts.end()) return false;
        return it->second.markedError;
    }

    size_t count() const {
        std::lock_guard<std::mutex> lock(mMutex);
        return mContexts.size();
    }

    std::vector<uint32_t> drainAll() {
        std::lock_guard<std::mutex> lock(mMutex);
        std::vector<uint32_t> frames;
        for (auto& entry : mContexts) {
            frames.push_back(entry.first);
        }
        mContexts.clear();
        return frames;
    }

private:
    mutable std::mutex mMutex;
    std::map<uint32_t, InFlightRequest> mContexts;
};

} // namespace mocha

#endif // IN_FLIGHT_TRACKER_H
