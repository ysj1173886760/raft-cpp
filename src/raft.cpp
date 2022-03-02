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

// we will handling response when RPC returns, so status code should always be OK
// unless the package has lost or timeout
Status Raft::AppendEntries(ServerContext* context, const AppendEntriesArgs* request, AppendEntriesReply* response) {
    std::lock_guard<std::mutex> guard(this->_mu);

    if (this->_currentTerm > request->term()) {
        response->set_term(this->_currentTerm);
        response->set_success(false);
        return Status::OK;
    }

    LOG_INFO("[%d] receive AppendEntries from %d term %d currTerm %d prevLogIndex %d prevLogTerm %d",
        this->_me, request->leaderid(), request->term(), this->_currentTerm, request->prevlogindex(), request->prevlogterm());
    // reset timer
    this->_electionTimer = std::chrono::steady_clock::now();
    this->_leaderID = request->leaderid();
    this->_currentState = Follower;

    if (request->term() > this->_currentTerm) {
        this->_currentTerm = request->term();
        // TODO: persist here
    }

    response->set_term(this->_currentTerm);

    // if it's the first log, accept anyway
    // if index of previous entry is larget than current log length, then we fail it
    if (request->prevlogindex() > this->_log.last()) {
        // if the last term of current log is not equal to the leader's, then we ask leader to send it from begining
        // otherwise, we tell the leader where we are
        if (this->_log.lastTerm() != request->prevlogterm()) {
            response->set_conflict(true);
        } else {
            response->set_startfrom(this->_log.last() + 1);
            LOG_INFO("[%d] conflict with same term, start from %d", this->_me, response->startfrom());
        }
        response->set_success(false);
        return Status::OK;
    }

    bool doModified = false;
    if (request->prevlogindex() < this->_log.start()) {
        // we can't match prev log index directly, we can try to overwrite directly
        for (int i = 0; i < request->entries_size(); i++) {
            int index = i + request->prevlogindex() + 1;

            if (index < this->_log.start()) {
                continue;
            }

            if (index <= this->_log.last()) {
                this->_log._entries[index - this->_log.start()] = request->entries(i);
                doModified = true;
            } else {
                this->_log.append(request->entries(i));
                doModified = true;
            }
        }
        // fall though
    } else {
        // check the prev term
        if (this->_log.getTerm(request->prevlogindex()) != request->prevlogterm()) {
            LOG_INFO("[%d] matching failed, current term=%d, expected=%d",
                this->_me, this->_log.getTerm(request->prevlogindex()), request->prevlogterm());

            response->set_success(false);
            response->set_conflict(true);
            return Status::OK;
        }

        if (request->entries_size() > 0) {
            for (int i = 0; i < request->entries_size(); i++) {
                if (request->prevlogindex() + i + 1 <= this->_log.last()) {
                    if (this->_log.getTerm(request->prevlogindex() + i + 1) != request->entries(i).term()) {
                        // remove the conflict log
                        this->_log.truncateEnd(request->prevlogindex() + i + 1);
                        doModified = true;
                        LOG_INFO("[%d] truncate the log, currentLength=%d", this->_me, this->_log.start());
                    } else {
                        continue;
                    }
                }
                this->_log.append(request->entries(i));
                doModified = true;
            }
        }
        // fall though
    }

    // if we do change the log, then do persist
    if (doModified) {
        // TODO: persist here
    }

    if (request->leadercommit() > this->_commitIndex) {
        this->_commitIndex = std::min(request->leadercommit(), this->_log.last());
    }
    if (request->entries_size() > 0) {
        LOG_INFO("[%d] Successfully append the entry term=%d curLogLength=%d commitIndex=%d",
            this->_me, this->_currentTerm, this->_log.start(), this->_commitIndex);
    }
    response->set_success(true);
    return Status::OK;
}

