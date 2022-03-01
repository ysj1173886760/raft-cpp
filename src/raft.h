#include <mutex>
#include <atomic>
#include <vector>
#include <chrono>
#include <queue>
#include <string>

#include "raft.pb.h"

struct CommitMessage {
    std::string data;
    int commitIndex;
};

class Log {
public:
    Log(): 
        _entries(std::vector<Entry>{}),
        _index0(0) {}

private:
    std::vector<Entry> _entries;
    int _index0;
};

typedef std::chrono::time_point<std::chrono::steady_clock> MyTime;

class Raft {
public:
    Raft() {

    }

private:
    // Lock to protect shared access to this peer's state
    std::mutex _mu;
    // TODO: record RPC end points of all raft peers

    // TODO: Persister

    // this peer's index in RPC end points
    int _me;

    // set by Kill()
    std::atomic<bool> _dead;

    // persist state
    int _votedFor;
    int _currentTerm;
    Log _log;

    // for election
    int _leaderID;
    int _currentState;
    MyTime _electionTimer;
    MyTime _heartbeatTimer;
    
    // for log
    int _commitIndex;
    int _lastApplied;
    std::vector<int> _nextIndex;
    std::vector<int> _matchIndex;

    // for commit
    // TODO: use a thread safe queue to mimic the channel
    std::queue<CommitMessage> channel;

    // TODO: snapshot
};