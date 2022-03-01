#include <mutex>
#include <atomic>
#include <vector>
#include <chrono>
#include <queue>
#include <string>

#include "log.h"
#include "raft.grpc.pb.h"

#include <grpc/grpc.h>
#include <grpcpp/server_context.h>

using grpc::ServerContext;
using grpc::Status;

enum State {
    Leader = 0,
    Follower,
    Candidate,
};

// for election thread
const int ElectionLowerBound = 1000;
const int ElectionUpperBound = 1300;
// for heartbeat thread
const int HeartbeatInterval = 150;
// for commit thread and update commit index thread
const int CommonInterval = 10;

struct CommitMessage {
    std::string data;
    int commitIndex;

    CommitMessage(const std::string &data, int commitIndex):
        data(data), 
        commitIndex(commitIndex) {}
};

typedef std::chrono::time_point<std::chrono::steady_clock> MyTime;

class Raft: public RaftRPC::Service {
public:
    // API interface
    Raft();

    virtual ~Raft() {}

    // the first return value is the index that the command will appear at
    // if it's ever committed. the second return value is the current
    // term. the third return value is true if this server believes it is
    // the leader.
    std::tuple<int, int, bool> Start(std::string data);

    void Kill();

    // return currentTerm and whether this server
    // believes it is the leader.

    std::tuple<int, bool> GetState();

    // RPC call

    Status AppendEntries(ServerContext* context, const AppendEntriesArgs* request, AppendEntriesReply* response);
    Status RequestVote(ServerContext* context, const RequestVoteArgs *request, RequestVoteReply *response);

private:
    bool killed();
    void startNewElection();
    
    // background thread used to start new election
    void electionThread();
    
    // background thread used to send heartbeat to followers
    void heartbeatThread();

    // background thread used to commit log to upper services
    void commitThread();

    // background thread used to update commit index
    void updateCommitIndexThread();

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
    State _currentState;
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

