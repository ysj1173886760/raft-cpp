#include <chrono>
#include <thread>
#include <random>
#include <grpc/grpc.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server_context.h>
#include <grpcpp/client_context.h>

#include "raft.h"
#include "debug.h"
#include "raft.grpc.pb.h"

using grpc::ServerContext;
using grpc::ClientContext;
using grpc::Status;

namespace Raft {

Raft::Raft(const std::vector<std::string> &peer_address, int me) {
    for (auto address: peer_address) {
        this->_peers.push_back(
            RaftRPC::NewStub(
                grpc::CreateChannel(address, 
                grpc::InsecureChannelCredentials())));
    }
    this->_me = me;
}

Status Raft::AppendEntries(ServerContext* context, const AppendEntriesArgs* request, AppendEntriesReply* response) {

}

Status Raft::RequestVote(ServerContext* context, const RequestVoteArgs *request, RequestVoteReply *response) {

}

void Raft::callRequestVote(int server, int term, int *counter, bool *done, const RequestVoteArgs &args) {
    this->_mu.lock();

    RequestVoteReply reply;
    // RPC context, DO NOT REUSE IT BETWEEN CALLS
    ClientContext context;

    this->_mu.unlock();

    // do not hold the lock while calling something that can be blocked
    Status status = this->_peers[server]->RequestVote(&context, args, &reply);

    // failed to get vote
    if (!status.ok() || !reply.votegranted()) {
        return;
    }

    std::lock_guard<std::mutex> guard(this->_mu);
    if (reply.term() > term && reply.term() > this->_currentTerm) {
        this->_currentTerm = reply.term();
        this->_currentState = Follower;
        // TODO: persist here
        return;
    }

    // otherwise, we increase the counter
    // TODO: maybe we can use atomic operations here
    *counter++;
    LOG_INFO("[%d] get voted by %d current count %d", this->_me, server, *counter);

    if (*done || *counter < this->getMajority()) {
        return;
    }

    // if it's the first we get majority vote, convert self to leader
    *done = true;
    this->_currentState = Leader;

    // TODO: send heartbeat packages here
    this->_heartbeatTimer = std::chrono::steady_clock::now();

    LOG_INFO("[%d] Wins to be a leader at term %d", this->_me, term);

    // reinitialize the leader state
    this->_nextIndex = std::vector<int> (this->_num, 0);
    this->_matchIndex = std::vector<int> (this->_num, 0);
    for (int i = 0; i < this->_num; i++) {
        this->_nextIndex[i] = this->_log.last() + 1;
        this->_matchIndex[i] = 0;
    }
}

int Raft::getMajority() {
    return this->_num / 2 + 1;
}

// start a new election
// called by electionThread
void Raft::startNewElection() {
    this->_mu.lock();

    // increase term number
    this->_currentTerm++;
    // vote for self
    this->_votedFor = this->_me;

    // TODO: persist here
    // because we've changed votedFor, which is a persist state

    // convert to candidate
    this->_currentState = Candidate;

    // this is the counter for the voting
    int counter = 1;
    // for saving the invariant
    int term = this->_currentTerm;
    bool done = false;

    // initialize args
    RequestVoteArgs args;
    args.set_term(term);
    args.set_candidatedid(this->_me);
    args.set_lastlogindex(this->_log.last());
    args.set_lastlogterm(this->_log.lastTerm());

    this->_mu.unlock();

    LOG_INFO("[%d] start to Election term %d", this->_me, term);
    
    for (int i = 0; i < this->_peers.size(); i++) {
        if (i == this->_me) {
            continue;
        }

        std::thread{
            [this, i, term, &counter, &done, args] (){ 
                this->callRequestVote(i, term, &counter, &done, args);
            }
        }.detach();
    }
}

void Raft::electionThread() {
    // random generator
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> 
        dist(ElectionLowerBound, ElectionUpperBound);

    while (!this->killed()) {
        auto interval = std::chrono::milliseconds(dist(rng));
        std::this_thread::sleep_for(
            std::chrono::milliseconds(CommonInterval));
        this->_mu.lock();
        auto end = std::chrono::steady_clock::now();
        if (end - this->_electionTimer >= interval && 
            this->_currentState != Leader) {
            // TODO: start a new election
            this->_electionTimer = std::chrono::steady_clock::now();
        }
        this->_mu.unlock();
    }
}

void Raft::heartbeatThread() {
    while (!this->killed()) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(CommonInterval));
        this->_mu.lock();
        auto end = std::chrono::steady_clock::now();
        if (end - this->_heartbeatTimer >= std::chrono::milliseconds(HeartbeatInterval) &&
            this->_currentState == Leader) {
            // TODO: start new thread to send heartbeat packages
            this->_heartbeatTimer = std::chrono::steady_clock::now();
        }
        this->_mu.unlock();
    }
}

void Raft::commitThread() {
    while (!this->killed()) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(CommonInterval));

        this->_mu.lock();

        // for snapshot
        if (this->_lastApplied < this->_log.start()) {
            this->_lastApplied = this->_log.start();
        }
        
        while (this->_lastApplied < this->_commitIndex) {
            this->_lastApplied++;
            int index = this->_lastApplied;
            CommitMessage msg = CommitMessage(
                this->_log._entries[index - this->_log.start()].data(),
                index
            );

            // TODO: change this to thread safe queue
            // we may get blocked while pushing the msg
            // we should not hold the mutex while doing some operation
            // that might get blocked
            this->channel.push(msg);
        }

        this->_mu.unlock();
    }
}

void Raft::updateCommitIndexThread() {
    while (!this->killed()) {
    }
}

bool Raft::killed() {
    return this->_dead.load(std::memory_order_relaxed);
}

}

int main() {

}