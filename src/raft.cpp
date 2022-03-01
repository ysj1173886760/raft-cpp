#include "raft.h"

#include <chrono>
#include <thread>
#include <random>

Raft::Raft() {

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

    this->_mu.unlock();

    // TODO: iterate all peers and call request vote RPC
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

voud Raft::heartbeatThread() {
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
        if (this->_lastApplied < this->log.start()) {
            this->_lastAppliedt = this->log.start();
        }
        
        while (this->_lastApplied < this->commitIndex) {
            this->_lastApplied++;
            int index = this->_lastApplied;
            CommitMessage msg = CommitMessage(
                this->log._entries[index - this->log.start()].Data,
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

int main() {

}