void Raft::singleAppendEntries(int server, int term, bool heartbeat) {
    this->_mu.lock();

    // check the assumption that whether we are the leader
    if (this->_currentTerm != term) {
        LOG_INFO("[%d] stop sending Entries, currentTerm=%d, expected %d", this->_me, this->_currentTerm, term);
        this->_mu.unlock();
        return;
    }

    int index = this->_nextIndex[server];

    // first check whether we need to send new log
    if (index > this->_log.last() && !heartbeat) {
        this->_mu.unlock();
        return;
    }

    AppendEntriesArgs args;
    args.set_term(term);
    args.set_leaderid(this->_me);
    args.set_leadercommit(this->_commitIndex);

    if (index <= this->_log.start()) {
        // just send nothing, we will send the snapshot when handling the reply message
        args.set_prevlogindex(this->_log.start());
        args.set_prevlogterm(this->_log._entries[0].term());
    } else {
        args.set_prevlogindex(index - 1);
        args.set_prevlogterm(this->_log.getTerm(index - 1));
        for (int i = 0; i < this->_log.last() - index + 1; i++) {
            Entry *entry = args.add_entries();
            entry->set_data(this->_log._entries[index + i - this->_log.start()].data());
            entry->set_term(this->_log._entries[index + i - this->_log.start()].term());
        }
    }

    LOG_INFO("[%d] AppendEntries to %d term=%d prevLogIndex=%d prevLogTerm=%d commitIndex=%d",
        this->_me, server, args.term(), args.prevlogindex(), args.prevlogterm(), args.leadercommit());

    this->_mu.unlock();

    AppendEntriesReply reply;
    ClientContext context;

    Status status = this->_peers[server]->AppendEntries(&context, args, &reply);

    if (!status.ok()) {
        return;
    }


    std::lock_guard<std::mutex> guard(this->_mu);

    if (reply.term() > term && reply.term() > this->_currentTerm) {
        this->_currentTerm = reply.term();
        this->_leaderID = -1;
        this->_currentState = Follower;
        // TODO: persist here
    }

    // check assumption
    if (this->_currentState != term) {
        return;
    }

    if (reply.success()) {
        if (args.entries_size() > 0) {
            int l = index + args.entries_size();
            if (l > this->_nextIndex[server]) {
                LOG_INFO("[%d] update nextIndex for server %d value=%d previous %d",
                    this->_me, server, l, this->_nextIndex[server]);
                this->_nextIndex[server] = l;
            }
            if (l - 1 > this->_matchIndex[server]) {
                LOG_INFO("[%d] update match for server %d value=%d previous %d",
                    this->_me, server, l - 1, this->_matchIndex[server]);
                this->_matchIndex[server] = l - 1;
            }
        }
    } else {
        // find the previous term
        int next = index - 1;
        if (reply.conflict()) {
            for (int i = index - 1; i > this->_log.start(); i--) {
                if (this->_log.getTerm(i) != this->_log.getTerm(i - 1)) {
                    next = i;
                    break;
                }
            }
        } else {
            next = reply.startfrom();
        }

        if (this->_nextIndex[server] == index) {
            LOG_INFO("[%d] update nextIndex for server %d value=%d previous %d",
                this->_me, server, next, this->_nextIndex[server]);
            this->_nextIndex[server] = next;

            // send snapshot
            if (this->_nextIndex[server] <= this->_log.start()) {
                // TODO: send snapshot here
            }
        }
    }

}

// start to append entries, called in Start
void Raft::startAppendEntries(int term) {
    std::lock_guard<std::mutex> guard(this->_mu);

    // update timer
    this->_heartbeatTimer = std::chrono::steady_clock::now();

    for (int i = 0; i < this->_num; i++) {
        if (i == this->_me) {
            continue;
        }

        std::thread{
            [this, i, term] (){ 
                this->singleAppendEntries(i, term, false);
            }
        }.detach();
    }
}

// send heartbeat package, called in heartbeat thread
void Raft::startSendHeartbeatPackage(int term) {
    for (int i = 0; i < this->_num; i++) {
        if (i == this->_me) {
            continue;
        }

        std::thread{
            [this, i, term] (){ 
                this->singleAppendEntries(i, term, true);
            }
        }.detach();
    }
}

