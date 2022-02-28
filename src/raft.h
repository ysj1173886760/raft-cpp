#include <mutex>
#include <atomic>
#include <vector>
#include <chrono>
#include "raft.pb.h"

struct Log {
    std::vector<Entry> Entries;
    int Index0;
};

typedef std::chrono::time_point<std::chrono::steady_clock> MyTime;

struct Raft {
    // Lock to protect shared access to this peer's state
    std::mutex Mu;
    // TODO: record RPC end points of all raft peers

    // TODO: Persister

    // this peer's index in RPC end points
    int Me;

    // set by Kill()
    std::atomic<bool> Dead;

    // persist state
    int VotedFor;
    int CurrentTerm;
    Log log;

    // for election
    int LeaderID;
    int CurrentState;
    MyTime ElectionTimer;
    MyTime HeartbeatTimer;
};