Status Raft::RequestVote(ServerContext* context, const RequestVoteArgs *request, RequestVoteReply *response) {
    std::lock_guard<std::mutex> guard(this->_mu);
    LOG_INFO("[%d] get RequestVote RPC from %d currentState=%d term=%d",
        this->_me, request->candidatedid(), this->_currentState, request->term());

    if (request->term() < this->_currentTerm) {
        response->set_votegranted(false);
        response->set_term(this->_currentTerm);
        return Status::OK;
    }

    if (request->term() > this->_currentTerm || this->_votedFor == -1) {
        if (request->term() > this->_currentTerm) {
            this->_currentTerm = request->term();
            this->_votedFor = -1;
            // TODO: persist here
        }
        this->_currentState = Follower;

        if (this->_log.last() > 0) {
            // when follower have the log and the candidate didn't
            if (request->lastlogterm() == 0) {
                LOG_INFO("[%d] reject to vote for %d",
                    this->_me, request->candidatedid());
                response->set_votegranted(false);
                response->set_term(this->_currentTerm);
                return Status::OK;
            }

            int curTerm = this->_log.lastTerm();
            // when follower has the newer log
            if (curTerm > request->lastlogterm()) {
                LOG_INFO("[%d] reject to vote for %d",
                    this->_me, request->candidatedid());
                response->set_votegranted(false);
                response->set_term(this->_currentTerm);
                return Status::OK;
            }

            // otherwise, grant the vote
            // fall through
        }

        // update timer only when we grant vote
        this->_electionTimer = std::chrono::steady_clock::now();

        // if we don't have the log, grant anyway
        LOG_INFO("[%d] vote for %d",
            this->_me, request->candidatedid());
        this->_votedFor = request->candidatedid();
        // TODO: persist here
        response->set_term(request->term());
        response->set_votegranted(true);

        return Status::OK;
    }

    // otherwise, deny the vote
    response->set_votegranted(false);
    response->set_term(this->_currentTerm);
    return Status::OK;
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
            // start a new election
            std::thread{
                [this] (){ 
                    this->startNewElection();
                }
            }.detach();
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

            // the reason i didn't use startAppendEntries here is because
            // it will update timer when it has acquire the lock, which won't guarantee
            // we will send heartbeat package in time.
            // i.e. we thought we've send heartbeat in HeartbeatInterval, but due to lock waiting,
            // the time may past longer than that.
            // Just my personal guess, futher experiment is needed
            std::thread{
                [this] (){ 
                    this->startSendHeartbeatPackage(this->_currentTerm);
                }
            }.detach();
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

// this thread can be removed and we can try to update commitIndex
// whenever the matchIndex is updated. 
// i.e. update commitIndex in reply handler of singleAppendEntries
void Raft::updateCommitIndexThread() {
    while (!this->killed()) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(CommonInterval));
        
        this->_mu.lock();

        if (this->_currentState == Leader) {
            int N = this->_commitIndex + 1;
            bool shouldExit = N > this->_log.last();

            while (!shouldExit) {
                int counter = 1;
                for (int i = 0; i < this->_num; i++) {
                    if (i == this->_me) {
                        continue;
                    }
                    if (this->_matchIndex[i] >= N) {
                        counter++;
                    }
                }
                if (counter >= this->getMajority()) {
                    if (this->_log.getTerm(N) == this->_currentTerm) {
                        this->_commitIndex = N;
                        LOG_INFO("[%d] update commit index %d curTerm=%d", 
                            this->_me, this->_commitIndex, this->_currentTerm);
                    }
                } else {
                    shouldExit = true;
                }

                if (N > this->_log.last()) {
                    shouldExit = true;
                }
            }
        }

        this->_mu.unlock();
    }
}

bool Raft::killed() {
    return this->_dead.load(std::memory_order_relaxed);
}

}

int main() {

}